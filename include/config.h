#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "ShipRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://ship-radar.local */
constexpr char kPortalHostname[] = "ship-radar";
constexpr char kPortalHostUrl[] = "ship-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-2424S012 / ESP32-C3, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: Sunton ESP32-2424S012, GC9A01A 1.2" round 240x240 (SPI) ---
constexpr int kDisplayPinRst = -1;
constexpr int kDisplayPinCs = 10;
constexpr int kDisplayPinDc = 2;
constexpr int kDisplayPinMosi = 7;  // display SDA
constexpr int kDisplayPinSclk = 6;  // display SCL
constexpr int kDisplayPinBacklight = 3;

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 51.03515;
constexpr double kDefaultRadarLon = 1.55343;

/** Redraw cadence while AISStream websocket messages are quiet. */
constexpr unsigned long kAisDisplayRefreshIntervalMs = 3000;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
