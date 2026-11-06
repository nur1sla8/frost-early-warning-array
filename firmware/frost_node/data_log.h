// CSV log on LittleFS with two-file rotation: when the active file is full, the older one is
// truncated and becomes active. History survives reboots and power loss (lines are flushed).
#pragma once

#include <Arduino.h>

bool logBegin(const char* header);
bool logAppend(const char* line);
void logDump(Stream& out);   // older file first, then the active one
void logClear();
void logInfo(Stream& out);
