// ============================================================
//  Hardware test mode + serial console
// ------------------------------------------------------------
//  Built with -DWALLE_TEST_MODE=1 (env:walle_test) the firmware
//  boots into an interactive menu where each piece of hardware can
//  be checked on its own - no AI, no motion, no Wi-Fi needed
//  unless you pick that test.
// ============================================================
#pragma once

#include <Arduino.h>

// Interactive menu (blocking). Only called from setup().
void runHardwareTestMode();

// Non-blocking serial console, available in the normal build too.
void pollSerialConsole();
void printConsoleHelp();
