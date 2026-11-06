#include "dht_reader.h"

#include <Arduino.h>

#include "config.h"

void DhtChannel::begin() {
  if (powerPin_ >= 0) {
    pinMode(powerPin_, OUTPUT);
    digitalWrite(powerPin_, HIGH);
  }
  dht_.begin();
}

frost::Reading DhtChannel::read() {
  frost::Reading r;
  // force = true: a fresh conversion, not the library's 2-s cache
  const float t = dht_.readTemperature(false, true);
  const float h = dht_.readHumidity(false);
  r.ok = !isnan(t) && !isnan(h);
  r.t = t;
  r.rh = h;
  if (r.ok) {
    failStreak_ = 0;
  } else {
    totalFails_++;
    if (++failStreak_ >= DHT_FAILS_BEFORE_POWER_CYCLE) powerCycle();
  }
  return r;
}

void DhtChannel::powerCycle() {
  failStreak_ = 0;
  if (powerPin_ < 0) return;
  powerCycles_++;
  pinMode(dataPin_, INPUT);  // do not feed the sensor through the data line while it is off
  digitalWrite(powerPin_, LOW);
  delay(1000);
  digitalWrite(powerPin_, HIGH);
  dht_.begin();  // the first valid reading comes >= 2 s later, i.e. at the next sample tick
}
