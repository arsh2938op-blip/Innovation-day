// ============================================================
//  Tiny logging helper -> produces the exact log format used in
//  the spec, e.g.  [WIFI] Connecting...
//  Usage: LOGI("WIFI", "Connected");   LOGE("S3", "HTTP %d", code);
// ============================================================
#pragma once

#include <Arduino.h>

#ifndef WALLE_LOG_LEVEL
#define WALLE_LOG_LEVEL 2   // 0=off 1=error 2=info(+error) 3=debug
#endif

#if WALLE_LOG_LEVEL >= 2
#define WALLE_LOG_PRINTF(tag, fmt, ...) \
    do { Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#else
#define WALLE_LOG_PRINTF(tag, fmt, ...) do {} while (0)
#endif

#if WALLE_LOG_LEVEL >= 3
#define WALLE_LOG_DEBUG(tag, fmt, ...) WALLE_LOG_PRINTF(tag, fmt, ##__VA_ARGS__)
#else
#define WALLE_LOG_DEBUG(tag, fmt, ...) do {} while (0)
#endif

#define LOGE(tag, fmt, ...) WALLE_LOG_PRINTF(tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) WALLE_LOG_PRINTF(tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) WALLE_LOG_PRINTF(tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) WALLE_LOG_DEBUG(tag, fmt, ##__VA_ARGS__)
