// Copyright 2004-present Facebook. All Rights Reserved.
#include "fboss/util/wedge_qsfp_util.h"
#include "fboss/lib/firmware_storage/FbossFirmware.h"
#include "fboss/lib/i2c/FirmwareUpgrader.h"

#include "fboss/lib/usb/WedgeI2CBus.h"
#include "fboss/lib/usb/Wedge100I2CBus.h"
#include "fboss/lib/usb/GalaxyI2CBus.h"

#include "fboss/qsfp_service/module/QsfpModule.h"
#include "fboss/qsfp_service/module/cmis/CmisModule.h"
#include "fboss/qsfp_service/module/sff/SffModule.h"
#include "fboss/qsfp_service/platforms/wedge/WedgeQsfp.h"

#include "fboss/qsfp_service/lib/QsfpClient.h"
#include "fboss/qsfp_service/module/cmis/CmisFieldInfo.h"

#include <folly/Conv.h>
#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <chrono>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sysexits.h>

#include <thread>
#include <vector>
#include <utility>

namespace {
const std::map<uint8_t, std::string> kCmisAppNameMapping = {
    {0x10, "100G_CWDM4"},
    {0x18, "200G_FR4"},
};

const std::map<uint8_t, std::string> kCmisModuleStateMapping = {
    {0b001, "LowPower"},
    {0b010, "PoweringUp"},
    {0b011, "Ready"},
    {0b100, "PoweringDown"},
    {0b101, "Fault"},
};

const std::map<uint8_t, std::string> kCmisLaneStateMapping = {
    {0b001, "DEACT"},
    {0b010, "INITL"},
    {0b011, "DEINT"},
    {0b100, "ACTIV"},
    {0b101, "TX_ON"},
    {0b110, "TXOFF"},
    {0b111, "DPINT"},
};

std::string getStateNameString(uint8_t stateCode, const std::map<uint8_t, std::string>& nameMap) {
  std::string stateName = "UNKNOWN";
  if (auto iter = nameMap.find(stateCode);
        iter != nameMap.end()) {
    stateName = iter->second;
  }
  return stateName;
}
}

using namespace facebook::fboss;
using folly::MutableByteRange;
using folly::StringPiece;
using std::pair;
using std::make_pair;
using std::chrono::seconds;
using std::chrono::steady_clock;

// We can check on the hardware type:

const char *chipCheckPath = "/sys/bus/pci/devices/0000:01:00.0/device";
const char *trident2 = "0xb850\n";  // Note expected carriage return
static constexpr uint16_t hexBase = 16;
static constexpr uint16_t decimalBase = 10;
static constexpr uint16_t eePromDefault = 255;
static constexpr uint16_t maxGauge = 30;

DEFINE_bool(clear_low_power, false,
            "Allow the QSFP to use higher power; needed for LR4 optics");
DEFINE_bool(set_low_power, false,
            "Force the QSFP to limit power usage; Only useful for testing");
DEFINE_bool(tx_disable, false, "Set the TX disable bits");
DEFINE_bool(tx_enable, false, "Clear the TX disable bits");
DEFINE_bool(set_40g, false, "Rate select 40G");
DEFINE_bool(set_100g, false, "Rate select 100G");
DEFINE_bool(cdr_enable, false, "Set the CDR bits if transceiver supports it");
DEFINE_bool(cdr_disable, false,
    "Clear the CDR bits if transceiver supports it");
DEFINE_int32(open_timeout, 30, "Number of seconds to wait to open bus");
DEFINE_bool(direct_i2c, false,
    "Read Transceiver info from i2c bus instead of qsfp_service");
DEFINE_bool(qsfp_hard_reset, false, "Issue a hard reset to port QSFP");
DEFINE_bool(electrical_loopback, false,
            "Set the module to be electrical loopback, only for Miniphoton");
DEFINE_bool(optical_loopback, false,
            "Set the module to be optical loopback, only for Miniphoton");
DEFINE_bool(clear_loopback, false,
            "Clear the module loopback bits, only for Miniphoton");
DEFINE_bool(read_reg, false, "Read a register, use with --offset and optionally --length");
DEFINE_bool(write_reg, false, "Write a register, use with --offset and --data");
DEFINE_int32(offset, -1, "The offset of register to read/write (0..255)");
DEFINE_int32(data, 0, "The byte to write to the register, use with --offset");
DEFINE_int32(length, 1, "The number of bytes to read from the register (1..128), use with --offset");
DEFINE_int32(pause_remediation, 0,
    "Number of seconds to prevent qsfp_service from doing remediation to modules");
DEFINE_bool(update_module_firmware, false,
            "Update firmware for module, use with --firmware_filename");
DEFINE_string(firmware_filename, "",
            "Module firmware filename along with path");
DEFINE_uint32(msa_password, 0x00001011, "MSA password for module privilige operation");
DEFINE_uint32(image_header_len, 0, "Firmware image header length");
DEFINE_bool(get_module_fw_info, false, "Get the module  firmware info for list of ports, use with portA and portB");

enum LoopbackMode {
  noLoopback,
  electricalLoopback,
  opticalLoopback
};

// CMIS module Identifier (from module register 0)
constexpr uint8_t kCMISIdentifier = 0x1e;

struct ModulePartInfo_s {
  std::array<uint8_t, 16> partNo;
  uint32_t headerLen;
};
struct ModulePartInfo_s modulePartInfo[] = {
  // Finisar 200G module info
  {{'F','T','C','C','1','1','1','2','E','1','P','L','L','-','F','B'}, 64},
  // Innolight 200G module info
  {{'T','-','F','X','4','F','N','T','-','H','F','B',0x20,0x20,0x20,0x20}, 48}
};
constexpr uint8_t kNumModuleInfo = sizeof(modulePartInfo)/sizeof(struct ModulePartInfo_s);

std::unique_ptr<facebook::fboss::QsfpServiceAsyncClient> getQsfpClient(folly::EventBase& evb) {
  return std::move(QsfpClient::createClient(&evb)).getVia(&evb);
}

