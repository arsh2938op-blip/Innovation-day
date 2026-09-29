// ============================================================
//  Camera manager
// ------------------------------------------------------------
//  !! HARDWARE DEPENDENCY !!
//  A generic ESP32-C3 module has NO camera peripheral. The camera
//  modules people mean (e.g. OV2640) need a camera-capable SoC and
//  the esp32-camera driver. WALL-E therefore ships with
//  WALLE_ENABLE_CAMERA = 0 and this file compiles to a stub that
//  reports "no camera".
//
//  To enable:
//   1. Confirm your SoC has a camera block (see README).
//   2. Fill in the CAMERA_* pins in include/config.h.
//   3. Add the vendor camera library to platformio.ini.
//   4. Set WALLE_ENABLE_CAMERA to 1.
// ============================================================
#pragma once

#include <Arduino.h>
#include "config.h"

class CameraManager {
public:
    bool begin();
    bool available() const { return _available; }

private:
    bool _available = false;

public:

    // Grabs a JPEG frame. Returns nullptr when there is no camera.
    // Only safe to call from a state that is allowed to block.
    const uint8_t* capture(uint8_t** jpegOut, size_t* lenOut);
    void release(uint8_t* jpeg);
};

extern CameraManager cameraManager;
