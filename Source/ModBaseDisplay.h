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
 * Runtimes that draw on the host's GPU may also offer (Recomp_SetRenderFeatures):
 *   Upscaler       Off, or FSR 1: the picture upscaled to its size on screen (AMD FidelityFX
 *                  Super Resolution 1, edge-adaptive), from the render Resolution
 *   Sharpness      Off, Low, Medium, High (contrast-adaptive sharpening, FSR's RCAS)
 *   Anti-aliasing  Off or SMAA (subpixel morphological anti-aliasing, 1x)
 *   Textures       Original (the game's own sampling) or Trilinear / Anisotropic 2x-16x for
 *                  the world's textures (mipmaps made from the full-size texture; which
 *                  textures count is the runtime's choice)
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
    int resolution = 1;     // render resolution: the game drawn at N x its own size (1..4), by
                            // runtimes that can (Recomp_SetMaxResolution); players clamp it
    // GPU post-processing and filtering (runtimes that offer them: Recomp_SetRenderFeatures)
    int upscaler = 0;       // 0 off, 1 FSR 1
    int sharpness = 0;      // 0 off, 1 low, 2 medium, 3 high
    int antialias = 0;      // 0 off, 1 SMAA
    int textures = 0;       // 0 original, 1 trilinear, 2..5 anisotropic 2x, 4x, 8x, 16x
};

// "Resolution" (display.resolution) is offered by runtimes that can draw the game larger than
// its own size: they call Recomp_SetMaxResolution(n) when they load (the settings scene and the
// editor list it only when some runtime did).
#define RECOMP_DISPLAY_HAS_RESOLUTION 1
MODBASE_API void Recomp_SetMaxResolution(int maxScale);
MODBASE_API int Recomp_MaxResolution(); // 1 until a runtime sets more

// "Upscaler", "Sharpness", "Anti-aliasing" and "Textures" (display.upscaler / sharpness /
// antialias / textures) are offered when a runtime declares it can do them, when it loads.
#define RECOMP_DISPLAY_HAS_RENDER_FEATURES 1
enum RecompRenderFeature : uint32_t
{
    RecompRender_Upscaler = 1u << 0,
    RecompRender_Sharpness = 1u << 1,
    RecompRender_AntiAlias = 1u << 2,
    RecompRender_Textures = 1u << 3,
};
MODBASE_API void Recomp_SetRenderFeatures(uint32_t features); // adds to what is offered
MODBASE_API uint32_t Recomp_RenderFeatures();
// Whether the settings scene / editor should list this display setting id here.
MODBASE_API bool Recomp_DisplaySettingOffered(const char* id);
// Every display setting id, in the order the settings list them.
MODBASE_API const std::vector<const char*>& Recomp_DisplaySettingIds();

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
