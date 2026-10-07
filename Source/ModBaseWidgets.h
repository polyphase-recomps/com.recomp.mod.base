/**
 * @file ModBaseWidgets.h
 * @brief UI widgets bound to the running game (any recomp runtime) and to mod settings.
 *
 *  RecompText    Text with a format: "HP {hp}/{hp_max}", "Attack x{@cheat_atkmul}".
 *  RecompButton  A Button that changes a mod setting (Setting + Direction), sends a game
 *                request, toggles a variable or steps it. Its label can be a format too.
 *                The selected button gets a border, so gamepad focus is easy to see.
 *  RecompBar     ProgressBar showing a variable against a maximum.
 *  RecompMenuController
 *                Put one in a UI's root, next to the panel it shows/hides (Panel property,
 *                default: the sibling named "Panel"). The root must stay visible: hidden
 *                widgets don't tick. While the panel is visible it gets the gamepad: a
 *                (visible) button is selected for navigation and kept scrolled into view
 *                in any ScrollContainer, the right stick scrolls the shown page,
 *                the game gets no input (Recomp_IsInputCaptured), and B closes the UI.
 *                Toggle Button (a gamepad button) and/or Toggle Action (a PlayerInput
 *                action, "Category/Name") open and close it; both are logged at start.
 *
 * Format tokens (RecompText, RecompButton labels, RecompFormat for C++ users):
 *   {name}          a game variable (number or text)
 *   {name[3]}       element 3 of an array variable
 *   {name>table}    the variable's value used as an index into another variable
 *   {name:02}       numbers zero-padded to 2 digits
 *   {name?yes|no}   "yes" when the variable is non-zero, else "no"
 *   {@id}           a mod setting's value as shown in menus ("ON", "Fit", "3")
 *   {@id.label}     a mod setting's label
 *   {@launcher.x}   the game's launcher: title, rom, romfile, message, status, ready
 *                   (ModBaseLauncher.h)
 *   {{ / }}         literal braces
 * Unresolved tokens show as "--" (the game publishes its variables once it reaches its
 * main loop). With no game running the widgets keep what the editor shows.
 */
#pragma once

#include "ModBaseApi.h"

#include "Nodes/Widgets/Button.h"
#include "Nodes/Widgets/ProgressBar.h"
#include "Nodes/Widgets/Text.h"

#include <cstdint>
#include <string>
#include <vector>

// Expands the tokens above. `missing` is set when a token couldn't be resolved.
MODBASE_API std::string RecompFormat(const std::string& format, bool* missing = nullptr);
// True while a game runs and publishes variables (any runtime).
MODBASE_API bool RecompIsLive();
// The project's PlayerInput actions as "Category/Name" (empty on engines that don't export
// PlayerInputSystem), and whether one was just pressed ("Category/Name" or just "Name").
MODBASE_API std::vector<std::string> RecompInputActions();
MODBASE_API bool RecompActionJustPressed(const std::string& action);
MODBASE_API const char* RecompGamepadButtonName(int32_t gamepadButton);

struct ModStyle;
// Restyles a settings UI tree (a generated scene's root, or a live instance of it): the
// Panel's tint / texture, every RecompButton (state textures and colors, text, selection
// border; tabs use the tab text size), and the Title / Label / Value / Note texts.
MODBASE_API void ModStyle_Apply(Node* root, const ModStyle& style);
// The font of a text role (header / body / button font), else the style's font, else the engine's.
class Font;
class AssetRef;
MODBASE_API Font* ModStyle_Font(const ModStyle& style, const AssetRef& role);

class MODBASE_API RecompText : public Text
{
public:
    DECLARE_NODE(RecompText, Text);

    virtual void Tick(float deltaTime) override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    void SetFormat(const std::string& format);
    void SetHideIfMissing(bool hide);

protected:
    std::string mFormat;
    bool mHideIfMissing = false;
};

class MODBASE_API RecompButton : public Button
{
public:
    DECLARE_NODE(RecompButton, Button);

    virtual void Activate() override;
    virtual void Tick(float deltaTime) override;
    virtual void PreRender() override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    // How the texture fits the button (ObjectFit: 0 fill, 1 contain, 2 cover, 3 none).
    void SetTextureFit(uint8_t fit);

