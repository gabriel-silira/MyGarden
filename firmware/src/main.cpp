#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <sys/time.h>
#include <time.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "config.h"
#include <GardenSafety.h>

// A tarefa de seguranca controla o prazo da bomba independentemente de HTTP.
// Nunca manter este mutex durante rede, Serial, JSON ou escrita na flash.
SemaphoreHandle_t stateMutex;
garden::SafetyController safety(config::kHardwareEnabled, config::kAutomaticAllowed);
WebServer server(80);
Preferences preferences;
bool storageReady = false;
bool serverStarted = false;
char deviceId[40] = {};
char bootId[33] = {};
char hostName[32] = {};

struct Measurement {
  garden::SensorSample sample{};
  uint16_t raw = 0;
  uint16_t rawSpread = 0;
  uint32_t milliVolts = 0;
  int64_t unixMs = 0;
};
Measurement measurement;
garden::Calibration calibration{};

// Um unico blob NVS evita combinar dry de uma gravacao com wet de outra.
struct StoredCalibration { uint32_t version; int32_t dryRaw; int32_t wetRaw; };
struct EventClock { uint32_t sequence = 0; int64_t startedUnixMs = 0; };
EventClock eventClocks[8];
size_t nextEventClock = 0;

struct Snapshot {
  Measurement measurement;
  garden::Calibration calibration;
  bool running = false;
  bool armed = false;
  uint32_t lastSequence = 0;
  uint32_t cooldownMs = 0;
  uint32_t budgetUsedMs = 0;
  uint32_t armRemainingMs = 0;
  bool hasEvent = false;
  garden::Event event{};
  int64_t eventStartedUnixMs = 0;
};

void lockState() { xSemaphoreTake(stateMutex, portMAX_DELAY); }
void unlockState() { xSemaphoreGive(stateMutex); }

int64_t wallMillis() {
  timeval now{};
  gettimeofday(&now, nullptr);
  // Sem SNTP, nao fabricar uma data a partir de millis().
  if (now.tv_sec < 1704067200LL) return 0;
  return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
}

String isoTimestamp(int64_t unixMs) {
  const time_t seconds = static_cast<time_t>(unixMs / 1000);
  tm utc{};
  gmtime_r(&seconds, &utc);
  char base[24];
  strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &utc);
  char output[32];
  snprintf(output, sizeof(output), "%s.%03uZ", base, static_cast<unsigned>(unixMs % 1000));
  return String(output);
}

void setTimestamp(JsonObject object, const char* key, int64_t unixMs) {
  if (unixMs > 0) object[key] = isoTimestamp(unixMs);
  else object[key] = nullptr;
}

void applyRelayLocked() {
  if (config::kPolarityConfirmed) {
    digitalWrite(config::kRelayPin, safety.running() ? config::kRelayActiveLevel
                                                    : 1 - config::kRelayActiveLevel);
  }
}

Snapshot snapshot(uint32_t sequence = 0) {
  Snapshot result;
  lockState();
  const uint32_t now = millis();
  result.measurement = measurement;
  result.calibration = calibration;
  result.running = safety.running();
  result.armed = safety.armed();
  result.lastSequence = safety.lastAcceptedSequence();
  result.cooldownMs = safety.cooldownRemainingMs(now);
  result.budgetUsedMs = safety.budgetUsedMs(now);
  result.armRemainingMs = safety.armRemainingMs(now);
  const garden::Event* event = sequence ? safety.findEvent(sequence) : safety.currentEvent();
  if (event) {
    result.event = *event;
    result.hasEvent = true;
    for (const auto& clock : eventClocks) {
      if (clock.sequence == event->request.sequence) result.eventStartedUnixMs = clock.startedUnixMs;
    }
  }
  unlockState();
  return result;
}

