#pragma once

namespace ScreenshotControl
{
    // Automatic score screenshots are enabled by default.
    // F9 toggles the feature on/off while Rocksmith is running.
    void Initialize();
    void Poll();
    void Shutdown();
}