bool overrideLowPower(
    TransceiverI2CApi* bus,
    unsigned int port,
    uint8_t value) {
  // 0x01 overrides low power mode
  // 0x04 is an LR4-specific bit that is otherwise reserved
  uint8_t buf[1] = {value};
  try {
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 93, 1, buf);
  } catch (const I2cError& ex) {
    // This generally means the QSFP module is not present.
    fprintf(stderr, "QSFP %d: not present or unwritable\n", port);
    return false;
  }
  return true;
}

bool setCdr(TransceiverI2CApi* bus, unsigned int port, uint8_t value) {
  // 0xff to enable
  // 0x00 to disable

  // Check if CDR is supported
  uint8_t supported[1];
  try {
    // ensure we have page0 selected
    uint8_t page0 = 0;
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP,
                     127, 1, &page0);

    bus->moduleRead(port, TransceiverI2CApi::ADDR_QSFP, 129,
                      1, supported);
  } catch (const I2cError& ex) {
    fprintf(stderr, "Port %d: Unable to determine whether CDR supported: %s\n",
            port, ex.what());
    return false;
  }
  // If 2nd and 3rd bits are set, CDR is supported.
  if ((supported[0] & 0xC) != 0xC) {
    fprintf(stderr, "CDR unsupported by this device, doing nothing");
    return false;
  }

  // Even if CDR isn't supported for one of RX and TX, set the whole
  // byte anyway
  uint8_t buf[1] = {value};
  try {
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 98, 1, buf);
  } catch (const I2cError& ex) {
    fprintf(stderr, "QSFP %d: Failed to set CDR\n", port);
    return false;
  }
  return true;
}

bool rateSelect(TransceiverI2CApi* bus, unsigned int port, uint8_t value) {
  // If v1 is used, both at 10, if v2
  // 0b10 - 25G channels
  // 0b00 - 10G channels
  uint8_t version[1];
  try {
    // ensure we have page0 selected
    uint8_t page0 = 0;
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP,
                     127, 1, &page0);

    bus->moduleRead(port, TransceiverI2CApi::ADDR_QSFP, 141,
                      1, version);
  } catch (const I2cError& ex) {
    fprintf(stderr,
        "Port %d: Unable to determine rate select version in use, defaulting \
        to V1\n",
        port);
    version[0] = 0b01;
  }

  uint8_t buf[1];
  if (version[0] & 1) {
    buf[0] = 0b10;
  } else {
    buf[0] = value;
  }

  try {
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 87, 1, buf);
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 88, 1, buf);
  } catch (const I2cError& ex) {
    // This generally means the QSFP module is not present.
    fprintf(stderr, "QSFP %d: not present or unwritable\n", port);
    return false;
  }
  return true;
}

/*
 * This function returns the module type whether it is CMIS or SFF type
 * by reading the register 0 from module
 */
TransceiverManagementInterface getModuleType(
  TransceiverI2CApi* bus, unsigned int port) {
  uint8_t moduleId;

  // Get the module id to differentiate between CMIS (0x1e) and SFF
  try {
    bus->moduleRead(port, TransceiverI2CApi::ADDR_QSFP, 0, 1, &moduleId);
  } catch (const I2cError& ex) {
    fprintf(stderr, "QSFP %d: not present or read error\n", port);
  }

  if (moduleId == kCMISIdentifier) {
    return TransceiverManagementInterface::CMIS;
  } else {
    return TransceiverManagementInterface::SFF;
  }
}

/*
 * This function disables the optics lane TX which brings down the port. The
 * TX Disable will cause LOS at the link partner and Remote Fault at this end.
 */
bool setTxDisable(TransceiverI2CApi* bus, unsigned int port, bool disable) {
  std::array<uint8_t, 1> buf;

  // Get module type CMIS or SFF
  auto moduleType = getModuleType(bus, port);

  if (moduleType != TransceiverManagementInterface::CMIS) {
    // For SFF the value 0xf disables all 4 lanes and 0x0 enables it back
    buf[0] = disable ? 0xf : 0x0;

    // For SFF module, the page 0 reg 86 controls TX_DISABLE for 4 lanes
    try {
      bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 86, 1, &buf[0]);
    } catch (const I2cError& ex) {
      fprintf(stderr, "QSFP %d: unwritable or write error\n", port);
      return false;
    }
  } else {
    // For CMIS module, the page 0x10 reg 130 controls TX_DISABLE for 8 lanes
    uint8_t savedPage, moduleControlPage=0x10;

    // For CMIS the value 0xff disables all 8 lanes and 0x0 enables it back
    buf[0] = disable ? 0xff : 0x0;

    try {
      // Save current page
      bus->moduleRead(port, TransceiverI2CApi::ADDR_QSFP, 127, 1, &savedPage);
      // Write page 10 reg 130
      bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 127, 1, &moduleControlPage);
      bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 130, 1, &buf[0]);
      // Restore current page
      bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 127, 1, &savedPage);
    } catch (const I2cError& ex) {
      fprintf(stderr, "QSFP %d: read/write error\n", port);
      return false;
    }
  }

  return true;
}

void doReadReg(TransceiverI2CApi* bus, unsigned int port, int offset, int length) {
  uint8_t buf[128];
  try {
    bus->moduleRead(port, TransceiverI2CApi::ADDR_QSFP, offset, length, buf);
  } catch (const I2cError& ex) {
    fprintf(stderr, "QSFP %d: fail to read module\n", port);
    return;
  }
  // Print the read registers
  // Print 16 bytes in a line with offset at start and extra gap after 8 bytes
  for (int i=0; i<length; i++) {
    if (i % 16 == 0) {
      // New line after 16 bytes (except the first line)
      if (i != 0) {
        printf("\n");
      }
      // 2 byte offset at start of every line
      printf("%04x: ", offset+i);
    } else if (i % 8 == 0) {
      // Extra gap after 8 bytes in a line
      printf(" ");
    }
    printf("%02x ", buf[i]);
  }
  printf("\n");
}

void doWriteReg(TransceiverI2CApi* bus, unsigned int port, int offset, uint8_t value) {
  uint8_t buf[1] = {value};
  try {
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, offset, 1, buf);
  } catch (const I2cError& ex) {
    fprintf(stderr, "QSFP %d: not present or unwritable\n", port);
    return;
  }
  printf("QSFP %d: successfully write 0x%02x to %d.\n", port, value, offset);
}

