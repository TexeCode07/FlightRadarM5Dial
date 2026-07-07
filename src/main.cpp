#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include "M5Dial.h"
#include "WiFiManagerHelpers.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include "AircraftManager.h"
// #include "DrawHelpers.h"
#include "models/Aircraft.h"
#include "models/TrackedAircraft.h"

constexpr int SCREEN_SIZE       = 240;
constexpr int SCREEN_SIZE_DIV_2 = SCREEN_SIZE / 2;

LGFX_Sprite backbuffer(&M5Dial.Display);

WiFiManager              wm;
ConfigurationWebServer   configServer;
HttpRequestManager       http;
OpenSkyAuthTokenHandler  authHandler(http);
AircraftManager          aircraftManager(configServer, authHandler, http,
                                         static_cast<LGFX&>(M5Dial.Display));

static long encoderLast = 0;

void setup()
{
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);
    M5Dial.Display.setBrightness(100);

    backbuffer.setColorDepth(8);
    backbuffer.createSprite(SCREEN_SIZE, SCREEN_SIZE);

    M5Dial.Display.fillScreen(lgfx::color888(0, 0, 0));
    M5Dial.Display.setTextColor(lgfx::color888(0, 255, 0));
    M5Dial.Display.drawCentreString("Connecting to WiFi...",
                                    SCREEN_SIZE / 2, SCREEN_SIZE / 2);

    WiFiManagerHelpers::ConfigureWiFiManager(wm, static_cast<LGFX&>(M5Dial.Display));
    wm.autoConnect(WiFiManagerHelpers::WiFiManagerName);

    configServer.Initialise();
    aircraftManager.Initialise();

    encoderLast = M5Dial.Encoder.read();
}

// void loop()
// {
//     M5Dial.update();

//     // Read encoder delta and use it to zoom the radar view in/out
//     long encoderNow   = M5Dial.Encoder.read();
//     long encoderDelta = encoderNow - encoderLast;
//     encoderLast       = encoderNow;

//     aircraftManager.AdjustZoom(encoderDelta);

//     aircraftManager.Update();

//     String renderScanlines = configServer.GetStoredString("scanline");
//     if (renderScanlines.isEmpty() || renderScanlines == "true") {
//         DrawScanLines(
//             backbuffer,
//             SCREEN_SIZE_DIV_2 - 1,
//             SCREEN_SIZE_DIV_2 - 1,
//             SCREEN_SIZE_DIV_2 - 1 + (std::cos(millis() / 3000.0f) * SCREEN_SIZE_DIV_2),
//             SCREEN_SIZE_DIV_2 - 1 + (std::sin(millis() / 3000.0f) * SCREEN_SIZE_DIV_2),
//             20, 128, 5
//         );
//     }

//     // backbuffer.fillSprite(TFT_BLACK);
//     aircraftManager.Draw(backbuffer);
//     backbuffer.pushSprite(0, 0);


// }

void loop()
{
    M5Dial.update();

    long encoderNow   = M5Dial.Encoder.read();
    long encoderDelta = encoderNow - encoderLast;
    encoderLast       = encoderNow;

    aircraftManager.AdjustZoom(encoderDelta);
    aircraftManager.Update();

    aircraftManager.Draw(backbuffer);
    backbuffer.pushSprite(0, 0);
}