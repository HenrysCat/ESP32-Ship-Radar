#pragma once

#include <cstddef>
#include <cstdint>

namespace services::ais {

struct Vessel {
  float lat;
  float lon;
  float heading_deg;
  float course_deg;
  float sog_knots;
  uint32_t mmsi;
  char name[21];
  char type[12];
  char speed[12];
  unsigned long last_seen_ms;
};

constexpr size_t kMaxVessels = 64;
constexpr size_t kApiKeyMaxLen = 96;

void init();
void loop(double center_lat, double center_lon, float radius_km);
void disconnect();

size_t vesselCount();
const Vessel* vesselList();
bool consumeDirty();
const char* statusText();

const char* apiKey();
bool hasApiKey();
void saveApiKeyFromPortal(const char* key);
void clearApiKey();

}  // namespace services::ais
