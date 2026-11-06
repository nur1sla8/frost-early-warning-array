// One DHT22 channel: read, count failures, power-cycle the sensor after repeated failures.
#pragma once

#include <DHT.h>
#include <FrostCore.h>

class DhtChannel {
 public:
  DhtChannel(int dataPin, int powerPin) : dht_(dataPin, DHT22), dataPin_(dataPin), powerPin_(powerPin) {}
  void begin();
  frost::Reading read();  // ok = false on timeout / checksum error / NaN
  uint32_t totalFails() const { return totalFails_; }
  uint32_t powerCycles() const { return powerCycles_; }

 private:
  void powerCycle();

  DHT dht_;
  int dataPin_, powerPin_;
  uint8_t failStreak_ = 0;
  uint32_t totalFails_ = 0, powerCycles_ = 0;
};
