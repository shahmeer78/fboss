/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/MKAServiceManager.h"
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "gmock/gmock.h"

using ::testing::_;
using ::testing::AtLeast;
using namespace facebook::fboss;

namespace {
#if FOLLY_HAS_COROUTINES
class PacketAcceptor
    : public facebook::fboss::AsyncPacketTransport::ReadCallback {
 public:
  explicit PacketAcceptor(std::shared_ptr<folly::Baton<>> baton)
      : baton_(baton) {
    if (baton_) {
      baton_->reset();
    }
  }

  void onReadError(
      const folly::AsyncSocketException& /*ex*/) noexcept override {
    baton_->post();
  }

  void onReadClosed() noexcept override {}

  void onDataAvailable(std::unique_ptr<folly::IOBuf> data) noexcept override {
    rxIOBufs_.emplace_back(std::move(data));
    baton_->post();
  }

  std::vector<std::unique_ptr<folly::IOBuf>> rxIOBufs_;
  std::shared_ptr<folly::Baton<>> baton_;
};

class MKAServiceManagerTest : public testing::Test {
 public:
  const MacAddress testLocalMac = MacAddress("00:00:00:00:00:02");
  static constexpr std::array<uint8_t, 6> DSTMAC =
      {0x01, 0x80, 0xc2, 0x00, 0x00, 0x03};
  std::unique_ptr<HwTestHandle> setupTestHandle(bool enableMacsec = true) {
    auto switchFlags =
        enableMacsec ? SwitchFlags::ENABLE_MACSEC : SwitchFlags::DEFAULT;
    auto state = testStateAWithPortsUp();
    return createTestHandle(state, testLocalMac, switchFlags);
  }
  std::unique_ptr<folly::IOBuf> createEapol() {
    auto iobuf = folly::IOBuf::create(64);
    iobuf->append(64);
    folly::io::RWPrivateCursor cursor(iobuf.get());
    cursor.push(DSTMAC.data(), folly::MacAddress::SIZE);
    cursor.push(testLocalMac.bytes(), folly::MacAddress::SIZE);
    cursor.writeBE<uint16_t>(MKAServiceManager::ETHERTYPE_EAPOL);
    return iobuf;
  }

  TPacket createPacket(PortID activePort) {
    TPacket pkt;
    pkt.l2Port_ref() = folly::to<std::string>(activePort);
    pkt.buf_ref() = createEapol()->moveToFbString().toStdString();
    return pkt;
  }

  void init(bool enableMacsec = true) {
    handle_ = setupTestHandle(enableMacsec);
    sw_ = handle_->getSw();
    for (const auto& port : *(sw_->getState()->getPorts())) {
      activePort_ = port->getID();
      break;
    }
    if (enableMacsec) {
      MKAServiceManager* manager = sw_->getMKAServiceMgr();
      mkaServerStream_->connectClient(manager->getServerPort());
      baton_->reset();
      EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(200)));
      EXPECT_TRUE(mkaServerStream_->isConnectedToServer());
      EXPECT_TRUE(manager->isConnectedToMkaServer());
      mkaPktTransport_ =
          mkaServerStream_->listen(folly::to<std::string>(activePort_));
      mkaPktTransport_->setReadCallback(recvAcceptor_.get());
    }
  }
  void SetUp() override {
    mkaClientThread_ = std::make_unique<folly::ScopedEventBaseThread>();
    mkaTimerThread_ = std::make_unique<folly::ScopedEventBaseThread>();
    baton_ = std::make_shared<folly::Baton<>>();
    mkaServerStream_ = std::make_shared<BidirectionalPacketStream>(
        "mka_server",
        mkaClientThread_->getEventBase(),
        mkaTimerThread_->getEventBase(),
        10);
    mkaServer_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
        mkaServerStream_);
    FLAGS_fboss_mka_port = 0;
    FLAGS_mka_service_port = mkaServer_->getPort();
    FLAGS_mka_reconnect_timer = 10;
    recvAcceptor_ = std::make_unique<PacketAcceptor>(baton_);
  }

  void TearDown() override {
    mkaServerStream_->stopClient();
    mkaTimerThread_->getEventBase()->terminateLoopSoon();
    mkaClientThread_->getEventBase()->terminateLoopSoon();
    baton_->reset();
    EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(100)));
  }

  SwSwitch* sw_{nullptr};
  std::unique_ptr<HwTestHandle> handle_;
  PortID activePort_;
  std::unique_ptr<folly::ScopedEventBaseThread> mkaTimerThread_;
  std::unique_ptr<folly::ScopedEventBaseThread> mkaClientThread_;
  std::shared_ptr<BidirectionalPacketStream> mkaServerStream_;
  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> mkaServer_;
  std::shared_ptr<folly::Baton<>> baton_;
  std::shared_ptr<AsyncPacketTransport> mkaPktTransport_;
  std::unique_ptr<PacketAcceptor> recvAcceptor_;
};

TEST_F(MKAServiceManagerTest, SendTest) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(AtLeast(1));
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_NE(manager, nullptr);
  manager->recvPacket(createPacket(activePort_));
}

TEST_F(MKAServiceManagerTest, EmptyMgr) {
  init(false);
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_EQ(manager, nullptr);
}

TEST_F(MKAServiceManagerTest, SendTestLocalMgr) {
  init(false);
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(AtLeast(1));
  EXPECT_EQ(sw_->getMKAServiceMgr(), nullptr);
  MKAServiceManager manager(sw_);
  manager.recvPacket(createPacket(activePort_));
}

TEST_F(MKAServiceManagerTest, SendInvalidPort) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(0);
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_NE(manager, nullptr);
  manager->recvPacket(createPacket(PortID(9999)));
}

TEST_F(MKAServiceManagerTest, SendInvalidPacket) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(0);
  MKAServiceManager* manager = sw_->getMKAServiceMgr();
  EXPECT_NE(manager, nullptr);
  manager->recvPacket(TPacket());
  TPacket pkt;
  pkt.l2Port_ref() = "test";
  pkt.buf_ref() = "test";
  manager->recvPacket(std::move(pkt));
}

TEST_F(MKAServiceManagerTest, RecvPktFromMkaServer) {
  init();
  EXPECT_HW_CALL(sw_, sendPacketOutOfPortAsync_(_, _, _)).Times(AtLeast(1));
  baton_->reset();
  TPacket pkt = createPacket(activePort_);
  mkaServerStream_->send(std::move(pkt));
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(200)));
}

TEST_F(MKAServiceManagerTest, SendPktToMkaServer) {
  init();
  baton_->reset();
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(50)));
  baton_->reset();
  auto iobuf = createEapol();
  std::unique_ptr<MockRxPacket> rxPkt =
      std::make_unique<MockRxPacket>(std::move(iobuf));
  rxPkt->setSrcPort(activePort_);
  EXPECT_NO_THROW(sw_->packetReceivedThrowExceptionOnError(std::move(rxPkt)));
  EXPECT_TRUE(baton_->try_wait_for(std::chrono::milliseconds(200)));
}

TEST_F(MKAServiceManagerTest, SendPktToMkaServerUnregisteredPort) {
  init(false);
  baton_->reset();
  auto iobuf = createEapol();
  std::unique_ptr<MockRxPacket> rxPkt =
      std::make_unique<MockRxPacket>(std::move(iobuf));
  rxPkt->setSrcPort(activePort_);
  EXPECT_NO_THROW(sw_->packetReceivedThrowExceptionOnError(std::move(rxPkt)));
  EXPECT_FALSE(baton_->try_wait_for(std::chrono::milliseconds(200)));
}
#endif
} // unnamed namespace
