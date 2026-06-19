#include "services/ais_client.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace services::ais {
namespace {

constexpr char kPrefsNamespace[] = "aisstream";
constexpr char kPrefsApiKey[] = "apiKey";
constexpr char kHost[] = "stream.aisstream.io";
constexpr uint16_t kPort = 443;
constexpr char kPath[] = "/v0/stream";
constexpr unsigned long kStaleVesselMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kReconnectMs = 10000UL;
constexpr unsigned long kSubscriptionMinMs = 1200UL;
constexpr unsigned long kNoRxResubscribeMs = 15000UL;
constexpr unsigned long kHealthLogIntervalMs = 30000UL;
constexpr float kKmPerDegLat = 111.0f;

Preferences s_prefs;
WebSocketsClient s_ws;
Vessel s_vessels[kMaxVessels];
size_t s_vessel_count = 0;
char s_api_key[kApiKeyMaxLen + 1] = "";
bool s_started = false;
bool s_connected = false;
bool s_dirty = false;
char s_status_text[48] = "AIS OFFLINE";
char s_last_error[48] = "";
unsigned long s_last_message_ms = 0;
unsigned long s_first_subscription_ms = 0;
unsigned long s_last_health_log_ms = 0;
uint32_t s_rx_count = 0;
uint32_t s_frame_count = 0;
uint8_t s_subscription_variant = 0;
bool s_subscription_pending = false;
double s_center_lat = 0.0;
double s_center_lon = 0.0;
float s_radius_km = 0.0f;
double s_sub_lat = 999.0;
double s_sub_lon = 999.0;
float s_sub_radius_km = -1.0f;
unsigned long s_last_subscription_ms = 0;

template <typename T>
T clampValue(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void trimCopy(const char* in, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return;
  }
  while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') {
    ++in;
  }
  size_t n = strnlen(in, out_len - 1);
  while (n > 0 &&
         (in[n - 1] == ' ' || in[n - 1] == '\t' || in[n - 1] == '\r' ||
          in[n - 1] == '\n')) {
    --n;
  }
  memcpy(out, in, n);
  out[n] = '\0';
}

void setStatus(const char* text) {
  if (text == nullptr) {
    text = "";
  }
  if (strncmp(s_status_text, text, sizeof(s_status_text)) == 0) {
    return;
  }
  strncpy(s_status_text, text, sizeof(s_status_text) - 1);
  s_status_text[sizeof(s_status_text) - 1] = '\0';
  s_dirty = true;
}

void setErrorStatus(const char* text) {
  if (text == nullptr || text[0] == '\0') {
    s_last_error[0] = '\0';
    return;
  }
  snprintf(s_last_error, sizeof(s_last_error), "AIS ERR: %.38s", text);
  setStatus(s_last_error);
  Serial.println(s_last_error);
}

void refreshIdleStatus() {
  if (!hasApiKey()) {
    setStatus("NO AIS KEY");
  } else if (s_last_error[0] != '\0') {
    setStatus(s_last_error);
  } else if (!s_started) {
    setStatus("AIS OFFLINE");
  } else if (!s_connected) {
    setStatus("AIS CONNECTING");
  } else if (s_subscription_variant >= 2 && s_rx_count == 0) {
    setStatus("AIS DIAG WIDE AREA");
  } else if (s_rx_count == 0) {
    setStatus("AIS CONNECTED - WAITING");
  } else if (s_vessel_count == 0) {
    setStatus("NO VESSELS IN RANGE");
  } else {
    setStatus("");
  }
}

void logHealth(bool force = false) {
  const unsigned long now = millis();
  if (!force && now - s_last_health_log_ms < kHealthLogIntervalMs) {
    return;
  }
  s_last_health_log_ms = now;

  const unsigned long age_s =
      s_last_message_ms == 0 ? 0 : (now - s_last_message_ms) / 1000UL;
  Serial.printf(
      "ais: health key=%s ws=%s frames=%lu vessels=%u last=%lus status=\"%s\"\n",
      hasApiKey() ? "yes" : "no", s_connected ? "connected" : "offline",
      static_cast<unsigned long>(s_frame_count),
      static_cast<unsigned>(s_vessel_count), age_s, s_status_text);
}