std::map<int32_t, DOMDataUnion> fetchDataFromQsfpService(
    const std::vector<int32_t>& ports, folly::EventBase& evb) {
  auto client = getQsfpClient(evb);

  std::map<int32_t, TransceiverInfo> qsfpInfoMap;

  client->sync_getTransceiverInfo(qsfpInfoMap, ports);

  std::vector<int32_t> presentPorts;
  for(auto& qsfpInfo : qsfpInfoMap) {
    if (*qsfpInfo.second.present_ref()) {
      presentPorts.push_back(qsfpInfo.first);
    }
  }

  std::map<int32_t, DOMDataUnion> domDataUnionMap;

  if(!presentPorts.empty()) {
    client->sync_getTransceiverDOMDataUnion(domDataUnionMap, presentPorts);
  }

  return domDataUnionMap;
}

std::map<int32_t, TransceiverInfo> fetchInfoFromQsfpService(
    const std::vector<int32_t>& ports) {
  folly::EventBase evb;
  auto qsfpServiceClient = QsfpClient::createClient(&evb);
  auto client = std::move(qsfpServiceClient).getVia(&evb);

  std::map<int32_t, TransceiverInfo> qsfpInfoMap;

  client->sync_getTransceiverInfo(qsfpInfoMap, ports);

  return qsfpInfoMap;
}

DOMDataUnion fetchDataFromLocalI2CBus(TransceiverI2CApi* bus, unsigned int port) {
  // port is 1 based and WedgeQsfp is 0 based.
  auto qsfpImpl = std::make_unique<WedgeQsfp>(port - 1, bus);
  auto mgmtInterface = qsfpImpl->getTransceiverManagementInterface();
  if (mgmtInterface == TransceiverManagementInterface::CMIS)
    {
      auto cmisModule =
          std::make_unique<CmisModule>(
              nullptr,
              std::move(qsfpImpl),
              1);
      cmisModule->refresh();
      return cmisModule->getDOMDataUnion();
    } else if (mgmtInterface == TransceiverManagementInterface::SFF)
    {
      auto sffModule =
          std::make_unique<SffModule>(
              nullptr,
              std::move(qsfpImpl),
              1);
      sffModule->refresh();
      return sffModule->getDOMDataUnion();
    } else {
      throw std::runtime_error(folly::sformat(
          "Unknown transceiver management interface: {}.",
          static_cast<int>(mgmtInterface)));
    }
}

void printPortSummary(TransceiverI2CApi*) {
  // TODO: Implement code for showing a summary of all ports.
  // At the moment I haven't tested this since my test switch has some
  // 3M modules plugged in that hang up the bus sometimes when accessed by our
  // CP2112 switch.  I'll implement this in a subsequent diff.
  fprintf(stderr, "Please specify a port number\n");
  exit(1);
}

StringPiece sfpString(const uint8_t* buf, size_t offset, size_t len) {
  const uint8_t* start = buf + offset;
  while (len > 0 && start[len - 1] == ' ') {
    --len;
  }
  return StringPiece(reinterpret_cast<const char*>(start), len);
}

void printThresholds(
    const std::string& name,
    const uint8_t* data,
    std::function<double(uint16_t)> conversionCb) {
  std::array<std::array<uint8_t, 2>, 4> byteOffset{{
      {0, 1},
      {2, 3},
      {4, 5},
      {6, 7},
  }};

  printf("\n");
  const std::array<std::string, 4> thresholds{
      "High Alarm", "Low Alarm", "High Warning", "Low Warning"};

  for (auto row = 0; row < 4; row++) {
    uint16_t u16 = 0;
    for (auto col = 0; col < 2; col++) {
      u16 = (u16 << 8 | data[byteOffset[row][col]]);
    }
    printf(
        "%10s %12s %f\n",
        name.c_str(),
        thresholds[row].c_str(),
        conversionCb(u16));
  }
}

void printChannelMonitor(unsigned int index,
                         const uint8_t* buf,
                         unsigned int rxMSB,
                         unsigned int rxLSB,
                         unsigned int txBiasMSB,
                         unsigned int txBiasLSB,
                         unsigned int txPowerMSB,
                         unsigned int txPowerLSB,
                         std::optional<double> rxSNR = std::nullopt) {
  uint16_t rxValue = (buf[rxMSB] << 8) | buf[rxLSB];
  uint16_t txPowerValue = (buf[txPowerMSB] << 8) | buf[txPowerLSB];
  uint16_t txBiasValue = (buf[txBiasMSB] << 8) | buf[txBiasLSB];

  // RX power ranges from 0mW to 6.5535mW
  double rxPower = 0.0001 * rxValue;

  // TX power ranges from 0mW to 6.5535mW
  double txPower = 0.0001 * txPowerValue;

  // TX bias ranges from 0mA to 131mA
  double txBias = (131.0 * txBiasValue) / 65535;

  if (rxSNR) {
    printf("    Channel %d:   %12fmW  %12fmW  %12fmA  %12f\n",
           index, rxPower, txPower, txBias, rxSNR.value());
  } else {
    printf("    Channel %d:   %12fmW  %12fmW  %12fmA  %12s\n",
           index, rxPower, txPower, txBias, "N/A");
  }
}

