#pragma once
#include <M5GFX.h>

// On M5Dial, M5GFX configures the GC9A01 panel internally
// (SPI2, MOSI=7, SCLK=6, DC=2, CS=10, BL=3) — identical to
// what the old LGFX class was doing manually.
// We alias M5GFX so all existing LGFX& parameters compile unchanged.
using LGFX = M5GFX;