bool readJsonFloat(const JsonObjectConst& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

bool readJsonUint(const JsonObjectConst& obj, const char* key, uint32_t* out) {
  if (obj[key].is<uint32_t>() || obj[key].is<unsigned long>() ||
      obj[key].is<int>()) {
    *out = obj[key].as<uint32_t>();
    return true;
  }
  return false;
}

void copyJsonStringTrimmed(const JsonObjectConst& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  trimCopy(obj[key].as<const char*>(), out, out_len);
}

Vessel* findOrAllocateVessel(uint32_t mmsi) {
  for (size_t i = 0; i < s_vessel_count; ++i) {
    if (s_vessels[i].mmsi == mmsi) {
      return &s_vessels[i];
    }
  }
  if (s_vessel_count < kMaxVessels) {
    Vessel* v = &s_vessels[s_vessel_count++];
    memset(v, 0, sizeof(*v));
    v->mmsi = mmsi;
    return v;
  }

  size_t oldest = 0;
  for (size_t i = 1; i < s_vessel_count; ++i) {
    if (s_vessels[i].last_seen_ms < s_vessels[oldest].last_seen_ms) {
      oldest = i;
    }
  }
  Vessel* v = &s_vessels[oldest];
  memset(v, 0, sizeof(*v));
  v->mmsi = mmsi;
  return v;
}

void pruneStaleVessels() {
  const unsigned long now = millis();
  for (size_t i = 0; i < s_vessel_count;) {
    if (now - s_vessels[i].last_seen_ms <= kStaleVesselMs) {
      ++i;
      continue;
    }
    s_vessels[i] = s_vessels[s_vessel_count - 1];
    --s_vessel_count;
    s_dirty = true;
  }
}

void formatSpeed(Vessel* vessel) {
  if (vessel->sog_knots > 0.05f) {
    snprintf(vessel->speed, sizeof(vessel->speed), "%.1f kt", vessel->sog_knots);
  } else {
    strncpy(vessel->speed, "0 kt", sizeof(vessel->speed) - 1);
    vessel->speed[sizeof(vessel->speed) - 1] = '\0';
  }
}

void setFallbackName(Vessel* vessel) {
  if (vessel->name[0] != '\0') {
    return;
  }
  snprintf(vessel->name, sizeof(vessel->name), "%lu",
           static_cast<unsigned long>(vessel->mmsi));
}

bool updateVesselFromMessage(const JsonObjectConst& msg,
                             const JsonObjectConst& metadata,
                             const char* message_type) {
  float lat = 0.0f;
  float lon = 0.0f;
  if (!readJsonFloat(msg, "Latitude", &lat) &&
      !readJsonFloat(metadata, "Latitude", &lat) &&
      !readJsonFloat(metadata, "latitude", &lat)) {
    Serial.printf("ais: %s without lat\n", message_type);
    return false;
  }
  if (!readJsonFloat(msg, "Longitude", &lon) &&
      !readJsonFloat(metadata, "Longitude", &lon) &&
      !readJsonFloat(metadata, "longitude", &lon)) {
    Serial.printf("ais: %s without lon\n", message_type);
    return false;
  }

  uint32_t mmsi = 0;
  if (!readJsonUint(msg, "UserID", &mmsi) &&
      !readJsonUint(metadata, "MMSI", &mmsi)) {
    Serial.printf("ais: %s without MMSI\n", message_type);
    return false;
  }

  Vessel* vessel = findOrAllocateVessel(mmsi);
  vessel->lat = lat;
  vessel->lon = lon;
  vessel->last_seen_ms = millis();

  float heading = 0.0f;
  if (readJsonFloat(msg, "TrueHeading", &heading) && heading <= 360.0f) {
    vessel->heading_deg = heading;
  } else if (readJsonFloat(msg, "Cog", &heading)) {
    vessel->heading_deg = heading;
  }

  float course = 0.0f;
  if (readJsonFloat(msg, "Cog", &course)) {
    vessel->course_deg = course;
  } else {
    vessel->course_deg = vessel->heading_deg;
  }

  readJsonFloat(msg, "Sog", &vessel->sog_knots);
  copyJsonStringTrimmed(metadata, "ShipName", vessel->name, sizeof(vessel->name));
  if (vessel->name[0] == '\0') {
    copyJsonStringTrimmed(msg, "Name", vessel->name, sizeof(vessel->name));
  }
  setFallbackName(vessel);

  if (msg["Type"].is<int>()) {
    snprintf(vessel->type, sizeof(vessel->type), "TYPE %d", msg["Type"].as<int>());
  } else if (vessel->type[0] == '\0') {
    strncpy(vessel->type, "VESSEL", sizeof(vessel->type) - 1);
    vessel->type[sizeof(vessel->type) - 1] = '\0';
  }
  formatSpeed(vessel);
  s_dirty = true;
  return true;
}

JsonObjectConst messageBodyForType(const JsonDocument& doc, const char* type) {
  JsonObjectConst message = doc["Message"].as<JsonObjectConst>();
  if (message.isNull() || type == nullptr) {
    return JsonObjectConst();
  }
  return message[type].as<JsonObjectConst>();
}

void handleTextMessage(const char* payload) {
  s_last_message_ms = millis();
  ++s_rx_count;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("ais: JSON parse error: %s\n", err.c_str());
    setErrorStatus("JSON parse");
    return;
  }
  if (doc["error"].is<const char*>()) {
    setErrorStatus(doc["error"].as<const char*>());
    return;
  }

  const char* type = doc["MessageType"] | "";
  JsonObjectConst body = messageBodyForType(doc, type);
  if (body.isNull()) {
    Serial.printf("ais: unsupported message type '%s'\n", type);
    return;
  }

  JsonObjectConst metadata = doc["MetaData"].as<JsonObjectConst>();
  if (metadata.isNull()) {
    metadata = doc["Metadata"].as<JsonObjectConst>();
  }
  updateVesselFromMessage(body, metadata, type);
  refreshIdleStatus();
}

