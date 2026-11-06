// ESP-NOW link of a sensor node: star topology, node -> gateway. Not a mesh: there is no routing;
// every node must be in radio range of the gateway.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef void (*BeaconHandler)(int64_t unixTs);

bool linkBegin(uint8_t channel, const uint8_t gatewayMac[6], BeaconHandler onBeacon);
// Queues one frame. Delivery status arrives asynchronously; see linkFailStreak().
bool linkSend(const void* data, size_t len);
// Consecutive frames without a link-layer ACK (unicast only; broadcast always reports success).
uint8_t linkFailStreak();
uint32_t linkSentTotal();
uint32_t linkFailedTotal();
void linkMacString(char* out, size_t n);
