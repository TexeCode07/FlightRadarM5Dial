#pragma once

#include <map>
#include <algorithm>

#include "models/TrackedAircraft.h"
#include "ConfigurationWebServer.h"
#include "OpenSkyAuthTokenHandler.h"
#include "LGFX.h"

// Colour palette modelled on MatixYo's ESP32-Plane-Radar:
// dark blue background, subdued green rings/crosshair, white compass labels.
namespace RadarTheme
{
    inline uint32_t Background()   { return lgfx::color888(4, 12, 28); }
    inline uint32_t RingOuter()    { return lgfx::color888(0, 130, 70); }
    inline uint32_t RingMid()      { return lgfx::color888(0, 80, 45); }
    inline uint32_t RingInner()    { return lgfx::color888(0, 50, 30); }
    inline uint32_t Crosshair()    { return lgfx::color888(0, 60, 35); }
    inline uint32_t CompassText()  { return lgfx::color888(255, 255, 255); }
    inline uint32_t RangeText()    { return lgfx::color888(180, 220, 200); }
    inline uint32_t AircraftInfo() { return lgfx::color888(0, 210, 140); }
    inline uint32_t AircraftBlip() { return lgfx::color888(0, 255, 140); }
    inline uint32_t DistantBlip()  { return lgfx::color888(255, 70, 70); } // near rim = red, like the reference project
    inline uint32_t SweepHead(uint8_t g)   { return lgfx::color888(0, g, static_cast<uint8_t>(g * 0.55f)); }
}

class AircraftManager
{
private:
    double lat = 0.0;
    double lon = 0.0;
    double rad = 0.2;
    String cityName = "";
    std::map<String, TrackedAircraft> trackedAircraft;

    bool displayInfoText = true;
    bool displayTriangles = true;

    // zoom (encoder-controlled, purely visual — does not affect OpenSky fetch radius)
    float zoomLevel = 1.0f;
    static constexpr float MIN_ZOOM = 0.2f;   // most zoomed in (smallest area shown)
    static constexpr float MAX_ZOOM = 3.0f;   // most zoomed out (largest area shown)
    static constexpr float ZOOM_STEP = 0.05f; // change per encoder tick

    unsigned long fetchInterval = 0;
    unsigned long lastFetch = 999999;

    ConfigurationWebServer& configServer;
    OpenSkyAuthTokenHandler& authHandler;
    HttpRequestManager& http;
    LGFX& tft;

    void DrawBackground(LGFX_Sprite& backbuffer) const;
    void DrawRadarCircles(LGFX_Sprite& backbuffer) const;
    void DrawCrosshair(LGFX_Sprite& backbuffer) const;
    void DrawRadarSweep(LGFX_Sprite& backbuffer) const;
    void DrawCompassLabels(LGFX_Sprite& backbuffer) const;
    void DrawCityLabel(LGFX_Sprite& backbuffer) const;
    void DrawRangeLabel(LGFX_Sprite& backbuffer) const;
    std::pair<int, int> ProjectCoordinateToScreen(float predLat, float predLon) const;
    bool IsDistant(int x, int y) const;
    void DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const;
    void DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool isDistant) const;
    void DrawAircraftBlip(LGFX_Sprite& backbuffer, int x, int y, bool isDistant) const;

public:
    AircraftManager(ConfigurationWebServer& config, OpenSkyAuthTokenHandler& auth, HttpRequestManager& httpManager, LGFX& tftGfx)
        : configServer(config), authHandler(auth), http(httpManager), tft(tftGfx)
    {
    }
    ~AircraftManager() = default;

    void Initialise();
    void Update();
    void Draw(LGFX_Sprite& backbuffer);

    // Feed raw encoder delta in from main loop() each frame.
    // Positive delta (clockwise) zooms in; negative (counter-clockwise) zooms out.
    void AdjustZoom(long encoderDelta);
};