void printSffDetail(const DOMDataUnion& domDataUnion, unsigned int port) {
  Sff8636Data sffData = domDataUnion.get_sff8636();
  auto lowerBuf = sffData.lower_ref()->data();
  auto page0Buf = sffData.page0_ref()->data();

  printf("Port %d\n", port);
  printf("  ID: %#04x\n", lowerBuf[0]);
  printf("  Status: 0x%02x 0x%02x\n", lowerBuf[1], lowerBuf[2]);
  printf("  Module State: 0x%02x\n", lowerBuf[3]);

  printf("  Interrupt Flags:\n");
  printf("    LOS: 0x%02x\n", lowerBuf[3]);
  printf("    Fault: 0x%02x\n", lowerBuf[4]);
  printf("    LOL: 0x%02x\n", lowerBuf[5]);
  printf("    Temp: 0x%02x\n", lowerBuf[6]);
  printf("    Vcc: 0x%02x\n", lowerBuf[7]);
  printf("    Rx Power: 0x%02x 0x%02x\n", lowerBuf[9], lowerBuf[10]);
  printf("    Tx Power: 0x%02x 0x%02x\n", lowerBuf[13], lowerBuf[14]);
  printf("    Tx Bias: 0x%02x 0x%02x\n", lowerBuf[11], lowerBuf[12]);
  printf("    Reserved Set 4: 0x%02x 0x%02x\n",
          lowerBuf[15], lowerBuf[16]);
  printf("    Reserved Set 5: 0x%02x 0x%02x\n",
          lowerBuf[17], lowerBuf[18]);
  printf("    Vendor Defined: 0x%02x 0x%02x 0x%02x\n",
         lowerBuf[19], lowerBuf[20], lowerBuf[21]);

  auto temp = static_cast<int8_t>
              (lowerBuf[22]) + (lowerBuf[23] / 256.0);
  printf("  Temperature: %f C\n", temp);
  uint16_t voltage = (lowerBuf[26] << 8) | lowerBuf[27];
  printf("  Supply Voltage: %f V\n", voltage / 10000.0);

  printf("  Channel Data:  %12s    %12s    %12s    %12s\n",
         "RX Power", "TX Power", "TX Bias", "Rx SNR");
  printChannelMonitor(1, lowerBuf, 34, 35, 42, 43, 50, 51);
  printChannelMonitor(2, lowerBuf, 36, 37, 44, 45, 52, 53);
  printChannelMonitor(3, lowerBuf, 38, 39, 46, 47, 54, 55);
  printChannelMonitor(4, lowerBuf, 40, 41, 48, 49, 56, 57);
  printf("    Power measurement is %s\n",
         (page0Buf[92] & 0x04) ? "supported" : "unsupported");
  printf("    Reported RX Power is %s\n",
         (page0Buf[92] & 0x08) ? "average power" : "OMA");

  printf("  Power set:  0x%02x\tExtended ID:  0x%02x\t"
         "Ethernet Compliance:  0x%02x\n",
         lowerBuf[93], page0Buf[1], page0Buf[3]);
  printf("  TX disable bits: 0x%02x\n", lowerBuf[86]);
  printf("  Rate select is %s\n",
      (page0Buf[93] & 0x0c) ? "supported" : "unsupported");
  printf("  RX rate select bits: 0x%02x\n", lowerBuf[87]);
  printf("  TX rate select bits: 0x%02x\n", lowerBuf[88]);
  printf("  CDR support:  TX: %s\tRX: %s\n",
      (page0Buf[1] & (1 << 3)) ? "supported" : "unsupported",
      (page0Buf[1] & (1 << 2)) ? "supported" : "unsupported");
  printf("  CDR bits: 0x%02x\n", lowerBuf[98]);

  auto vendor = sfpString(page0Buf, 20, 16);
  auto vendorPN = sfpString(page0Buf, 40, 16);
  auto vendorRev = sfpString(page0Buf, 56, 2);
  auto vendorSN = sfpString(page0Buf, 68, 16);
  auto vendorDate = sfpString(page0Buf, 84, 8);

  int gauge = page0Buf[109];
  auto cableGauge = gauge;
  if (gauge == eePromDefault && gauge > maxGauge) {
    // gauge implemented as hexadecimal (why?). Convert to decimal
    cableGauge = (gauge / hexBase) * decimalBase + gauge % hexBase;
  } else {
    cableGauge = 0;
  }

  printf("  Connector: 0x%02x\n", page0Buf[2]);
  printf("  Spec compliance: "
         "0x%02x 0x%02x 0x%02x 0x%02x"
         "0x%02x 0x%02x 0x%02x 0x%02x\n",
         page0Buf[3], page0Buf[4], page0Buf[5], page0Buf[6],
         page0Buf[7], page0Buf[8], page0Buf[9], page0Buf[10]);
  printf("  Encoding: 0x%02x\n", page0Buf[11]);
  printf("  Nominal Bit Rate: %d MBps\n", page0Buf[12] * 100);
  printf("  Ext rate select compliance: 0x%02x\n", page0Buf[13]);
  printf("  Length (SMF): %d km\n", page0Buf[14]);
  printf("  Length (OM3): %d m\n", page0Buf[15] * 2);
  printf("  Length (OM2): %d m\n", page0Buf[16]);
  printf("  Length (OM1): %d m\n", page0Buf[17]);
  printf("  Length (Copper): %d m\n", page0Buf[18]);
  if (page0Buf[108] != eePromDefault) {
    auto fractional = page0Buf[108] * .1;
    auto effective = fractional >= 1 ? fractional : page0Buf[18];
    printf("  Length (Copper dM): %.1f m\n", fractional);
    printf("  Length (Copper effective): %.1f m\n", effective);
  }
  if (cableGauge > 0){
    printf("  DAC Cable Gauge: %d\n", cableGauge);
  }
  printf("  Device Tech: 0x%02x\n", page0Buf[19]);
  printf("  Ext Module: 0x%02x\n", page0Buf[36]);
  printf("  Wavelength tolerance: 0x%02x 0x%02x\n",
         page0Buf[60], page0Buf[61]);
  printf("  Max case temp: %dC\n", page0Buf[62]);
  printf("  CC_BASE: 0x%02x\n", page0Buf[63]);
  printf("  Options: 0x%02x 0x%02x 0x%02x 0x%02x\n",
         page0Buf[64], page0Buf[65],
         page0Buf[66], page0Buf[67]);
  printf("  DOM Type: 0x%02x\n", page0Buf[92]);
  printf("  Enhanced Options: 0x%02x\n", page0Buf[93]);
  printf("  Reserved: 0x%02x\n", page0Buf[94]);
  printf("  CC_EXT: 0x%02x\n", page0Buf[95]);
  printf("  Vendor Specific:\n");
  printf("    %02x %02x %02x %02x %02x %02x %02x %02x"
         "  %02x %02x %02x %02x %02x %02x %02x %02x\n",
         page0Buf[96], page0Buf[97], page0Buf[98], page0Buf[99],
         page0Buf[100], page0Buf[101], page0Buf[102], page0Buf[103],
         page0Buf[104], page0Buf[105], page0Buf[106], page0Buf[107],
         page0Buf[108], page0Buf[109], page0Buf[110], page0Buf[111]);
  printf("    %02x %02x %02x %02x %02x %02x %02x %02x"
         "  %02x %02x %02x %02x %02x %02x %02x %02x\n",
         page0Buf[112], page0Buf[113], page0Buf[114], page0Buf[115],
         page0Buf[116], page0Buf[117], page0Buf[118], page0Buf[119],
         page0Buf[120], page0Buf[121], page0Buf[122], page0Buf[123],
         page0Buf[124], page0Buf[125], page0Buf[126], page0Buf[127]);

  printf("  Vendor: %s\n", vendor.str().c_str());
  printf("  Vendor OUI: %02x:%02x:%02x\n", lowerBuf[165 - 128],
          lowerBuf[166 - 128], lowerBuf[167 - 128]);
  printf("  Vendor PN: %s\n", vendorPN.str().c_str());
  printf("  Vendor Rev: %s\n", vendorRev.str().c_str());
  printf("  Vendor SN: %s\n", vendorSN.str().c_str());
  printf("  Date Code: %s\n", vendorDate.str().c_str());

  // print page3 values
  if (!sffData.page3_ref()) {
    return;
  }

  auto page3Buf = sffData.page3_ref().value_unchecked().data();

  printThresholds("Temp", &page3Buf[0], [](const uint16_t u16_temp) {
    double data;
    data = u16_temp / 256.0;
    if (data > 128) {
      data = data - 256;
    }
    return data;
  });

  printThresholds("Vcc", &page3Buf[16], [](const uint16_t u16_vcc) {
    double data;
    data = u16_vcc / 10000.0;
    return data;
  });

  printThresholds("Rx Power", &page3Buf[48], [](const uint16_t u16_rxpwr) {
    double data;
    data = u16_rxpwr * 0.1 / 1000;
    return data;
  });

  printThresholds("Tx Bias", &page3Buf[56], [](const uint16_t u16_txbias) {
    double data;
    data = u16_txbias * 2.0 / 1000;
    return data;
  });
}

