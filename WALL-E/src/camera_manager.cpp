#include "camera_manager.h"
#include "log.h"

static const char* TAG = "CAM";

CameraManager cameraManager;

bool CameraManager::begin() {
#if WALLE_ENABLE_CAMERA
#error "Camera support needs a camera-capable SoC and the esp32-camera driver. See src/camera_manager.h."
#else
    _available = false;
    LOGW(TAG, "No camera on this build (WALLE_ENABLE_CAMERA=0) - continuing without vision");
    return false;
#endif
}

const uint8_t* CameraManager::capture(uint8_t** jpegOut, size_t* lenOut) {
    *jpegOut = nullptr;
    *lenOut = 0;
    if (!_available) return nullptr;
    return nullptr;   // real implementation goes here once the driver is wired in
}

void CameraManager::release(uint8_t* jpeg) { if (jpeg) free(jpeg); }
