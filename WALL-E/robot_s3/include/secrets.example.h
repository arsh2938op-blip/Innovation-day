// ============================================================
//  WALL-E - secrets template
//  ------------------------------------------------------------
//  HOW TO USE
//  1. Copy this file:      include/secrets.example.h  ->  include/secrets.h
//  2. Fill in your real values in include/secrets.h
//  3. Never commit include/secrets.h (it is already in .gitignore)
// ============================================================
#pragma once

// ---------------- Wi-Fi ----------------
#define WALLE_WIFI_SSID     "YOUR_WIFI_SSID"
#define WALLE_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ---------------- Gemini ----------------
// Get a key at: https://aistudio.google.com/app/apikey
#define WALLE_GEMINI_API_KEY "PASTE_GEMINI_API_KEY_HERE"

// ---------------- Speech ----------------
// The STT and TTS keys that used to live here were removed with the
// voice pipeline. WALL-E no longer listens or speaks: it shows its
// Gemini replies on the OLED and in the companion app. The Gemini
// key above is now the only API key the robot needs.