    // Setting mode: Direction +1 steps up / toggles / next choice / runs an action,
    // -1 steps down / previous choice.
    void SetSetting(const std::string& id, int32_t direction = 1);
    // Request mode: comma-separated integer arguments.
    void SetRequest(const std::string& request, const std::string& arguments = "");
    void SetToggle(const std::string& variable, int32_t onValue = 1);
    void SetStep(const std::string& variable, int32_t step, int32_t minValue, int32_t maxValue);
    void SetLabelFormat(const std::string& format);
    void SetHighlight(glm::vec4 color, float width);

protected:
    std::string mSetting;
    int32_t mDirection = 1;
    std::string mRequest;
    std::string mArguments;
    std::string mToggleVariable;
    int32_t mToggleOnValue = 1;
    std::string mVariable;
    int32_t mStep = 1;
    int32_t mMin = 0;
    int32_t mMax = 10;
    std::string mLabelFormat;
    glm::vec4 mHighlightColor = {1.0f, 0.8f, 0.2f, 1.0f};
    float mHighlightWidth = 3.0f;
    bool mHighlighted = false;
    int32_t mPending = 0;
    uint8_t mTextureFit = 1; // contain
};

class MODBASE_API RecompBar : public ProgressBar
{
public:
    DECLARE_NODE(RecompBar, ProgressBar);

    virtual void Tick(float deltaTime) override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    void SetVariables(const std::string& variable, const std::string& maxVariable);

protected:
    std::string mVariable;
    std::string mMaxVariable;
};

class MODBASE_API RecompMenuController : public Widget
{
public:
    enum class Sound : uint8_t { Move, Select, Cancel, Back, Count };

public:
    DECLARE_NODE(RecompMenuController, Widget);

    virtual void Start() override;
    virtual void Stop() override;
    virtual void Destroy() override;
    virtual void Tick(float deltaTime) override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    void Open();
    void Close();
    void Toggle();
    bool IsOpen() const;
    bool IsInHomeMenu() const;
    const std::string& GetTitle() const;
    void Setup(const std::string& title, bool startVisible, bool captureInput, Node* firstButton);
    void SetBoundVariable(const std::string& name);
    void SetInHomeMenu(bool inHomeMenu);
    void SetToggleButton(int32_t gamepadButton);
    // PlayerInput action ("Category/Name" or "Name") that opens/closes the UI, and one that
    // only closes it (B always closes).
    void SetToggleAction(const std::string& action);
    void SetCloseAction(const std::string& action);
    // B closes the UI (default). Off for UIs that must stay, like a launcher.
    void SetCloseOnBack(bool closeOnBack);
    // The widget shown/hidden. Empty: a sibling named "Panel", else the parent. Keep the
    // controller outside it: hidden widgets don't tick, so a controller inside the widget
    // it hides can't open it again.
    void SetPanel(Node* panel);
    // A full-stretch panel fills the screen minus `margin`, at most `maxSize` (0 = no cap),
    // placed by `align` (0 centre, 1 left, 2 right) when the screen is larger.
    void SetPanelFit(glm::vec2 maxSize, float margin, int32_t align);
    // The menu's sounds (SoundWave assets; none = silent) and their volume.
    void SetSounds(const AssetRef& move, const AssetRef& select, const AssetRef& cancel, const AssetRef& back,
                   float volume);
    void PlaySound(Sound sound);
    // A launcher's sound at this menu's effects volume.
    void PlaySoundWave(class SoundWave* wave);

    static const std::vector<RecompMenuController*>& GetAll();
    static bool IsCapturingInput();
    // The controller of the UI a node is in (a child of one of its ancestors).
    static RecompMenuController* FindFor(Node* node);
    // The generated mod settings menu: the controller whose UI has "Pages" (else the first).
    static RecompMenuController* FindSettingsMenu();

protected:
    Widget* Target();
    bool TargetVisible();
    void SyncBoundVariable();
    void FitPanel();
    void GamepadScroll(float deltaTime, Widget* target);

    std::string mTitle = "Menu";
    bool mStartVisible = false;
    bool mCaptureInput = true;
    WeakPtr<Button> mFirstButton;
    WeakPtr<Widget> mPanel;
    glm::vec2 mMaxPanelSize = {0.0f, 0.0f};
    float mPanelMargin = 16.0f;
    int32_t mPanelAlign = 0;
    glm::vec4 mFitMargins = {-1.0f, -1.0f, -1.0f, -1.0f};
    bool mFitted = false; // FitPanel changed the panel's margins
    // sounds (Move, Select, Cancel, Back) and their volume
    AssetRef mSounds[(int)Sound::Count];
    float mSoundVolume = 1.0f;
    WeakPtr<Button> mLastSelected; // for the Move sound
    float mScrollSpeed = 420.0f;
    int32_t mToggleButton = -1;
    std::string mToggleAction;
    std::string mCloseAction;
    bool mCloseOnBack = true;
    bool mLoggedSetup = false;
    bool mInHomeMenu = true;
    std::string mBoundVariable;
    bool mWasVisible = false;
    bool mSelectPending = false;
    bool mBoundOn = false;
    // several open UIs (the mod settings over a launcher): the last opened drives the gamepad
    uint32_t mOpenOrder = 0;
};