Measurement sampleSoil() {
  Measurement result;
  uint16_t samples[16];
  for (auto& value : samples) {
    value = analogRead(config::kSoilPin);
    delayMicroseconds(200);
  }
  std::sort(samples, samples + 16);
  uint32_t sum = 0;
  for (size_t i = 4; i < 12; ++i) sum += samples[i];
  result.raw = sum / 8;
  result.rawSpread = samples[15] - samples[0];
  result.milliVolts = analogReadMilliVolts(config::kSoilPin);
  result.sample.sampledAtMs = millis();
  result.unixMs = wallMillis();
  result.sample.valid = samples[0] > 10 && samples[15] < 4085 && result.rawSpread <= 400;
  return result;
}

void safetyTask(void*) {
  uint32_t lastSample = millis() - config::kSampleIntervalMs;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    const bool readNow = static_cast<uint32_t>(now - lastSample) >= config::kSampleIntervalMs;
    Measurement next;
    if (readNow) next = sampleSoil();
    const bool connected = WiFi.status() == WL_CONNECTED;
    lockState();
    if (readNow) {
      next.sample.calibrated = garden::calibrationValid(calibration);
      if (next.sample.calibrated) {
        garden::calibratedMoisture(calibration, next.raw, next.sample.moisture);
      }
      measurement = next;
      safety.setSample(next.sample);
      lastSample = next.sample.sampledAtMs;
    }
    safety.tick(millis(), connected);
    applyRelayLocked();
    unlockState();
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(config::kSafetyPeriodMs));
  }
}

bool originAllowed() {
  const String origin = server.header("Origin");
  return origin.isEmpty() ||
         (strlen(MYGARDEN_WEB_ORIGIN) > 0 && origin == MYGARDEN_WEB_ORIGIN);
}

void responseHeaders() {
  server.sendHeader("Cache-Control", "no-store");
  if (!server.header("Origin").isEmpty() && originAllowed()) {
    server.sendHeader("Access-Control-Allow-Origin", MYGARDEN_WEB_ORIGIN);
    server.sendHeader("Vary", "Origin");
  }
}

void sendJson(int status, const JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  responseHeaders();
  server.send(status, "application/json; charset=utf-8", body);
}

void fail(int status, const char* code) {
  JsonDocument doc;
  doc["error"]["code"] = code;
  doc["bootId"] = bootId;
  sendJson(status, doc);
}

bool authorized() {
  if (!originAllowed()) { fail(403, "origin_not_allowed"); return false; }
  if (!config::kCredentialsReady) { fail(503, "device_not_configured"); return false; }
  const String header = server.header("Authorization");
  const String expected = String("Bearer ") + MYGARDEN_API_TOKEN;
  unsigned difference = header.length() ^ expected.length();
  for (unsigned i = 0; i < expected.length(); ++i) {
    difference |= static_cast<unsigned>((i < header.length() ? header[i] : 0) ^ expected[i]);
  }
  if (difference) { fail(401, "unauthorized"); return false; }
  return true;
}

bool readBody(JsonDocument& doc) {
  if (!authorized()) return false;
  String type = server.header("Content-Type");
  const int separator = type.indexOf(';');
  if (separator >= 0) type = type.substring(0, separator);
  type.trim();
  if (type != "application/json") { fail(415, "json_required"); return false; }
  // WebServer mistura argumentos da URL com o corpo, chamado "plain".
  // Escritas nao aceitam query: impedir que ?plain=... substitua o JSON real.
  if (server.clientContentLength() <= 0 || server.args() != 1 ||
      server.argName(0) != "plain") {
    fail(400, "invalid_body"); return false;
  }
  const String body = server.arg("plain");
  if (body.length() > config::kMaxBodyBytes) { fail(413, "body_too_large"); return false; }
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) {
    fail(400, "invalid_json"); return false;
  }
  if (!doc["bootId"].is<const char*>() || strcmp(doc["bootId"], bootId) != 0) {
    fail(409, "boot_changed"); return false;
  }
  if (!doc["plantId"].is<const char*>() || strcmp(doc["plantId"], MYGARDEN_PLANT_ID) != 0) {
    fail(409, "plant_mismatch"); return false;
  }
  return true;
}

