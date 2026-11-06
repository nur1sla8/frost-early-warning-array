// Frost Early-Warning Array: sensor node firmware (ESP32-S3-WROOM-1, Arduino-ESP32 core 2.x/3.x).
//
// LIVE mode  : 2x DHT22 -> FrostCore engine -> Serial CSV ("D,..."), LittleFS log, ESP-NOW telemetry.
// REPLAY mode: rows "R,ts,t1,rh1,ok1,t2,rh2,ok2,time_valid" from the PC replace the sensors; the node
//              answers each with "O,..." in the same format as firmware/host_tests/replay_cli, so
//              tools/compare_replay.py checks the device against the Python reference.
// Type HELP in the Serial Monitor (115200 baud, line ending: Newline) for all commands.
//
// Libraries: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"; FrostCore from
// firmware/libraries (set the Arduino IDE sketchbook location to the firmware/ folder).

#include <Arduino.h>
#include <FrostCore.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_random.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "data_log.h"
#include "dht_reader.h"
#include "espnow_link.h"

namespace {

const uint8_t kGatewayMac[6] = GATEWAY_MAC;
const char* const kLogHeader =
    "unix_ts,node_id,boot_id,seq,uptime_s,t1,rh1,ok1,t2,rh2,ok2,t,rh,td,tf,d,t_smooth,rt_1h,rd_1h,rt_3h,"
    "quality,level,level_name,flags,send_fail_streak,calibrated,time_valid";
const uint32_t kCfgVersion = 1;

DhtChannel g_s1(DHT1_DATA_PIN, DHT1_POWER_PIN);
DhtChannel g_s2(DHT2_DATA_PIN, DHT2_POWER_PIN);
frost::Engine g_engine;
Preferences g_prefs;

enum Mode { LIVE, REPLAY };
Mode g_mode = LIVE;
uint8_t g_nodeId = 1;
uint16_t g_bootId = 0;
uint32_t g_seq = 0;
uint32_t g_lastSampleMs = 0, g_lastLogMs = 0;
uint8_t g_lastLoggedLevel = 255;
frost::Reading g_r1, g_r2;
frost::Output g_out;
String g_line;

// ---------- clock ----------
bool clockValid() { return time(nullptr) > 1700000000; }  // after Nov 2023 = has been set

void setClock(int64_t unixTs) {
  timeval tv = {(time_t)unixTs, 0};
  settimeofday(&tv, nullptr);
}

void onBeacon(int64_t unixTs) {
  // keep our clock within 2 s of the gateway
  if (!clockValid() || llabs((long long)(time(nullptr) - unixTs)) > 2) setClock(unixTs);
}

// ---------- config persistence ----------
void loadConfig() {
  g_prefs.begin("frost", true);
  g_nodeId = g_prefs.getUChar("node", 1);
  frost::Config c;  // defaults = initial mode
  c.tzOffsetS = TZ_OFFSET_S;
  if (g_prefs.getUInt("cfgver", 0) == kCfgVersion && g_prefs.getBytesLength("cfg") == sizeof(c)) {
    g_prefs.getBytes("cfg", &c, sizeof(c));  // saved (e.g. calibrated) configuration
  }
  g_prefs.end();
  g_engine.setConfig(c);
}

void saveConfig() {
  const frost::Config c = g_engine.config();
  g_prefs.begin("frost", false);
  g_prefs.putUChar("node", g_nodeId);
  g_prefs.putUInt("cfgver", kCfgVersion);
  g_prefs.putBytes("cfg", &c, sizeof(c));
  g_prefs.end();
}

void printConfig() {
  const frost::Config& c = g_engine.config();
  Serial.printf("# node=%u mode=%s calibrated=%d\n", g_nodeId, g_mode == LIVE ? "LIVE" : "REPLAY", c.calibrated);
  Serial.printf("# watch=%.2f warning=%.2f critical=%.2f frost=%.2f hyst=%.2f smooth=%ld\n", c.tWatch,
                c.tWarning, c.tCritical, c.tFrost, c.hysteresis, (long)c.smoothS);
  Serial.printf("# wstart=%d wend=%d tz=%ld tagree=%.2f rhagree=%.2f\n", c.windowStartH, c.windowEndH,
                (long)c.tzOffsetS, c.tAgree, c.rhAgree);
  Serial.printf("# off_t1=%.2f off_rh1=%.2f off_t2=%.2f off_rh2=%.2f\n", c.offsetT1, c.offsetRh1, c.offsetT2,
                c.offsetRh2);
}

bool setKey(const String& key, float v) {
  frost::Config c = g_engine.config();
  if (key == "watch") c.tWatch = v;
  else if (key == "warning") c.tWarning = v;
  else if (key == "critical") c.tCritical = v;
  else if (key == "frost") c.tFrost = v;
  else if (key == "hyst") c.hysteresis = v;
  else if (key == "smooth") c.smoothS = (int32_t)v;
  else if (key == "wstart") c.windowStartH = (int8_t)v;
  else if (key == "wend") c.windowEndH = (int8_t)v;
  else if (key == "tz") c.tzOffsetS = (int32_t)v;
  else if (key == "tagree") c.tAgree = v;
  else if (key == "rhagree") c.rhAgree = v;
  else if (key == "off_t1") c.offsetT1 = v;
  else if (key == "off_rh1") c.offsetRh1 = v;
  else if (key == "off_t2") c.offsetT2 = v;
  else if (key == "off_rh2") c.offsetRh2 = v;
  else if (key == "calibrated") c.calibrated = v != 0;
  else if (key == "node") { g_nodeId = (uint8_t)v; return true; }
  else return false;
  if (!(c.tFrost < c.tCritical && c.tCritical <= c.tWarning && c.tWarning <= c.tWatch)) {
    Serial.println("# ERR thresholds must satisfy frost < critical <= warning <= watch");
    return false;
  }
  g_engine.setConfig(c);
  return true;
}

// ---------- output ----------
void formatRow(char* buf, size_t n) {
  const frost::Output& o = g_out;
  snprintf(buf, n,
           "%lld,%u,%u,%lu,%lu,%.1f,%.1f,%d,%.1f,%.1f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%u,%u,%s,%u,%u,%d,%d",
           (long long)o.ts, g_nodeId, g_bootId, (unsigned long)g_seq, (unsigned long)(millis() / 1000), g_r1.t,
           g_r1.rh, g_r1.ok, g_r2.t, g_r2.rh, g_r2.ok, o.t, o.rh, o.td, o.tf, o.d, o.tSmooth, o.rt1h, o.rd1h,
           o.rt3h, o.quality, o.level, frost::levelName(o.level), o.flags, linkFailStreak(),
           g_engine.config().calibrated, clockValid());
}

void sendTelemetry() {
  frost::TelemetryPacket p;
  frost::fillPacket(p, g_nodeId, g_bootId, g_seq, millis() / 1000, clockValid() ? (int64_t)time(nullptr) : 0,
                    g_r1, g_r2, g_out, g_engine.config().calibrated, linkFailStreak());
  frost::sealPacket(p);
  linkSend(&p, sizeof(p));
}

// ---------- one engine step from live sensors ----------
void liveTick() {
  g_r1 = g_s1.read();
  g_r2 = g_s2.read();
  const bool tv = clockValid();
  // Without a clock the engine still runs on uptime; the level is then evaluated at any hour.
  const int64_t ts = tv ? (int64_t)time(nullptr) : (int64_t)(millis() / 1000);
  g_out = g_engine.update(ts, g_r1, g_r2, tv);
  g_seq++;

  static char row[320];
  formatRow(row, sizeof(row));
  Serial.print("D,");
  Serial.println(row);

  const uint32_t now = millis();
  if (g_out.level != g_lastLoggedLevel || now - g_lastLogMs >= LOG_PERIOD_S * 1000UL) {
    logAppend(row);
    g_lastLogMs = now;
    g_lastLoggedLevel = g_out.level;
  }
  sendTelemetry();  // every tick: the gateway counts lost frames from gaps in seq
}

// ---------- replay ----------
void replayRow(const String& line) {
  long long ts;
  double t1, rh1, t2, rh2;
  int ok1, ok2, tv;
  if (sscanf(line.c_str(), "R,%lld,%lf,%lf,%d,%lf,%lf,%d,%d", &ts, &t1, &rh1, &ok1, &t2, &rh2, &ok2, &tv) != 8) {
    Serial.println("# ERR bad replay row");
    return;
  }
  g_r1.t = t1; g_r1.rh = rh1; g_r1.ok = ok1 != 0;
  g_r2.t = t2; g_r2.rh = rh2; g_r2.ok = ok2 != 0;
  g_out = g_engine.update(ts, g_r1, g_r2, tv != 0);
  g_seq++;
  const frost::Output& o = g_out;
  Serial.printf("O,%lld,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u\n", (long long)o.ts, o.t, o.rh, o.td,
                o.d, o.tSmooth, o.rt1h, o.rd1h, o.rt3h, o.quality, o.level, o.flags);
}

// ---------- commands ----------
void printStatus() {
  char mac[24];
  linkMacString(mac, sizeof(mac));
  Serial.printf("# mac=%s boot=%u seq=%lu uptime=%lus clock=%s\n", mac, g_bootId, (unsigned long)g_seq,
                (unsigned long)(millis() / 1000), clockValid() ? "set" : "NOT SET");
  Serial.printf("# dht1 fails=%lu powercycles=%lu  dht2 fails=%lu powercycles=%lu\n",
                (unsigned long)g_s1.totalFails(), (unsigned long)g_s1.powerCycles(),
                (unsigned long)g_s2.totalFails(), (unsigned long)g_s2.powerCycles());
  Serial.printf("# espnow sent=%lu failed=%lu fail_streak=%u\n", (unsigned long)linkSentTotal(),
                (unsigned long)linkFailedTotal(), linkFailStreak());
  Serial.printf("# level=%s quality=%u flags=0x%04X\n", frost::levelName(g_out.level), g_out.quality, g_out.flags);
  logInfo(Serial);
}

void handleCommand(String line) {
  line.trim();
  if (line.isEmpty()) return;
  if (line.startsWith("R,")) {
    if (g_mode == REPLAY) replayRow(line);
    else Serial.println("# ERR not in REPLAY mode");
    return;
  }
  String cmd = line, arg;
  const int sp = line.indexOf(' ');
  if (sp > 0) { cmd = line.substring(0, sp); arg = line.substring(sp + 1); arg.trim(); }
  cmd.toUpperCase();

  if (cmd == "HELP") {
    Serial.println("# STATUS | SHOW | SET key=value | SAVE | DEFAULTS | TIME <unix_utc> | MODE LIVE|REPLAY");
    Serial.println("# DUMP | CLEARLOG | R,ts,t1,rh1,ok1,t2,rh2,ok2,time_valid (REPLAY mode)");
    Serial.println("# keys: watch warning critical frost hyst smooth wstart wend tz tagree rhagree");
    Serial.println("#       off_t1 off_rh1 off_t2 off_rh2 calibrated node");
  } else if (cmd == "STATUS") {
    printStatus();
  } else if (cmd == "SHOW") {
    printConfig();
  } else if (cmd == "SET") {
    const int eq = arg.indexOf('=');
    if (eq <= 0 || !setKey(arg.substring(0, eq), arg.substring(eq + 1).toFloat())) Serial.println("# ERR SET");
    else printConfig();
  } else if (cmd == "SAVE") {
    saveConfig();
    Serial.println("# saved");
  } else if (cmd == "DEFAULTS") {
    frost::Config c;
    c.tzOffsetS = TZ_OFFSET_S;
    g_engine.setConfig(c);
    printConfig();
  } else if (cmd == "TIME") {
    setClock(atoll(arg.c_str()));
    Serial.printf("# clock set to %lld\n", (long long)time(nullptr));
  } else if (cmd == "MODE") {
    arg.toUpperCase();
    g_mode = arg == "REPLAY" ? REPLAY : LIVE;
    g_engine.reset();  // replay and live histories must not mix
    Serial.printf("# mode=%s\n", g_mode == LIVE ? "LIVE" : "REPLAY");
  } else if (cmd == "DUMP") {
    Serial.println("# BEGIN LOG");
    logDump(Serial);
    Serial.println("# END LOG");
  } else if (cmd == "CLEARLOG") {
    logClear();
    Serial.println("# log cleared");
  } else {
    Serial.println("# ERR unknown command, type HELP");
  }
}

void pollSerial() {
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\n') {
      handleCommand(g_line);
      g_line = "";
    } else if (ch != '\r' && g_line.length() < 200) {
      g_line += ch;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  g_bootId = (uint16_t)(esp_random() & 0xFFFF);
  loadConfig();
  g_s1.begin();
  g_s2.begin();

  if (strlen(WIFI_SSID) > 0) {  // optional NTP; ESP-NOW then follows the router's channel
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      configTime(0, 0, "pool.ntp.org");
      for (int i = 0; i < 20 && !clockValid(); i++) delay(250);
    }
  }
  const uint8_t ch = WiFi.status() == WL_CONNECTED ? WiFi.channel() : ESPNOW_CHANNEL;
  const bool linkOk = linkBegin(ch, kGatewayMac, onBeacon);
  const bool logOk = logBegin(kLogHeader);

  Serial.printf("# frost_node boot=%u node=%u espnow=%s ch=%u log=%s clock=%s\n", g_bootId, g_nodeId,
                linkOk ? "ok" : "FAIL", ch, logOk ? "ok" : "FAIL", clockValid() ? "set" : "NOT SET");
  Serial.print("# D,");
  Serial.println(kLogHeader);
  printConfig();
  enableLoopWDT();  // reboot if loop() hangs > ~5 s
  g_lastSampleMs = millis() - SAMPLE_PERIOD_S * 1000UL + 3000;  // first sample after DHT warm-up
}

void loop() {
  feedLoopWDT();
  pollSerial();
  if (g_mode == LIVE && millis() - g_lastSampleMs >= SAMPLE_PERIOD_S * 1000UL) {
    g_lastSampleMs = millis();
    liveTick();
  }
  delay(5);
}
