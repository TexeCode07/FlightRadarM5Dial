#include "AircraftManager.h"

constexpr int SCREEN_SIZE = 240;
constexpr int SCREEN_SIZE_DIV_2 = (SCREEN_SIZE / 2);

#include <ArduinoJson.h>

void AircraftManager::Initialise()
{
    // get centre point + radius
    lat = configServer.GetStoredString("latitude").toDouble();
    lon = configServer.GetStoredString("longitude").toDouble();
    rad = configServer.GetStoredString("radius").toDouble();
    cityName = configServer.GetStoredString("city");

    // configuration
    const String renderText = configServer.GetStoredString("infotext");
    const String renderTris = configServer.GetStoredString("triangle");
    if (!renderText.isEmpty()) displayInfoText = renderText == "true" ? true : false;
    if (!renderTris.isEmpty()) displayTriangles = renderTris == "true" ? true : false;

    // calculate how often we can call OpenSky API before being rate limited
    constexpr int MS_PER_DAY = 24 * 60 * 60 * 1000;
    constexpr int ANONYMOUS_TOKENS_PER_DAY = 400;
    constexpr int AUTHED_TOKENS_PER_DAY = 4000;
    constexpr int TOKEN_BUFFER = 3;
    int dailyRequestBudget = ANONYMOUS_TOKENS_PER_DAY - TOKEN_BUFFER; // non-authed tokens minus buffer

    const String token = authHandler.GetValidToken(configServer.GetStoredString("opensky-id"), configServer.GetStoredString("opensky-secret"));
    if (!token.isEmpty())
        dailyRequestBudget = AUTHED_TOKENS_PER_DAY - TOKEN_BUFFER; // authed tokens minus buffer

    fetchInterval = MS_PER_DAY / dailyRequestBudget;
}

void AircraftManager::AdjustZoom(long encoderDelta)
{
    if (encoderDelta == 0) return;

    // CW (positive delta) = zoom in => smaller effective radius
    zoomLevel -= static_cast<float>(encoderDelta) * ZOOM_STEP;
    zoomLevel = std::clamp(zoomLevel, MIN_ZOOM, MAX_ZOOM);
}

void AircraftManager::Update()
{
    unsigned long now = millis();

    // fetch cycle
    if (now - lastFetch >= fetchInterval) {
        lastFetch = now;

        // auth
        const String token = authHandler.GetValidToken(
            configServer.GetStoredString("opensky-id"),
            configServer.GetStoredString("opensky-secret")
        );

        std::vector<std::pair<String, String>> headers = {};
        if (!token.isEmpty()) headers.push_back({ "Authorization", "Bearer " + token });

        // request (always uses the configured radius, not the visual zoom level)
        HttpResult result = http.Get(
            "https://opensky-network.org/api/states/all",
            {
              {"lamin", String(lat - rad)},
              {"lamax", String(lat + rad)},
              {"lomin", String(lon - rad)},
              {"lomax", String(lon + rad)}
            },
            headers
        );

        // If request failed, skip this update
        if (!result.success) {
            Serial.print("[WARN] OpenSky API request failed: ");
            Serial.println(result.errorMessage);
            return;
        }

        // track
        JsonDocument doc;
        deserializeJson(doc, result.response);
        auto aircraft = JsonParser::ParseArray<Aircraft>(doc["states"]);
        now = millis(); // override with post-parse timestamp

        for (auto& ac : aircraft) {
            auto it = trackedAircraft.find(ac.icao24);
            if (it == trackedAircraft.end())
                trackedAircraft.emplace(ac.icao24, TrackedAircraft{ ac, now });
            else
                it->second.Update(ac, now);
        }

        // remove any planes that disappeared from the feed
        for (auto it = trackedAircraft.begin(); it != trackedAircraft.end(); ) {
            bool aircraftPresent = std::any_of(aircraft.begin(), aircraft.end(), [&](const Aircraft& ac) { return ac.icao24 == it->first; });
            if (!aircraftPresent)
                it = trackedAircraft.erase(it);
            else
                ++it;
        }
    }
}

void AircraftManager::Draw(LGFX_Sprite& backbuffer)
{
    DrawBackground(backbuffer);
    DrawRadarCircles(backbuffer);
    DrawCrosshair(backbuffer);
    DrawRadarSweep(backbuffer);
    DrawCompassLabels(backbuffer);
    DrawCityLabel(backbuffer);
    DrawRangeLabel(backbuffer);

    for (auto& [icao, tracked] : trackedAircraft) {
        if (tracked.state.onGround) continue;

        tracked.Tick();
        auto [predLat, predLon] = tracked.GetDisplayPosition();
        auto [x, y] = ProjectCoordinateToScreen(predLat, predLon);
        const bool isDistant = IsDistant(x, y);

        if (displayInfoText)
            DrawAircraftInfo(backbuffer, x, y, tracked);

        if (displayTriangles)
            DrawAircraftTriangle(backbuffer, x, y, tracked, isDistant);
        else
            DrawAircraftBlip(backbuffer, x, y, isDistant);
    }
}

