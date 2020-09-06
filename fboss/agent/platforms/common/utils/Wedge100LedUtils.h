// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/types.h"

#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss {

class Wedge100LedUtils {
 public:
  enum class LedColor : uint32_t {
    OFF = 0b000,
    BLUE = 0b001,
    GREEN = 0b010,
    CYAN = 0b011,
    RED = 0b100,
    MAGENTA = 0b101,
    YELLOW = 0b110,
    WHITE = 0b111,
  };

  int getPortIndex(std::optional<ChannelID> channel);
  LedColor getLEDColor(bool up, bool adminUp);
  LedColor getLEDColor(PortLedExternalState externalState);
};

} // namespace facebook::fboss