void addIdentity(JsonDocument& doc) {
  doc["apiVersion"] = 1;
  doc["firmwareVersion"] = config::kVersion;
  doc["deviceId"] = deviceId;
  doc["bootId"] = bootId;
  doc["plantId"] = MYGARDEN_PLANT_ID;
  doc["source"] = "device";
  doc["uptimeMs"] = millis();
  doc["clockSynchronized"] = wallMillis() > 0;
}

void addMeasurement(JsonDocument& doc, const Snapshot& state) {
  const Measurement& value = state.measurement;
  const uint32_t age = millis() - value.sample.sampledAtMs;
  const bool fresh = age <= 3000;
  JsonObject sample = doc["sample"].to<JsonObject>();
  sample["raw"] = value.raw;
  sample["milliVolts"] = value.milliVolts;
  sample["rawSpread"] = value.rawSpread;
  sample["valid"] = value.sample.valid && fresh;
  sample["calibrated"] = value.sample.calibrated;
  sample["ageMs"] = age;
  sample["sampledAtUptimeMs"] = value.sample.sampledAtMs;
  if (!value.sample.valid || !fresh || !value.sample.calibrated) {
    doc["reading"] = nullptr;
    doc["readingUnavailableReason"] = !value.sample.valid ? "sensor_invalid" :
                                      !fresh ? "sensor_stale" : "not_calibrated";
    return;
  }
  JsonObject reading = doc["reading"].to<JsonObject>();
  reading["plantId"] = MYGARDEN_PLANT_ID;
  reading["deviceId"] = deviceId;
  reading["moisture"] = value.sample.moisture;
  reading["temperature"] = nullptr; // Nao ha sensor de temperatura neste circuito.
  reading["source"] = "device";
  setTimestamp(reading, "timestamp", value.unixMs);
  reading["timestampSource"] = value.unixMs ? "device_sntp" : "unsynchronized";
}

void addEvent(JsonDocument& doc, const Snapshot& state) {
  if (!state.hasEvent) { doc["command"] = nullptr; doc["event"] = nullptr; return; }
  const garden::Event& event = state.event;
  JsonObject command = doc["command"].to<JsonObject>();
  command["sequence"] = event.request.sequence;
  command["state"] = event.running ? "running" : "finished";
  command["mode"] = garden::modeName(event.request.mode);
  command["requestedDurationMs"] = event.request.durationMs;
  command["actualDurationMs"] = event.running ? millis() - event.startedAtMs : event.actualDurationMs;
  command["startedAtUptimeMs"] = event.startedAtMs;
  command["reason"] = garden::stopReasonName(event.reason);
  if (event.running) { doc["event"] = nullptr; return; }
  JsonObject finished = doc["event"].to<JsonObject>();
  finished["id"] = String(deviceId) + "/" + bootId + "/" + event.request.sequence;
  finished["plantId"] = MYGARDEN_PLANT_ID;
  finished["mode"] = garden::modeName(event.request.mode);
  finished["durationMicroseconds"] = static_cast<uint64_t>(event.actualDurationMs) * 1000;
  finished["source"] = "device";
  setTimestamp(finished, "timestamp", state.eventStartedUnixMs ?
               state.eventStartedUnixMs + event.actualDurationMs : 0);
  finished["timestampSource"] = state.eventStartedUnixMs ? "device_sntp" : "unsynchronized";
  finished["stopReason"] = garden::stopReasonName(event.reason);
}