void AircraftManager::DrawBackground(LGFX_Sprite& backbuffer) const
{
    // dark blue base, replacing the plain black fill previously done in main.cpp's loop()
    backbuffer.fillScreen(RadarTheme::Background());
}

void AircraftManager::DrawRadarCircles(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;

    backbuffer.drawCircle(CENTRE, CENTRE, OUTER, RadarTheme::RingOuter());
    backbuffer.drawCircle(CENTRE, CENTRE, (OUTER / 3) * 2, RadarTheme::RingMid());
    backbuffer.drawCircle(CENTRE, CENTRE, OUTER / 3, RadarTheme::RingInner());
}

void AircraftManager::DrawRadarSweep(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER  = SCREEN_SIZE_DIV_2 - 1;

    constexpr int   TRAIL_STEPS       = 28;     // resolution of the fading trail
    constexpr float TRAIL_SPAN_DEG    = 70.0f;  // how "long" the fading tail is, in degrees
    constexpr float ROTATION_PERIOD_MS = 4000.0f; // one full 360° revolution every 4s

    // continuously increasing angle -> always spins the same direction, forever
    const float headAngle = fmodf((millis() / ROTATION_PERIOD_MS) * 360.0f, 360.0f);

    for (int i = 0; i < TRAIL_STEPS; ++i) {
        const float t = static_cast<float>(i) / (TRAIL_STEPS - 1); // 0 = head, 1 = tail
        const float angleDeg = headAngle - t * TRAIL_SPAN_DEG;
        const float angleRad = radians(angleDeg);

        const int x2 = CENTRE + static_cast<int>(std::cos(angleRad) * OUTER);
        const int y2 = CENTRE + static_cast<int>(std::sin(angleRad) * OUTER);

        const float brightness = 1.0f - t; // linear fade from head to tail
        const uint8_t g = static_cast<uint8_t>(brightness * 200);

        backbuffer.drawLine(CENTRE, CENTRE, x2, y2, RadarTheme::SweepHead(g));
    }
}

void AircraftManager::DrawCrosshair(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;

    backbuffer.drawLine(CENTRE - OUTER, CENTRE, CENTRE + OUTER, CENTRE, RadarTheme::Crosshair());
    backbuffer.drawLine(CENTRE, CENTRE - OUTER, CENTRE, CENTRE + OUTER, RadarTheme::Crosshair());
}

void AircraftManager::DrawCompassLabels(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr int LABEL_INSET = 12; // pull labels in from the bezel edge so they aren't clipped

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(RadarTheme::CompassText());
    backbuffer.setTextDatum(lgfx::middle_center);

    backbuffer.drawString("N", CENTRE, CENTRE - OUTER + LABEL_INSET);
    backbuffer.drawString("S", CENTRE, CENTRE + OUTER - LABEL_INSET);
    backbuffer.drawString("W", CENTRE - OUTER + LABEL_INSET, CENTRE);
    backbuffer.drawString("E", CENTRE + OUTER - LABEL_INSET, CENTRE);

    backbuffer.setTextDatum(lgfx::top_left); // restore default datum for other draw calls
}

void AircraftManager::DrawCityLabel(LGFX_Sprite& backbuffer) const
{
    if (cityName.isEmpty()) return;

    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr int LABEL_INSET = 12;
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(RadarTheme::RangeText());
    backbuffer.setTextDatum(lgfx::middle_center);
    backbuffer.drawString(cityName, CENTRE, CENTRE - OUTER + LABEL_INSET + lineHeight);
    backbuffer.setTextDatum(lgfx::top_left);
}

void AircraftManager::DrawRangeLabel(LGFX_Sprite& backbuffer) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr double KM_PER_DEGREE = 111.0; // rough equatorial approximation, matches the "radius in degrees" config field

    const int ringRadiusPx = (OUTER / 3) * 2; // the mid ring is used as the labelled range marker
    const int rangeKm = static_cast<int>(rad * zoomLevel * KM_PER_DEGREE); // reflects current zoom level
    const String label = String(rangeKm) + "km";

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(RadarTheme::RangeText());
    backbuffer.setTextDatum(lgfx::middle_left);
    backbuffer.drawString(label, CENTRE + ringRadiusPx + 3, CENTRE);
    backbuffer.setTextDatum(lgfx::top_left);
}

std::pair<int, int> AircraftManager::ProjectCoordinateToScreen(float predLat, float predLon) const
{
    const float effectiveRad = static_cast<float>(rad) * zoomLevel; // zoom scales the visible window, not the fetch window

    const float dLon = predLon - lon;
    const float dLat = predLat - lat;

    const float normLon = (dLon + effectiveRad) / (2.0f * effectiveRad);
    const float normLat = (dLat + effectiveRad) / (2.0f * effectiveRad);

    const int x = static_cast<int>(normLon * SCREEN_SIZE);
    const int y = static_cast<int>(SCREEN_SIZE - (normLat * SCREEN_SIZE));

    return { x, y };
}

