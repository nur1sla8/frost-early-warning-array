#include "data_log.h"

#include <LittleFS.h>

#include "config.h"

namespace {
const char* const kStateFile = "/log_active";  // holds "A" or "B", survives reboots
const char* g_header = "";
bool g_ok = false;
bool g_activeA = true;

const char* activePath() { return g_activeA ? LOG_FILE_A : LOG_FILE_B; }
const char* olderPath() { return g_activeA ? LOG_FILE_B : LOG_FILE_A; }

void saveState() {
  File s = LittleFS.open(kStateFile, "w");
  s.print(g_activeA ? "A" : "B");
  s.close();
}

void startFile(const char* path) {
  File f = LittleFS.open(path, "w");  // truncates
  f.println(g_header);
  f.close();
}

size_t fileSize(const char* path) {
  File f = LittleFS.open(path, "r");
  const size_t s = f ? f.size() : 0;
  f.close();
  return s;
}
}  // namespace

bool logBegin(const char* header) {
  g_header = header;
  g_ok = LittleFS.begin(true);  // formats on first use
  if (!g_ok) return false;
  if (LittleFS.exists(kStateFile)) {
    File s = LittleFS.open(kStateFile, "r");
    g_activeA = s.read() != 'B';
    s.close();
  }
  if (!LittleFS.exists(activePath())) startFile(activePath());
  return true;
}

bool logAppend(const char* line) {
  if (!g_ok) return false;
  if (fileSize(activePath()) >= LOG_FILE_MAX_BYTES) {
    g_activeA = !g_activeA;      // the older file is overwritten
    startFile(activePath());
    saveState();
  }
  File f = LittleFS.open(activePath(), "a");
  if (!f) return false;
  const bool ok = f.println(line) > 0;
  f.close();  // close = flush to flash, so a power cut loses at most the current line
  return ok;
}

void logDump(Stream& out) {
  if (!g_ok) return;
  for (const char* p : {olderPath(), activePath()}) {
    if (!LittleFS.exists(p)) continue;
    File f = LittleFS.open(p, "r");
    while (f.available()) out.write(f.read());
    f.close();
  }
}

void logClear() {
  if (!g_ok) return;
  LittleFS.remove(LOG_FILE_A);
  LittleFS.remove(LOG_FILE_B);
  g_activeA = true;
  startFile(LOG_FILE_A);
  saveState();
}

void logInfo(Stream& out) {
  if (!g_ok) {
    out.println("# log: LittleFS not mounted (check the partition scheme)");
    return;
  }
  out.printf("# log: used %u of %u bytes, active %s (%u bytes)\n", (unsigned)LittleFS.usedBytes(),
             (unsigned)LittleFS.totalBytes(), activePath(), (unsigned)fileSize(activePath()));
}
