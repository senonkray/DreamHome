#pragma once
// Edit these values before uploading. Keep this file private.
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY "CHANGE_ME_TO_A_RANDOM_32_CHAR_KEY"
// Only this HTTPS website may use the API cross-origin. Local UI works too.
#define ALLOWED_ORIGIN "https://luma-home-atelier-d25.abloomgnome257.chatgpt.site"
// Calibrated wiring: D5 lights green, D6 blue, D7 red.
#define RED_PIN 13    // D7 / GPIO13
#define GREEN_PIN 14  // D5 / GPIO14
#define BLUE_PIN 12   // D6 / GPIO12
// Backup Wi-Fi (optional): used only while the first network is down. Leave "" to disable.
#define WIFI_SSID_2 ""
#define WIFI_PASSWORD_2 ""