void printCmisDetail(const DOMDataUnion& domDataUnion, unsigned int port) {
  int i = 0; // For the index of lane
  CmisData cmisData = domDataUnion.get_cmis();
  auto lowerBuf = cmisData.lower_ref()->data();
  auto page0Buf = cmisData.page0_ref()->data();
  auto page10Buf = cmisData.page10_ref()->data();
  auto page11Buf = cmisData.page11_ref()->data();
  auto page14Buf = cmisData.page14_ref()->data();

  printf("Port %d\n", port);
  printf("  Module Interface Type: CMIS (200G or above)\n");

  printf("  Module State: %s\n",
      getStateNameString(lowerBuf[3] >> 1, kCmisModuleStateMapping).c_str());

  auto ApSel = page11Buf[78] >> 4;
  auto ApCode = lowerBuf[86 + (ApSel - 1) * 4 + 1];
  printf("  Application Selected: %s\n",
      getStateNameString(ApCode, kCmisAppNameMapping).c_str());
  printf("  Low power: 0x%x\n", (lowerBuf[26] >> 6) & 0x1);
  printf("  Low power forced: 0x%x\n", (lowerBuf[26] >> 4) & 0x1);

  printf("  FW Version: %d.%d\n", lowerBuf[39], lowerBuf[40]);
  printf("  Firmware fault: 0x%x\n", (lowerBuf[8] >> 1) & 0x3);
  auto vendor = sfpString(page0Buf, 1, 16);
  auto vendorPN = sfpString(page0Buf, 20, 16);
  auto vendorRev = sfpString(page0Buf, 36, 2);
  auto vendorSN = sfpString(page0Buf, 38, 16);
  auto vendorDate = sfpString(page0Buf, 54, 8);

  printf("  Vendor: %s\n", vendor.str().c_str());
  printf("  Vendor PN: %s\n", vendorPN.str().c_str());
  printf("  Vendor Rev: %s\n", vendorRev.str().c_str());
  printf("  Vendor SN: %s\n", vendorSN.str().c_str());
  printf("  Date Code: %s\n", vendorDate.str().c_str());

  auto temp = static_cast<int8_t>
              (lowerBuf[14]) + (lowerBuf[15] / 256.0);
  printf("  Temperature: %f C\n", temp);

  printf("  VCC: %f V\n", CmisFieldInfo::getVcc(lowerBuf[16] << 8 | lowerBuf[17]));

  printf("\nPer Lane status: \n");
  printf("Lanes             1        2        3        4        5        6        7        8\n");
  printf("Datapath de-init  ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page10Buf[0]>>i)&1);
  }
  printf("\nTx disable        ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page10Buf[2]>>i)&1);
  }
  printf("\nTx squelch bmap   ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page10Buf[4]>>i)&1);
  }
  printf("\nRx Out disable    ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page10Buf[10]>>i)&1);
  }
  printf("\nRx Sqlch disable  ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page10Buf[11]>>i)&1);
  }
  printf("\nHost lane state   ");
  for (i=0; i<4; i++) {
    printf("%-7s  %-7s  ",
    getStateNameString(page11Buf[i] & 0xf, kCmisLaneStateMapping).c_str(),
    getStateNameString((page11Buf[i]>>4) & 0xf, kCmisLaneStateMapping).c_str());
  }
  printf("\nTx fault          ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[7]>>i)&1);
  }
  printf("\nTx LOS            ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[8]>>i)&1);
  }
  printf("\nTx LOL            ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[9]>>i)&1);
  }
  printf("\nTx PWR alarm Hi   ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[11]>>i)&1);
  }
  printf("\nTx PWR alarm Lo   ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[12]>>i)&1);
  }
  printf("\nTx PWR warn Hi    ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[13]>>i)&1);
  }
  printf("\nTx PWR warn Lo    ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[14]>>i)&1);
  }
  printf("\nRx LOS            ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[19]>>i)&1);
  }
  printf("\nRx LOL            ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[20]>>i)&1);
  }
  printf("\nRx PWR alarm Hi   ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[21]>>i)&1);
  }
  printf("\nRx PWR alarm Lo   ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[22]>>i)&1);
  }
  printf("\nRx PWR warn Hi    ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[23]>>i)&1);
  }
  printf("\nRx PWR warn Lo    ");
  for (i = 0; i < 8; i++) {
    printf("%d        ", (page11Buf[24]>>i)&1);
  }
  printf("\nTX Power (mW)     ");
  for (i = 0; i < 8; i++) {
    printf("%.3f    ", ((page11Buf[26+i*2]<<8 | page11Buf[27+i*2]))*0.0001);
  }
  printf("\nRX Power (mW)     ");
  for (i = 0; i < 8; i++) {
    printf("%.3f    ", ((page11Buf[58+i*2]<<8 | page11Buf[59+i*2]))*0.0001);
  }
  printf("\nRx SNR            ");
  for (i = 0; i < 8; i++) {
    printf("%05.04g    ", (CmisFieldInfo::getSnr(page14Buf[113+i*2] << 8 | page14Buf[112+i*2])));
  }
  printf("\n\n");
}

