#pragma once
#include <stdint.h>
#include <stddef.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef MYGARDEN_WIFI_SSID
#define MYGARDEN_WIFI_SSID ""
#endif
#ifndef MYGARDEN_WIFI_PASSWORD
#define MYGARDEN_WIFI_PASSWORD ""
#endif
#ifndef MYGARDEN_API_TOKEN
#define MYGARDEN_API_TOKEN ""
#endif
#ifndef MYGARDEN_PLANT_ID
#define MYGARDEN_PLANT_ID ""
#endif
#ifndef MYGARDEN_RELAY_ACTIVE_LEVEL
#define MYGARDEN_RELAY_ACTIVE_LEVEL -1
#endif
#ifndef MYGARDEN_PUMP_ENABLED
#define MYGARDEN_PUMP_ENABLED 0
#endif
#ifndef MYGARDEN_AUTOMATIC_ENABLED
#define MYGARDEN_AUTOMATIC_ENABLED 0
#endif
#ifndef MYGARDEN_WEB_ORIGIN
#define MYGARDEN_WEB_ORIGIN ""
#endif

namespace config {
constexpr uint8_t kSoilPin = 34;  // ADC1_CH6, entrada; nunca ligado aos barramentos.
constexpr uint8_t kRelayPin = 26; // Somente IN do modulo rele.
constexpr int kRelayActiveLevel = MYGARDEN_RELAY_ACTIVE_LEVEL;
constexpr bool kPolarityConfirmed = kRelayActiveLevel == 0 || kRelayActiveLevel == 1;
constexpr bool kCredentialsReady = sizeof(MYGARDEN_API_TOKEN) > 32 &&
                                   sizeof(MYGARDEN_API_TOKEN) <= 129 &&
                                   sizeof(MYGARDEN_PLANT_ID) > 1 &&
                                   sizeof(MYGARDEN_WIFI_SSID) > 1;
constexpr bool kHardwareEnabled = MYGARDEN_PUMP_ENABLED && kPolarityConfirmed &&
                                  kCredentialsReady;
constexpr bool kAutomaticAllowed = MYGARDEN_AUTOMATIC_ENABLED;
constexpr uint32_t kSampleIntervalMs = 500;
constexpr uint32_t kSafetyPeriodMs = 10;
constexpr uint32_t kWifiRetryMs = 15000;
constexpr size_t kMaxBodyBytes = 1024;
constexpr const char* kVersion = "0.1.0";
static_assert(kSoilPin != kRelayPin, "GPIOs devem ser distintos");
static_assert(kRelayActiveLevel >= -1 && kRelayActiveLevel <= 1, "Polaridade: -1, 0 ou 1");
static_assert(sizeof(MYGARDEN_PLANT_ID) <= 65, "plantId: maximo 64 caracteres");
}
