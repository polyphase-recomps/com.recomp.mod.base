/**
 * @file ModBaseDisplay.h
 * @brief Resolution scaler shared by every recomp player node.
 *
 * Players draw the game's frame (PS1 320x240, GBA 240x160, ...) into a Quad. Instead of
 * stretching it over the screen, they call Recomp_DisplayApply each frame, which places
 * the Quad by the user's display settings:
 *
 *   Fit         as large as fits, with the game's real shape (4:3, GBA 3:2) [default]
 *   Integer     the largest whole multiple of the native size that fits (sharp pixels)
 *   Native      1:1, the game's own resolution, centred
 *   Full Screen fills the whole screen (stretched)
 *   Scale xN    N times the native size (opt-in: 2x..6x), centred
 *
 * Filter: Auto (the runtime's own choice), Sharp (nearest) or Smooth (linear).
 *
 * "Full Screen" and window sizes per platform:
 *   Windows (packaged game): Window "Full Screen" is the engine's borderless desktop
 *     fullscreen; the other window presets size the window (1280x720, 1920x1080, ...,
 *     or a multiple of the game's resolution). In the editor only the picture inside
 *     the game view changes.
 *   Wii / GameCube: the screen is the console's video mode (640x480, PAL 640x528); the
 *     fit modes apply inside it. A Wii set to 16:9 stretches the 640-wide picture, which
 *     the fit compensates.
 *
 * The settings are saved with the user's mod settings (ModSettings, ids "display.*").
 */
#pragma once

#include "ModBaseApi.h"
#include "ModBaseProvider.h"

#include <cstdint>
#include <string>
#include <vector>

class Quad;
class Texture;

enum class RecompFitMode : uint8_t
{
    Fit,
    Integer,
    Native,
    FullScreen,
    Scale,
    Count
};

struct RecompDisplaySettings
{
    RecompFitMode mode = RecompFitMode::Fit;
    int scale = 2;          // Scale mode: 2..6
    int filter = 0;         // 0 auto (the runtime's default), 1 sharp (nearest), 2 smooth (linear)
    int window = 0;         // Windows window preset, see Recomp_WindowPresets (0 = leave)
};

struct RecompRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct RecompWindowPreset
{
    const char* label;
    int width;  // 0 with height 0: leave the window alone; -1: borderless fullscreen;
    int height; // negative width -N: N x the game's native size
};

MODBASE_API RecompDisplaySettings& Recomp_DisplaySettings();
MODBASE_API const char* Recomp_FitModeName(RecompFitMode mode);
// Window presets offered on this platform (Windows only; empty elsewhere).
MODBASE_API const std::vector<RecompWindowPreset>& Recomp_WindowPresets();

// Where the picture goes in a viewW x viewH screen. `displayAspect` is the shape the
// game's frame should have (4:3); `screenStretch` is how much the screen stretches
// horizontally (Wii at 16:9: 1.333, else 1).
MODBASE_API RecompRect Recomp_DisplayFit(int nativeW, int nativeH, float displayAspect, float viewW, float viewH,
                                         float screenStretch, const RecompDisplaySettings& settings);

// Places the player's display Quad for this frame (call after updating its texture).
// True when the texture's filter doesn't match the setting: recreate it with
// Recomp_DisplayFilterLinear().
// `defaultLinear`: the runtime's own filter when the setting is Auto.
MODBASE_API bool Recomp_DisplayApply(Quad* quad, Texture* texture, int frameW, int frameH, float displayAspect,
                                     bool defaultLinear = false);
MODBASE_API bool Recomp_DisplayFilterLinear(bool defaultLinear = false);

// Packaged Windows builds: applies the window preset when it changed (no-op elsewhere).
MODBASE_API void Recomp_DisplayApplyWindow(int nativeW, int nativeH);