void printPortDetail(const DOMDataUnion& domDataUnion, unsigned int port) {
  if (domDataUnion.__EMPTY__) {
    fprintf(stderr, "DOMDataUnion object is empty\n");
    return;
  }
  if (domDataUnion.getType() == DOMDataUnion::Type::sff8636) {
    printSffDetail(domDataUnion, port);
  } else {
    printCmisDetail(domDataUnion, port);
  }
}

bool isTrident2() {
  std::string contents;
  if (!folly::readFile(chipCheckPath, contents)) {
    if (errno == ENOENT) {
      return false;
    }
    folly::throwSystemError("error reading ", chipCheckPath);
  }
  return (contents == trident2);
}

void tryOpenBus(TransceiverI2CApi* bus) {
  auto expireTime = steady_clock::now() + seconds(FLAGS_open_timeout);
  while (true) {
    try {
      bus->open();
      return;
    } catch (const std::exception& ex) {
      if (steady_clock::now() > expireTime) {
        throw;
      }
    }
    usleep(100);
  }
}

/* This function does a hard reset of the QSFP in a given platform. The reset
 * is done by Fpga or I2C function. This function calls another function which
 * creates and returns TransceiverPlatformApi object. For Fpga controlled
 * platform the called function creates Platform specific TransceiverApi object
 * and returns it. For I2c controlled platform the called function creates
 * TransceiverPlatformI2cApi object and keeps the platform specific I2CBus
 * object raw pointer inside it. The returned object's Qsfp control function
 * is called here to use appropriate Fpga/I2c Api in this function.
 */
bool doQsfpHardReset(
  TransceiverI2CApi *bus,
  unsigned int port) {

  // Call the function to get TransceiverPlatformApi object. For Fpga
  // controlled platform it returns Platform specific TransceiverApi object.
  // For I2c controlled platform it returns TransceiverPlatformI2cApi
  // which contains "bus" as it's internal variable
  auto busAndError = getTransceiverPlatformAPI(bus);

  if (busAndError.second) {
    fprintf(stderr, "Trying to doQsfpHardReset, Couldn't getTransceiverPlatformAPI, error out.\n");
      return false;
  }

  auto qsfpBus = std::move(busAndError.first);

  // This will eventuall call the Fpga or the I2C based platform specific
  // Qsfp reset function
  qsfpBus->triggerQsfpHardReset(port);

  return true;
}

bool doMiniphotonLoopback(TransceiverI2CApi* bus, unsigned int port, LoopbackMode mode) {
  try {
    // Make sure we have page128 selected.
    uint8_t page128 = 128;
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 127, 1, &page128);

    uint8_t loopback = 0;
    if (mode == electricalLoopback) {
      loopback = 0b01010101;
    } else if (mode == opticalLoopback) {
      loopback = 0b10101010;
    }
    fprintf(stderr, "loopback value: %x\n", loopback);
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 245, 1, &loopback);
  } catch (const I2cError& ex) {
    fprintf(stderr, "QSFP %d: fail to set loopback\n", port);
    return false;
  }
  return true;
}

void cmisHostInputLoopback(TransceiverI2CApi* bus, unsigned int port, LoopbackMode mode) {
  try {
    // Make sure we have page 0x13 selected.
    uint8_t page = 0x13;
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 127, sizeof(page), &page);

    uint8_t data = (mode == electricalLoopback) ? 0xff : 0;
    bus->moduleWrite(port, TransceiverI2CApi::ADDR_QSFP, 183, sizeof(data), &data);
  } catch (const I2cError& ex) {
    fprintf(stderr, "QSFP %d: fail to set loopback\n", port);
  }
}

/*
 * cliModulefirmwareUpgrade
 *
 * This function makes thrift call to qsfp_service to do the firmware upgrade for
 * for the optical module. The result (pass/fail) along with message is returned
 * back by thrift call and displayed here.
 */
bool cliModulefirmwareUpgrade(TransceiverI2CApi* bus, unsigned int port, std::string firmwareFilename) {

  // Confirm module type is CMIS
  auto moduleType = getModuleType(bus, port);
  if (moduleType != TransceiverManagementInterface::CMIS) {
    fprintf(stderr, "This command is applicable to CMIS module only\n");
    return false;
  }

  // Get the image header length
  uint32_t imageHdrLen = 0;
  if (FLAGS_image_header_len > 0) {
    imageHdrLen = FLAGS_image_header_len;
  } else {
    // Image header length is not provided by user. Try to get it from known
    // module info
    auto domData = fetchDataFromLocalI2CBus (bus, port);
    CmisData cmisData = domData.get_cmis();
    auto dataUpper = cmisData.page0_ref()->data();

    std::array<uint8_t, 16> modPartNo;
    for (int i=0; i<16; i++) {
      modPartNo[i] = dataUpper[20+i];
    }

    for (int i=0; i<kNumModuleInfo; i++) {
      if (modulePartInfo[i].partNo == modPartNo) {
        imageHdrLen = modulePartInfo[i].headerLen;
        break;
      }
    }
    if (imageHdrLen == 0) {
      printf("Image header length is not specified on command line and");
      printf(" the default image header size is unknown for this module");
      printf("Pl re-run the same command with option --image_header_len <len>");
      return false;
    }
  }

  // Create FbossFirmware object using firmware filename and msa password,
  // header length as properties
  FbossFirmware::FwAttributes firmwareAttr;
  firmwareAttr.filename = firmwareFilename;
  firmwareAttr.properties["msa_password"] = folly::to<std::string>(FLAGS_msa_password);
  firmwareAttr.properties["header_length"] = folly::to<std::string>(imageHdrLen);
  auto fbossFwObj = std::make_unique<FbossFirmware>(firmwareAttr);

  auto fwUpgradeObj = std::make_unique<CmisFirmwareUpgrader>(
    bus, port, std::move(fbossFwObj));

  // Do the standalone upgrade in the same process as wedge_qsfp_util
  bool ret = fwUpgradeObj->cmisModuleFirmwareUpgrade();

  if (ret) {
    printf("Firmware download successful, the module is running desired firmware\n");
    printf("Pl reload the chassis to finish the last step\n");
  } else {
    printf("Firmware upgrade failed, you may retry the same command\n");
  }

  return ret;
}