void handleFramePayload(const uint8_t* payload, size_t length, const char* label) {
  if (payload == nullptr || length == 0) {
    return;
  }
  ++s_frame_count;
  String text;
  text.reserve(static_cast<unsigned>(length + 1));
  text.concat(reinterpret_cast<const char*>(payload),
              static_cast<unsigned>(length));
  handleTextMessage(text.c_str());
}

void sendSubscription(uint8_t variant = 0) {
  if (!s_connected || !hasApiKey()) {
    return;
  }
  const unsigned long now = millis();
  if (now - s_last_subscription_ms < kSubscriptionMinMs) {
    return;
  }

  double lat_min = 0.0;
  double lat_max = 0.0;
  double lon_min = 0.0;
  double lon_max = 0.0;
  const bool wide_diag = variant >= 2;
  const bool use_alt_key = (variant % 2) == 1;

  if (wide_diag) {
    lat_min = 49.0;
    lat_max = 56.0;
    lon_min = -6.0;
    lon_max = 4.0;
  } else {
    const double lat_span = static_cast<double>(s_radius_km) / kKmPerDegLat;
    const double cos_lat =
        std::max(0.08, fabs(cos(s_center_lat * 0.017453292519943)));
    const double lon_span = lat_span / cos_lat;

    lat_min = clampValue(s_center_lat - lat_span, -90.0, 90.0);
    lat_max = clampValue(s_center_lat + lat_span, -90.0, 90.0);
    lon_min = clampValue(s_center_lon - lon_span, -180.0, 180.0);
    lon_max = clampValue(s_center_lon + lon_span, -180.0, 180.0);
  }

  JsonDocument doc;
  doc[use_alt_key ? "Apikey" : "APIKey"] = s_api_key;
  JsonArray boxes = doc["BoundingBoxes"].to<JsonArray>();
  JsonArray box = boxes.add<JsonArray>();
  JsonArray c1 = box.add<JsonArray>();
  c1.add(lat_min);
  c1.add(lon_min);
  JsonArray c2 = box.add<JsonArray>();
  c2.add(lat_max);
  c2.add(lon_max);
  String json;
  serializeJson(doc, json);
  const bool sent = s_ws.sendTXT(json);
  if (!sent) {
    Serial.println("ais: subscription send failed");
    setStatus("AIS SUB SEND FAILED");
    return;
  }
  s_last_error[0] = '\0';
  setStatus(wide_diag ? "AIS DIAG WIDE AREA" : "AIS SUBSCRIBED");
  s_sub_lat = s_center_lat;
  s_sub_lon = s_center_lon;
  s_sub_radius_km = s_radius_km;
  s_last_subscription_ms = now;
  s_subscription_variant = variant;
  s_subscription_pending = false;
  if (s_rx_count == 0 && s_first_subscription_ms == 0) {
    s_first_subscription_ms = now;
  }
  Serial.printf("ais: subscribed bbox [%.5f,%.5f] [%.5f,%.5f]%s\n", lat_min,
                lon_min, lat_max, lon_max,
                wide_diag ? " (wide diagnostic)" : "");
  logHealth(true);
}

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      s_connected = true;
      s_last_error[0] = '\0';
      s_rx_count = 0;
      s_frame_count = 0;
      s_first_subscription_ms = 0;
      s_subscription_variant = 0;
      s_subscription_pending = true;
      setStatus("AIS CONNECTED");
      Serial.println("ais: websocket connected");
      logHealth(true);
      break;
    case WStype_DISCONNECTED:
      s_connected = false;
      setStatus("AIS DISCONNECTED");
      Serial.println("ais: websocket disconnected");
      logHealth(true);
      break;
    case WStype_ERROR:
      if (payload != nullptr && length > 0) {
        String text;
        text.reserve(static_cast<unsigned>(length + 1));
        text.concat(reinterpret_cast<const char*>(payload),
                    static_cast<unsigned>(length));
        Serial.print("ais: websocket error ");
        Serial.println(text);
      }
      setErrorStatus("websocket");
      break;
    case WStype_TEXT:
      handleFramePayload(payload, length, "text");
      break;
    case WStype_BIN:
      handleFramePayload(payload, length, "binary");
      break;
    case WStype_FRAGMENT_TEXT_START:
      Serial.printf("ais: text fragment start %u bytes\n",
                    static_cast<unsigned>(length));
      break;
    case WStype_FRAGMENT:
      Serial.printf("ais: fragment %u bytes\n", static_cast<unsigned>(length));
      break;
    case WStype_FRAGMENT_FIN:
      Serial.printf("ais: fragment end %u bytes\n", static_cast<unsigned>(length));
      break;
    case WStype_PING:
      Serial.println("ais: ping");
      break;
    case WStype_PONG:
      Serial.println("ais: pong");
      break;
    default:
      break;
  }
}