bool AircraftManager::IsDistant(int x, int y) const
{
    constexpr int CENTRE = SCREEN_SIZE_DIV_2 - 1;
    constexpr int OUTER = SCREEN_SIZE_DIV_2 - 1;
    constexpr float DISTANT_THRESHOLD = 0.9f; // fraction of outer radius considered "near the rim"

    const float dx = static_cast<float>(x - CENTRE);
    const float dy = static_cast<float>(y - CENTRE);
    const float dist = std::sqrt(dx * dx + dy * dy);

    return dist >= OUTER * DISTANT_THRESHOLD;
}

void AircraftManager::DrawAircraftInfo(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked) const
{
    const int lineHeight = tft.fontHeight() + 1;

    backbuffer.setTextSize(1);
    backbuffer.setTextColor(RadarTheme::AircraftInfo());
    backbuffer.drawString(tracked.state.callsign, x + 5, y + 5);
    backbuffer.drawString(String(tracked.state.velocity) + "m/s", x + 5, y + 5 + lineHeight);
    backbuffer.drawString(String(tracked.state.baroAltitude) + "m", x + 5, y + 5 + lineHeight * 2);
}

void AircraftManager::DrawAircraftTriangle(LGFX_Sprite& backbuffer, int x, int y, const TrackedAircraft& tracked, bool isDistant) const
{
    // forward vector (nose direction) and right vector (perpendicular)
    const float dx = std::sin(radians(tracked.state.trueTrack));
    const float dy = -std::cos(radians(tracked.state.trueTrack));
    const float px = -dy;
    const float py = dx;

    // local coord system: 'along' = distance forward (+) / back (-) along heading
    //                      'across' = distance right (+) / left (-) of heading
    auto toScreen = [&](float along, float across) -> std::pair<float, float> {
        return { x + dx * along + px * across, y + dy * along + py * across };
    };

    // --- shape parameters (tweak these to taste) ---
    constexpr float NOSE_LEN          = 6.0f;   // nose tip forward
    constexpr float FUSELAGE_TAIL     = -5.0f;  // back of fuselage
    constexpr float FUSELAGE_WIDTH    = 1.0f;   // half-width of fuselage dart

    constexpr float WING_APEX_FWD     = 1.0f;   // where wing root meets fuselage (ahead of tip line)
    constexpr float WING_TIP_ALONG    = -1.5f;  // wingtips swept slightly back
    constexpr float WING_HALF_SPAN    = 6.0f;

    constexpr float TAIL_APEX_ALONG   = -3.0f;  // tailplane root position
    constexpr float TAIL_TIP_ALONG    = -5.0f;
    constexpr float TAIL_HALF_SPAN    = 3.0f;

    const uint32_t colour = isDistant ? RadarTheme::DistantBlip() : RadarTheme::AircraftBlip();

    // fuselage — thin dart from nose to tail
    auto [noseX, noseY]   = toScreen(NOSE_LEN, 0.0f);
    auto [fLX, fLY]       = toScreen(FUSELAGE_TAIL, FUSELAGE_WIDTH);
    auto [fRX, fRY]       = toScreen(FUSELAGE_TAIL, -FUSELAGE_WIDTH);
    backbuffer.fillTriangle(noseX, noseY, fLX, fLY, fRX, fRY, colour);

    // main wings — wide swept-back delta
    auto [wApexX, wApexY] = toScreen(WING_APEX_FWD, 0.0f);
    auto [wLX, wLY]       = toScreen(WING_TIP_ALONG, WING_HALF_SPAN);
    auto [wRX, wRY]       = toScreen(WING_TIP_ALONG, -WING_HALF_SPAN);
    backbuffer.fillTriangle(wApexX, wApexY, wLX, wLY, wRX, wRY, colour);

    // tailplane — smaller delta near the back
    auto [tApexX, tApexY] = toScreen(TAIL_APEX_ALONG, 0.0f);
    auto [tLX, tLY]       = toScreen(TAIL_TIP_ALONG, TAIL_HALF_SPAN);
    auto [tRX, tRY]       = toScreen(TAIL_TIP_ALONG, -TAIL_HALF_SPAN);
    backbuffer.fillTriangle(tApexX, tApexY, tLX, tLY, tRX, tRY, colour);
}

void AircraftManager::DrawAircraftBlip(LGFX_Sprite& backbuffer, int x, int y, bool isDistant) const
{
    const uint32_t colour = isDistant ? RadarTheme::DistantBlip() : RadarTheme::AircraftBlip();
    backbuffer.fillCircle(x, y, 3, colour);
}