/*
 * get_module_fw_info
 *
 * This function gets the module firmware info and prints it for a range of
 * ports. The info are : vendor name, part number and current firmware version
 * sample output:
 * Module     Vendor               Part Number          Fw version
 * 52         FINISAR CORP.        FTCC1112E1PLL-FB     2.1
 * 82         INNOLIGHT            T-FX4FNT-HFB         ca.f8
 * 84         FINISAR CORP.        FTCC1112E1PLL-FB     7.8
 */
void get_module_fw_info(TransceiverI2CApi* bus, unsigned int moduleA, unsigned int moduleB) {

  if (moduleA > moduleB) {
    printf("The moduleA should be smaller than or equal to moduleB\n");
    return;
  }

  printf("Displaying firmware info for modules %d-%d\n", moduleA, moduleB);
  printf("Module     Vendor               Part Number          Fw version\n");

  for (unsigned int module = moduleA; module <= moduleB; module++) {

    std::array<uint8_t, 16> vendor;
    std::array<uint8_t, 16> partNo;
    std::array<uint8_t, 2> fwVer;

    if(!bus->isPresent(module)) {
        continue;
      }

    auto moduleType = getModuleType(bus, module);
    if (moduleType != TransceiverManagementInterface::CMIS) {
      continue;
    }

    DOMDataUnion tempDomData = fetchDataFromLocalI2CBus(bus, module);
    CmisData cmisData = tempDomData.get_cmis();
    auto dataLower = cmisData.lower_ref()->data();
    auto dataUpper = cmisData.page0_ref()->data();

    fwVer[0] = dataLower[39];
    fwVer[1] = dataLower[40];

    memcpy(&vendor[0], &dataUpper[1], 16);
    memcpy(&partNo[0], &dataUpper[20], 16);

    printf("%2d         ", module);
    for (int i=0; i<16; i++) {
      printf("%c", vendor[i]);
    }

    printf("     ");
    for (int i=0; i<16; i++) {
      printf("%c", partNo[i]);
    }

    printf("     ");
    printf("%x.%x", fwVer[0], fwVer[1]);
    printf("\n");
  }
}

