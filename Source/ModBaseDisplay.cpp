/**
 * @file ModBaseDisplay.cpp
 * @brief Resolution scaler (see ModBaseDisplay.h).
 */

#include "ModBaseDisplay.h"

#include "Engine.h"
#include "Assets/Texture.h"
#include "Nodes/Widgets/Quad.h"
#include "System/System.h"

#include <algorithm>
#include <cmath>

RecompDisplaySettings& Recomp_DisplaySettings()
{
    static RecompDisplaySettings sSettings;
    return sSettings;
}

const char* Recomp_FitModeName(RecompFitMode mode)
{
    switch (mode)
    {
    case RecompFitMode::Fit: return "Fit";
    case RecompFitMode::Integer: return "Integer";
    case RecompFitMode::Native: return "Native";
    case RecompFitMode::FullScreen: return "Full Screen";
    case RecompFitMode::Scale: return "Scale";
    default: return "?";
    }
}

const std::vector<RecompWindowPreset>& Recomp_WindowPresets()
{
#if PLATFORM_WINDOWS
    static const std::vector<RecompWindowPreset> sPresets = {
        {"Keep", 0, 0},           {"Full Screen", -1, 0},   {"2x Native", -2, 0},  {"3x Native", -3, 0},
        {"4x Native", -4, 0},     {"1280 x 720", 1280, 720}, {"1600 x 900", 1600, 900},
        {"1920 x 1080", 1920, 1080}, {"2560 x 1440", 2560, 1440}, {"3840 x 2160", 3840, 2160},
    };
#else
    static const std::vector<RecompWindowPreset> sPresets;
#endif
    return sPresets;
}

RecompRect Recomp_DisplayFit(int nativeW, int nativeH, float displayAspect, float viewW, float viewH,
                             float screenStretch, const RecompDisplaySettings& settings)
{
    RecompRect r;
    if (viewW <= 0.0f || viewH <= 0.0f)
    {
        return r;
    }
    if (settings.mode == RecompFitMode::FullScreen || nativeW <= 0 || nativeH <= 0)
    {
        r.w = viewW;
        r.h = viewH;
        return r;
    }
    if (displayAspect <= 0.0f)
    {
        displayAspect = float(nativeW) / float(nativeH);
    }
    if (screenStretch <= 0.0f)
    {
        screenStretch = 1.0f;
    }
    // the shape in screen pixels that shows as displayAspect once the screen stretches it
    const float aspect = displayAspect / screenStretch;
    const float fitH = std::min(viewH, viewW / aspect);

    float h = fitH;
    switch (settings.mode)
    {
    case RecompFitMode::Integer:
    {
        const float k = std::floor(fitH / float(nativeH));
        h = k >= 1.0f ? k * float(nativeH) : fitH;
        break;
    }
    case RecompFitMode::Native:
        h = std::min(float(nativeH), fitH);
        break;
    case RecompFitMode::Scale:
        h = std::min(float(nativeH) * float(std::max(1, settings.scale)), fitH);
        break;
    default:
        break;
    }
    r.h = std::floor(h);
    r.w = std::floor(std::min(viewW, h * aspect));
    r.x = std::floor((viewW - r.w) * 0.5f);
    r.y = std::floor((viewH - r.h) * 0.5f);
    return r;
}

bool Recomp_DisplayFilterLinear(bool defaultLinear)
{
    const int filter = Recomp_DisplaySettings().filter;
    return filter == 0 ? defaultLinear : filter == 2;
}

bool Recomp_DisplayApply(Quad* quad, Texture* texture, int frameW, int frameH, float displayAspect, bool defaultLinear)
{
    if (quad == nullptr)
    {
        return false;
    }
    const float viewW = quad->GetParentWidth();
    const float viewH = quad->GetParentHeight();
    float stretch = 1.0f;
#if PLATFORM_DOLPHIN
    // a Wii set to 16:9 widens the 640-wide picture on the TV
    stretch = GetEngineState()->mAspectRatioScale;
#endif
    const RecompRect r = Recomp_DisplayFit(frameW, frameH, displayAspect, viewW, viewH, stretch,
                                           Recomp_DisplaySettings());
    // widget offsets and sizes are scaled by the UI scale: undo it to place by pixels
    glm::vec2 scale = quad->GetAbsoluteScale();
    if (scale.x <= 0.0f) scale.x = 1.0f;
    if (scale.y <= 0.0f) scale.y = 1.0f;
    quad->SetAnchorMode(AnchorMode::TopLeft);
    quad->SetPosition(r.x / scale.x, r.y / scale.y);
    quad->SetDimensions(r.w / scale.x, r.h / scale.y);
    quad->SetObjectFit(ObjectFit::Fill);

    return texture != nullptr &&
           (texture->GetFilterType() == FilterType::Linear) != Recomp_DisplayFilterLinear(defaultLinear);
}

void Recomp_DisplayApplyWindow(int nativeW, int nativeH)
{
#if PLATFORM_WINDOWS && !EDITOR
    static int sApplied = 0;
    static bool sFullscreen = false;
    const int preset = Recomp_DisplaySettings().window;
    const std::vector<RecompWindowPreset>& presets = Recomp_WindowPresets();
    if (preset == sApplied || preset < 0 || preset >= (int)presets.size())
    {
        return;
    }
    sApplied = preset;
    const RecompWindowPreset& p = presets[preset];
    if (p.width == 0 && p.height == 0)
    {
        return;
    }
    if (p.width == -1)
    {
        SYS_SetFullscreen(true);
        sFullscreen = true;
        return;
    }
    if (sFullscreen || SYS_IsFullscreen())
    {
        SYS_SetFullscreen(false);
        sFullscreen = false;
    }
    int w = p.width, h = p.height;
    if (w < 0)
    {
        // N x the game's native height, at 4:3
        h = -w * (nativeH > 0 ? nativeH : 240);
        w = h * 4 / 3;
        (void)nativeW;
    }
    int32_t x = 0, y = 0, cw = 0, ch = 0;
    SYS_GetWindowRect(x, y, cw, ch);
    SYS_SetWindowRect(x, y, w, h);
#else
    (void)nativeW;
    (void)nativeH;
#endif
}