void statusHandler() {
  if (!authorized()) return;
  const Snapshot state = snapshot();
  JsonDocument doc;
  addIdentity(doc);
  doc["connection"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  doc["ip"] = WiFi.localIP().toString();
  doc["hostname"] = hostName;
  doc["lastAcceptedSequence"] = state.lastSequence;
  JsonObject capabilities = doc["capabilities"].to<JsonObject>();
  capabilities["soilMoisture"] = true;
  capabilities["temperature"] = false;
  capabilities["plantSlots"] = 1;
  capabilities["autonomousIrrigation"] = false;
  capabilities["automaticCommandsEnabled"] = config::kAutomaticAllowed;
  capabilities["persistentEventHistory"] = false;
  JsonObject pump = doc["pump"].to<JsonObject>();
  pump["state"] = !config::kPolarityConfirmed ? "unconfigured" : state.running ? "running" : "idle";
  pump["hardwareEnabled"] = config::kHardwareEnabled;
  pump["polarityConfirmed"] = config::kPolarityConfirmed;
  pump["feedbackAvailable"] = false;
  pump["armed"] = state.armed;
  pump["armRemainingMs"] = state.armRemainingMs;
  pump["cooldownRemainingMs"] = state.cooldownMs;
  pump["budgetUsedMs"] = state.budgetUsedMs;
  pump["budgetLimitMs"] = 30000;
  pump["budgetWindowMs"] = 3600000;
  pump["maxPulseMs"] = 5000;
  JsonObject cal = doc["calibration"].to<JsonObject>();
  cal["confirmed"] = garden::calibrationValid(state.calibration);
  if (garden::calibrationValid(state.calibration)) {
    cal["dryRaw"] = state.calibration.dryRaw;
    cal["wetRaw"] = state.calibration.wetRaw;
  }
  cal["storageReady"] = storageReady;
  addMeasurement(doc, state);
  addEvent(doc, state);
  sendJson(200, doc);
}

void readingHandler() {
  if (!authorized()) return;
  JsonDocument doc;
  addIdentity(doc);
  addMeasurement(doc, snapshot());
  sendJson(200, doc);
}

int resultStatus(garden::Result result) {
  using garden::Result;
  if (result == Result::InvalidRequest) return 400;
  if (result == Result::Cooldown || result == Result::BudgetExceeded) return 429;
  if (result == Result::HardwareDisabled || result == Result::NetworkLost) return 503;
  return 409;
}

void armHandler() {
  JsonDocument input;
  if (!readBody(input)) return;
  const bool connected = WiFi.status() == WL_CONNECTED;
  lockState();
  const garden::Result result = safety.arm(millis(), connected);
  applyRelayLocked();
  unlockState();
  if (result != garden::Result::Accepted) { fail(resultStatus(result), garden::resultName(result)); return; }
  JsonDocument doc;
  addIdentity(doc);
  const Snapshot state = snapshot();
  doc["armed"] = state.armed;
  doc["armRemainingMs"] = state.armRemainingMs;
  sendJson(200, doc);
}

void irrigationHandler() {
  JsonDocument input;
  if (!readBody(input)) return;
  if (!input["sequence"].is<uint32_t>() || !input["durationMs"].is<uint32_t>() ||
      !input["minimum"].is<float>() || !input["target"].is<float>() ||
      !input["mode"].is<const char*>()) {
    fail(400, "invalid_request"); return;
  }
  const String mode = input["mode"].as<String>();
  if (mode != "manual" && mode != "automatic") { fail(400, "invalid_mode"); return; }
  garden::Request request{};
  request.sequence = input["sequence"];
  request.durationMs = input["durationMs"];
  request.minimum = input["minimum"];
  request.target = input["target"];
  request.mode = mode == "automatic" ? garden::Mode::Automatic : garden::Mode::Manual;
  const bool connected = WiFi.status() == WL_CONNECTED;
  const int64_t startedWall = wallMillis();
  lockState();
  const garden::Result result = safety.start(request, millis(), connected);
  if (result == garden::Result::Accepted) {
    EventClock& clock = eventClocks[nextEventClock];
    nextEventClock = (nextEventClock + 1) % 8;
    clock.sequence = request.sequence;
    clock.startedUnixMs = startedWall;
  }
  applyRelayLocked();
  unlockState();
  if (result != garden::Result::Accepted && result != garden::Result::Duplicate) {
    fail(resultStatus(result), garden::resultName(result)); return;
  }
  JsonDocument doc;
  addIdentity(doc);
  doc["duplicate"] = result == garden::Result::Duplicate;
  addEvent(doc, snapshot(request.sequence));
  sendJson(result == garden::Result::Accepted ? 202 : 200, doc);
}

bool parseSequence(const String& text, uint32_t& result) {
  if (text.isEmpty() || text.length() > 10) return false;
  uint64_t number = 0;
  for (unsigned i = 0; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    number = number * 10 + text[i] - '0';
  }
  if (number == 0 || number > UINT32_MAX) return false;
  result = static_cast<uint32_t>(number);
  return true;
}

void eventHandler() {
  if (!authorized()) return;
  uint32_t sequence = 0;
  if (!parseSequence(server.arg("sequence"), sequence)) { fail(400, "invalid_sequence"); return; }
  const Snapshot state = snapshot(sequence);
  if (!state.hasEvent) { fail(404, "event_not_retained"); return; }
  JsonDocument doc;
  addIdentity(doc);
  addEvent(doc, state);
  sendJson(200, doc);
}

void stopHandler() {
  JsonDocument input;
  if (!readBody(input)) return;
  lockState();
  safety.stop(millis());
  safety.disarm(millis());
  applyRelayLocked();
  unlockState();
  JsonDocument doc;
  addIdentity(doc);
  doc["armed"] = false;
  doc["pumpRunning"] = false;
  addEvent(doc, snapshot());
  sendJson(200, doc);
}

void calibrationHandler() {
  JsonDocument input;
  if (!readBody(input)) return;
  if (!input["dryRaw"].is<int>() || !input["wetRaw"].is<int>()) {
    fail(400, "invalid_calibration"); return;
  }
  garden::Calibration next{};
  next.confirmed = true;
  next.dryRaw = input["dryRaw"];
  next.wetRaw = input["wetRaw"];
  if (!garden::calibrationValid(next)) { fail(400, "invalid_calibration"); return; }
  lockState();
  const bool busy = safety.running();
  if (!busy) { safety.disarm(millis()); applyRelayLocked(); }
  unlockState();
  if (busy) { fail(409, "busy"); return; }
  if (!storageReady) { fail(503, "calibration_storage_unavailable"); return; }
  const StoredCalibration stored{1, next.dryRaw, next.wetRaw};
  if (preferences.putBytes("calibration", &stored, sizeof(stored)) != sizeof(stored)) {
    fail(503, "calibration_save_failed"); return;
  }
  lockState();
  calibration = next;
  // Exigir uma nova amostra; nao aplicar porcentagem nova a uma amostra velha.
  measurement.sample.calibrated = false;
  safety.setSample(measurement.sample);
  unlockState();
  JsonDocument doc;
  addIdentity(doc);
  doc["calibration"]["confirmed"] = true;
  doc["calibration"]["dryRaw"] = next.dryRaw;
  doc["calibration"]["wetRaw"] = next.wetRaw;
  doc["armed"] = false;
  sendJson(200, doc);
}

void registerRoutes() {
  const char* headers[] = {"Authorization", "Content-Type", "Origin"};
  server.collectHeaders(headers, 3);
  server.on("/api/v1/status", HTTP_GET, statusHandler);
  server.on("/api/v1/reading", HTTP_GET, readingHandler);
  server.on("/api/v1/calibration", HTTP_PUT, calibrationHandler);
  server.on("/api/v1/arm", HTTP_POST, armHandler);
  server.on("/api/v1/irrigation", HTTP_POST, irrigationHandler);
  server.on("/api/v1/irrigation", HTTP_GET, eventHandler);
  server.on("/api/v1/irrigation/stop", HTTP_POST, stopHandler);
  server.onNotFound([] {
    if (server.method() == HTTP_OPTIONS) {
      if (server.header("Origin").isEmpty() || !originAllowed()) { fail(403, "origin_not_allowed"); return; }
      responseHeaders();
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
      server.send(204);
      return;
    }
    if (authorized()) fail(404, "not_found");
  });
}

void setup() {
  // Nenhuma polaridade e presumida. Com -1 o GPIO fica em alta impedancia:
  // fonte da bomba deve permanecer desligada ate confirmar o modulo real.
  if (config::kPolarityConfirmed) {
    // No Arduino-ESP32 3.x, digitalWrite antes de pinMode nao registra o pino.
    // Pre-carregar o latch via IDF evita habilitar OUTPUT em LOW transitorio
    // quando o modulo e ativo LOW. Isto nao cobre o periodo anterior ao setup.
    gpio_set_level(static_cast<gpio_num_t>(config::kRelayPin), 1 - config::kRelayActiveLevel);
    pinMode(config::kRelayPin, OUTPUT);
    digitalWrite(config::kRelayPin, 1 - config::kRelayActiveLevel);
  } else pinMode(config::kRelayPin, INPUT);
  Serial.begin(115200);
  pinMode(config::kSoilPin, INPUT); // GPIO34 nao tem pull-up/pull-down interno.
  analogReadResolution(12);
  analogSetPinAttenuation(config::kSoilPin, ADC_11db);
  stateMutex = xSemaphoreCreateMutex();
  if (!stateMutex) {
    Serial.println("Falha ao iniciar mutex. Reiniciando sem habilitar a bomba.");
    delay(1000); ESP.restart();
  }
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(deviceId, sizeof(deviceId), "mygarden-%04x%08lx", static_cast<unsigned>(mac >> 32), static_cast<unsigned long>(mac));
  snprintf(hostName, sizeof(hostName), "mygarden-%06lx", static_cast<unsigned long>(mac & 0xFFFFFF));
  snprintf(bootId, sizeof(bootId), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
  storageReady = preferences.begin("mygarden", false);
  if (storageReady && preferences.getBytesLength("calibration") == sizeof(StoredCalibration)) {
    StoredCalibration stored{};
    if (preferences.getBytes("calibration", &stored, sizeof(stored)) == sizeof(stored) && stored.version == 1) {
      calibration.confirmed = true;
      calibration.dryRaw = stored.dryRaw;
      calibration.wetRaw = stored.wetRaw;
      if (!garden::calibrationValid(calibration)) calibration.confirmed = false;
    }
  }
  registerRoutes();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostName);
  WiFi.setAutoReconnect(true);
  if (strlen(MYGARDEN_WIFI_SSID)) WiFi.begin(MYGARDEN_WIFI_SSID, MYGARDEN_WIFI_PASSWORD);
  if (xTaskCreate(safetyTask, "garden-safety", 4096, nullptr, 3, nullptr) != pdPASS) {
    Serial.println("Falha ao iniciar tarefa de seguranca. Reiniciando.");
    delay(1000); ESP.restart();
  }
  Serial.printf("\nMyGarden firmware %s | %s\n", config::kVersion, deviceId);
  Serial.printf("GPIO34: sensor; GPIO26: rele. Atuacao habilitada: %s\n", config::kHardwareEnabled ? "sim, requer arm" : "nao");
  if (!config::kPolarityConfirmed) Serial.println("Polaridade desconhecida: GPIO26 em entrada. Mantenha a fonte da bomba DESLIGADA.");
  if (!config::kCredentialsReady) Serial.println("Configure include/secrets.h: Wi-Fi, token >=32 caracteres e plantId.");
}

void loop() {
  static bool wasConnected = false;
  static uint32_t lastRetry = 0;
  static uint32_t lastLog = 0;
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    MDNS.end();
    if (MDNS.begin(hostName)) MDNS.addService("http", "tcp", 80);
    if (!serverStarted) { server.begin(); serverStarted = true; }
    Serial.printf("Rede conectada. API http://%s/api/v1/status\n", WiFi.localIP().toString().c_str());
  }
  if (!connected && wasConnected) Serial.println("Rede perdida: a tarefa local desarma e interrompe a bomba.");
  wasConnected = connected;
  if (!connected && strlen(MYGARDEN_WIFI_SSID) && static_cast<uint32_t>(now - lastRetry) >= config::kWifiRetryMs) {
    lastRetry = now;
    WiFi.reconnect();
  }
  if (serverStarted && connected) server.handleClient();
  if (static_cast<uint32_t>(now - lastLog) >= 5000) {
    lastLog = now;
    const Snapshot state = snapshot();
    Serial.printf("ADC=%u | mV=%lu | valida=%d | calibrada=%d | bombaSolicitada=%d\n", state.measurement.raw,
                  static_cast<unsigned long>(state.measurement.milliVolts), state.measurement.sample.valid,
                  state.measurement.sample.calibrated, state.running);
  }
  delay(2);
}