void startWebSocket() {
  if (s_started || !hasApiKey() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  s_ws.beginSSL(kHost, kPort, kPath, "", "");
  s_ws.onEvent(onWebSocketEvent);
  s_ws.setReconnectInterval(kReconnectMs);
  s_ws.enableHeartbeat(15000, 3000, 2);
  s_started = true;
  setStatus("AIS CONNECTING");
  Serial.println("ais: websocket starting");
  logHealth(true);
}

bool subscriptionChanged() {
  if (!s_connected) {
    return false;
  }
  if (fabs(s_center_lat - s_sub_lat) > 0.0001 ||
      fabs(s_center_lon - s_sub_lon) > 0.0001) {
    return true;
  }
  return fabs(s_radius_km - s_sub_radius_km) > 0.5f;
}

bool subscriptionNeedsRetry() {
  if (!s_connected || s_frame_count > 0 || s_rx_count > 0) {
    return false;
  }
  return millis() - s_last_subscription_ms >= kNoRxResubscribeMs;
}

}  // namespace

void init() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const String saved = s_prefs.getString(kPrefsApiKey, "");
  s_prefs.end();
  trimCopy(saved.c_str(), s_api_key, sizeof(s_api_key));
}

void loop(double center_lat, double center_lon, float radius_km) {
  s_center_lat = center_lat;
  s_center_lon = center_lon;
  s_radius_km = radius_km;

  if (WiFi.status() != WL_CONNECTED || !hasApiKey()) {
    disconnect();
    refreshIdleStatus();
    return;
  }

  startWebSocket();
  s_ws.loop();
  if (s_subscription_pending) {
    sendSubscription(0);
  } else if (subscriptionChanged()) {
    s_subscription_variant = 0;
    sendSubscription(0);
  } else if (subscriptionNeedsRetry()) {
    uint8_t next_variant = s_subscription_variant + 1;
    if (next_variant > 3) {
      next_variant = 0;
    }
    sendSubscription(next_variant);
  }
  pruneStaleVessels();
  refreshIdleStatus();
  logHealth();
}

void disconnect() {
  if (!s_started) {
    return;
  }
  s_ws.disconnect();
  s_started = false;
  s_connected = false;
  s_first_subscription_ms = 0;
  s_frame_count = 0;
  s_subscription_variant = 0;
  s_subscription_pending = false;
  refreshIdleStatus();
}

size_t vesselCount() { return s_vessel_count; }

const Vessel* vesselList() { return s_vessels; }

bool consumeDirty() {
  const bool dirty = s_dirty;
  s_dirty = false;
  return dirty;
}

const char* statusText() { return s_status_text; }

const char* apiKey() { return s_api_key; }

bool hasApiKey() { return s_api_key[0] != '\0'; }

void saveApiKeyFromPortal(const char* key) {
  char trimmed[kApiKeyMaxLen + 1];
  trimCopy(key, trimmed, sizeof(trimmed));
  if (strcmp(trimmed, s_api_key) == 0) {
    return;
  }
  strncpy(s_api_key, trimmed, sizeof(s_api_key) - 1);
  s_api_key[sizeof(s_api_key) - 1] = '\0';
  if (s_prefs.begin(kPrefsNamespace, false)) {
    if (s_api_key[0] == '\0') {
      s_prefs.remove(kPrefsApiKey);
    } else {
      s_prefs.putString(kPrefsApiKey, s_api_key);
    }
    s_prefs.end();
  }
  disconnect();
  s_rx_count = 0;
  s_frame_count = 0;
  s_first_subscription_ms = 0;
  s_subscription_variant = 0;
  s_subscription_pending = false;
  Serial.printf("ais: API key %s\n", hasApiKey() ? "saved" : "cleared");
}

void clearApiKey() { saveApiKeyFromPortal(""); }

}  // namespace services::ais
