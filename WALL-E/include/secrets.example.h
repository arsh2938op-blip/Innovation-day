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

// ---------------- Speech-to-Text (Google Cloud STT v1) ----------------
// Optional: enabled/disabled + endpoint can be changed in include/config.h
#define WALLE_STT_API_KEY   "PASTE_GOOGLE_CLOUD_STT_KEY_HERE"

// ---------------- Text-to-Speech ----------------
// Google Cloud TTS uses the same Google Cloud key as STT.
// If you switch TTS to a different provider, put that key here instead.
#define WALLE_TTS_API_KEY   "PASTE_GOOGLE_CLOUD_TTS_KEY_HERE"