int main(int argc, char* argv[]) {
  folly::init(&argc, &argv, true);
  gflags::SetCommandLineOptionWithMode(
      "minloglevel", "0", gflags::SET_FLAGS_DEFAULT);
  folly::EventBase evb;

  if (FLAGS_set_100g && FLAGS_set_40g) {
    fprintf(stderr, "Cannot set both 40g and 100g\n");
    return EX_USAGE;
  }
  if (FLAGS_cdr_enable && FLAGS_cdr_disable) {
    fprintf(stderr, "Cannot set and clear the CDR bits\n");
    return EX_USAGE;
  }
  if (FLAGS_clear_low_power && FLAGS_set_low_power) {
    fprintf(stderr, "Cannot set and clear lp mode\n");
    return EX_USAGE;
  }

  if (FLAGS_pause_remediation) {
    try {
      auto client = getQsfpClient(evb);
      client->sync_pauseRemediation(FLAGS_pause_remediation);
      return EX_OK;
    } catch (const std::exception& ex) {
      fprintf(stderr, "error pausing remediation of qsfp_service: %s\n", ex.what());
      return EX_SOFTWARE;
    }
  }

  std::vector<unsigned int> ports;
  bool good = true;
  for (int n = 1; n < argc; ++n) {
    unsigned int portNum;
    try {
      if (argv[n][0] == 'x' && argv[n][1] == 'e') {
        portNum = 1 + folly::to<unsigned int>(argv[n] + 2);
      } else {
        portNum = folly::to<unsigned int>(argv[n]);
      }
      ports.push_back(portNum);
    } catch (const std::exception& ex) {
      fprintf(stderr, "error: invalid port number \"%s\": %s\n",
              argv[n], ex.what());
      good = false;
    }
  }
  if (!good) {
    return EX_USAGE;
  }
  auto busAndError = getTransceiverAPI();
  if (busAndError.second) {
      return busAndError.second;
  }
  auto bus = std::move(busAndError.first);

  bool printInfo = !(FLAGS_clear_low_power || FLAGS_tx_disable ||
                     FLAGS_tx_enable || FLAGS_set_100g || FLAGS_set_40g ||
                     FLAGS_cdr_enable || FLAGS_cdr_disable ||
                     FLAGS_set_low_power || FLAGS_qsfp_hard_reset ||
                     FLAGS_electrical_loopback || FLAGS_optical_loopback ||
                     FLAGS_clear_loopback || FLAGS_read_reg ||
                     FLAGS_write_reg || FLAGS_update_module_firmware ||
                     FLAGS_get_module_fw_info);

  if (FLAGS_direct_i2c || !printInfo) {
    try {
      tryOpenBus(bus.get());
    } catch (const std::exception& ex) {
        fprintf(stderr, "error: unable to open device: %s\n", ex.what());
        return EX_IOERR;
    }
  } else {
    try {
      std::vector<int32_t> idx;
      for(auto port : ports) {
        // Direct I2C bus starts from 1 instead of 0, however qsfp_service index
        // starts from 0. So here we try to comply to match that behavior.
        idx.push_back(port - 1);
      }
      auto domDataUnionMap = fetchDataFromQsfpService(idx, evb);
      for (auto& i : idx) {
        auto iter = domDataUnionMap.find(i);
        if(iter == domDataUnionMap.end()) {
          fprintf(stderr, "Port %d is not present.\n", i + 1);
        }
        else {
          printPortDetail(iter->second, iter->first + 1);
        }
      }
      return EX_OK;
    } catch (const std::exception& e) {
      fprintf(stderr, "Exception talking to qsfp_service: %s\n", e.what());
      return EX_SOFTWARE;
    }
  }

  if (ports.empty()) {
    try {
      printPortSummary(bus.get());
    } catch (const std::exception& ex) {
      fprintf(stderr, "error: %s\n", ex.what());
      return EX_SOFTWARE;
    }
    return EX_OK;
  }

  int retcode = EX_OK;
  for (unsigned int portNum : ports) {
    if (FLAGS_clear_low_power && overrideLowPower(bus.get(), portNum, 0x5)) {
      printf("QSFP %d: cleared low power flags\n", portNum);
    }
    if (FLAGS_set_low_power && overrideLowPower(bus.get(), portNum, 0x3)) {
      printf("QSFP %d: set low power flags\n", portNum);
    }
    if (FLAGS_tx_disable && setTxDisable(bus.get(), portNum, true)) {
      printf("QSFP %d: disabled TX on all channels\n", portNum);
    }
    if (FLAGS_tx_enable && setTxDisable(bus.get(), portNum, false)) {
      printf("QSFP %d: enabled TX on all channels\n", portNum);
    }

    if (FLAGS_set_40g && rateSelect(bus.get(), portNum, 0x0)) {
      printf("QSFP %d: set to optimize for 10G channels\n", portNum);
    }
    if (FLAGS_set_100g && rateSelect(bus.get(), portNum, 0xaa)) {
      printf("QSFP %d: set to optimize for 25G channels\n", portNum);
    }

    if (FLAGS_cdr_enable && setCdr(bus.get(), portNum, 0xff)) {
      printf("QSFP %d: CDR enabled\n", portNum);
    }

    if (FLAGS_cdr_disable && setCdr(bus.get(), portNum, 0x00)) {
      printf("QSFP %d: CDR disabled\n", portNum);
    }

    if (FLAGS_qsfp_hard_reset && doQsfpHardReset(bus.get(), portNum)) {
      printf("QSFP %d: Hard reset done\n", portNum);
    }

    if (FLAGS_electrical_loopback) {
      if (getModuleType(bus.get(), portNum) != TransceiverManagementInterface::CMIS) {
        if (doMiniphotonLoopback(bus.get(), portNum, electricalLoopback)) {
          printf("QSFP %d: done setting module to electrical loopback.\n", portNum);
        }
      } else {
        cmisHostInputLoopback(bus.get(), portNum, electricalLoopback);
      }
    }

    if (FLAGS_optical_loopback &&
        doMiniphotonLoopback(bus.get(), portNum, opticalLoopback)) {
      printf("QSFP %d: done setting module to optical loopback.\n", portNum);
    }

    if (FLAGS_clear_loopback) {
      if (getModuleType(bus.get(), portNum) != TransceiverManagementInterface::CMIS) {
        if (doMiniphotonLoopback(bus.get(), portNum, noLoopback)) {
          printf("QSFP %d: done clear module to loopback.\n", portNum);
        }
      } else {
        cmisHostInputLoopback(bus.get(), portNum, noLoopback);
      }
    }

    if (FLAGS_read_reg) {
      if (FLAGS_offset == -1) {
        fprintf(stderr,
               "QSFP %d: Fail to read register. Specify offset using --offset",
               portNum);
        retcode = EX_SOFTWARE;
      }
      else {
        if (FLAGS_length > 128) {
          fprintf(stderr,
               "QSFP %d: Fail to read register. The --length value should be between 1 to 128",
               portNum);
          retcode = EX_SOFTWARE;
        } else {
          doReadReg(bus.get(), portNum, FLAGS_offset, FLAGS_length);
        }
      }
    }

    if (FLAGS_write_reg) {
      if (FLAGS_offset == -1) {
        fprintf(stderr,
               "QSFP %d: Fail to write register. Specify offset using --offset",
               portNum);
        retcode = EX_SOFTWARE;
      }
      else {
        doWriteReg(bus.get(), portNum, FLAGS_offset, FLAGS_data);
      }
    }

    if (FLAGS_direct_i2c && printInfo) {
      try {
        // Get the port details from the direct i2c read and then print out the
        // i2c info from module
        printPortDetail(fetchDataFromLocalI2CBus(bus.get(), portNum), portNum);
      } catch (const I2cError& ex) {
        // This generally means the QSFP module is not present.
        fprintf(stderr, "Port %d: not present: %s\n", portNum, ex.what());
        retcode = EX_SOFTWARE;
      } catch (const std::exception& ex) {
        fprintf(stderr, "error parsing QSFP data %u: %s\n", portNum, ex.what());
        retcode = EX_SOFTWARE;
      }
    }

    if (FLAGS_update_module_firmware) {
      printf("This action may bring down the port and interrupt the traffic\n");
      if (FLAGS_firmware_filename.empty()) {
        fprintf(stderr,
               "QSFP %d: Fail to upgrade firmware. Specify firmware using --firmware_filename\n",
               portNum);
      } else {
          cliModulefirmwareUpgrade(bus.get(), portNum, FLAGS_firmware_filename);
      }
    }
  }

  if (FLAGS_get_module_fw_info) {
    if (ports.size() < 1) {
      fprintf(stderr, "Pl specify 1 module or 2 modules for the range: <ModuleA> <moduleB>\n");
    } else if (ports.size() == 1) {
      get_module_fw_info(bus.get(), ports[0], ports[0]);
    } else {
      get_module_fw_info(bus.get(), ports[0], ports[1]);
    }
  }

  return retcode;
}
