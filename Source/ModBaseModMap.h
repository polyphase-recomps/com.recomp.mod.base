/**
 * @file ModBaseModMap.h
 * @brief ModMap asset: the mod settings of one game.
 *
 * A ModMap lists what end users can change (or watch) in a game: each entry has an id,
 * a label and group for the settings UI, a widget kind (toggle, number, choice, action,
 * display, bar) and a source: a published bridge variable, a raw address or decomp
 * symbol, a game request, a startup option, or one of the built-in display settings.
 *
 * Made with the Mod Map Editor (Tools > Recomp > Mods), shipped like any asset, read at
 * runtime by ModSettings. The settings UI scene is generated from it.
 */
#pragma once

#include "ModBaseApi.h"
#include "ModBaseProvider.h"

#include "Asset.h"
#include "AssetRef.h"

#include "glm/glm.hpp"

#include <string>
#include <vector>

enum class ModKind : uint8_t
{
    Toggle,  // on / off
    Int,     // number with - / +
    Float,   // number with - / +, fractional steps
    Choice,  // one of a list of labelled values, cycled
    Action,  // a button (requests)
    Display, // read-only text
    Bar,     // read-only bar against a maximum
    Count
};

enum class ModSource : uint8_t
{
    Variable,      // a published bridge variable (name + index)
    Address,       // raw game memory (address + type)
    Symbol,        // decomp global by name (GameCube), read as `type`
    Request,       // game request (name + arguments)
    StartupOption, // option given to the game when it starts (PS1 game.json "options")
    Display,       // built-in resolution scaler setting ("display.mode", ...)
    Count
};

struct ModEntry
{
    std::string mId;      // unique within the map; also the settings save key
    std::string mLabel;
    std::string mGroup;
    std::string mHelp;
    ModKind mKind = ModKind::Toggle;
    ModSource mSource = ModSource::Variable;
    std::string mName;    // variable / symbol / request / option / display setting
    int32_t mIndex = 0;   // array element (Variable)
    uint64_t mAddress = 0;
    RecompType mType = RecompType::S32; // Address / Symbol
    std::string mArgs;    // Request arguments, "1000" or "3, 0"
    float mMin = 0.0f;
    float mMax = 1.0f;
    float mStep = 1.0f;
    float mDefault = 0.0f;
    std::vector<std::string> mChoiceLabels;
    std::vector<float> mChoiceValues;
    std::string mFormat;  // Display: text format ({v} = the value, other {tokens} work too)
    std::string mMaxName; // Bar: variable holding the maximum
    bool mPersist = true; // saved with the user's settings and re-applied
    bool mLock = false;   // re-applied whenever the game changes it (cheats)
    bool mOnTitle = false;// also shown before the game publishes its variables
};

// How the generated settings scene looks (Tools > Recomp > Mods > Menu Style). Applied to
// the scene's nodes on every Generate / Update and by the style window's Update button.
// Defaults are the generated look.
struct ModStyle
{
    enum ButtonState : uint8_t { Normal, Hovered, Pressed, Locked, StateCount };

    // panel (background)
    glm::vec4 mPanelColor = {0.04f, 0.05f, 0.08f, 0.88f}; // tint (with a texture) or fill
    AssetRef mPanelTexture;
    bool mTintPanel = true; // off: the panel's texture as it is
    // buttons: per-state texture (empty = the Normal one, or none) and tint
    AssetRef mButtonTextures[StateCount];
    glm::vec4 mButtonColors[StateCount] = {
        {0.5f, 0.5f, 0.5f, 1.0f}, {0.6f, 0.6f, 0.6f, 1.0f}, {0.4f, 0.4f, 0.4f, 1.0f}, {0.2f, 0.2f, 0.2f, 1.0f}};
    glm::vec4 mButtonTextColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float mButtonTextSize = 16.0f;
    float mTabTextSize = 16.0f;
    glm::vec4 mHighlightColor = {1.0f, 0.8f, 0.2f, 1.0f}; // border of the gamepad-selected button
    float mHighlightWidth = 3.0f;
    bool mShowHighlight = true;                          // that border at all
    bool mButtonStateTint = true;                        // the state colors tint the whole button (hover, press)
    // the button textures: the part shown (UV scale / offset, Crop Texture) and how it fits the
    // button (ObjectFit: 0 fill, 1 contain, 2 cover, 3 none)
    glm::vec2 mButtonUvScale = {1.0f, 1.0f};
    glm::vec2 mButtonUvOffset = {0.0f, 0.0f};
    uint8_t mButtonFit = 1;
    // text
    AssetRef mFont; // empty = the engine default
    AssetRef mHeaderFont; // titles (empty = mFont)
    AssetRef mBodyFont;   // labels, values, notes and other text (empty = mFont)
    AssetRef mButtonFont; // the buttons' and tabs' text (empty = mFont)
    // sounds (SoundWave assets; none = silent)
    AssetRef mSoundMove;   // the gamepad / keyboard selection moves to another button
    AssetRef mSoundSelect; // a button is pressed
    AssetRef mSoundCancel; // a Close button is pressed
    AssetRef mSoundBack;   // the gamepad's B (or the close action) closes the menu
    float mSoundVolume = 1.0f;
    glm::vec4 mTitleColor = {1.0f, 0.85f, 0.35f, 1.0f};
    float mTitleSize = 20.0f;
    glm::vec4 mLabelColor = {1.0f, 1.0f, 1.0f, 1.0f};     // labels of rows you can change
    glm::vec4 mInfoColor = {0.65f, 0.75f, 0.9f, 1.0f};    // labels of read-only rows, the note
    float mLabelSize = 16.0f;
    glm::vec4 mValueColor = {1.0f, 0.85f, 0.35f, 1.0f};
    float mValueSize = 16.0f;
    float mNoteSize = 13.0f;
};

// The game's launcher (Tools > Recomp > Mods > Launcher): the front-end scene that sets the
// ROM up, opens the mod settings and starts the game. Its buttons and text use the menu style
// above; these are the launcher's own look and behaviour. Applied on every Generate / Update.
struct ModLauncherSettings
{
    std::string mTitle;                                   // "" = the map's title
    std::string mSubtitle;                                // a line under the title
    AssetRef mLogo;                                       // a picture above the title (none = no logo)
    glm::vec2 mLogoSize = {320.0f, 120.0f};
    glm::vec2 mLogoUvScale = {1.0f, 1.0f};               // the part of the logo shown (Crop Texture)
    glm::vec2 mLogoUvOffset = {0.0f, 0.0f};
    uint8_t mLogoFit = 1;                                 // ObjectFit: 0 fill, 1 contain, 2 cover, 3 none
    AssetRef mBackground;                                 // full-screen picture behind the panel
    glm::vec4 mBackgroundColor = {0.0f, 0.0f, 0.0f, 1.0f}; // its tint (or the fill without one)
    bool mTintBackground = true;                          // off: the picture as it is
    glm::vec2 mPanelSize = {440.0f, 460.0f};              // largest panel size (it fits smaller screens)
    bool mPanelFullScreen = false;                        // the panel fills the screen (Panel size unused)
    int32_t mPosition = 0;                                // 0 centre, 1 left, 2 right
    std::string mPlayLabel = "Play";
    std::string mBrowseLabel = "Choose ROM...";
    std::string mForgetLabel = "Forget ROM";
    std::string mModsLabel = "Mods";
    std::string mQuitLabel = "Quit";
    bool mShowForget = false;
    bool mShowMods = true;
    bool mShowQuit = true;
    AssetRef mGameScene;                                  // opened once the game starts (none = stay)
    bool mAutoStart = false;                              // start at once when the game has its ROM
    AssetRef mSoundStart;                                 // Play (the game starts once it has played)
    AssetRef mSoundQuit;                                  // Quit (a packaged game closes once it has played)
    AssetRef mMusic;                                      // loops while the launcher is up
    AssetRef mSoundDeny;                                  // Play without a ROM, a ROM refused, a failed start
    AssetRef mSoundForget;                                // Forget ROM
    // the footer: bottom left a line and a logo after it, bottom right the app's version
    bool mShowFooter = true;
    std::string mFooterText = "Compiled with the Polyphase Engine";
    AssetRef mFooterLogo;                                 // after the text (none = no logo)
    glm::vec2 mFooterLogoSize = {40.0f, 10.0f};
    std::string mVersionFormat = "Version {@launcher.version}"; // "" = no version
    float mFooterTextSize = 5.0f;
    float mMusicVolume = 0.7f;
};

class MODBASE_API ModMap : public Asset
{
public:
    DECLARE_ASSET(ModMap, Asset);

    ModMap();
    virtual ~ModMap();

    virtual void LoadStream(Stream& stream, Platform platform) override;
    virtual void SaveStream(Stream& stream, Platform platform) override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;
    virtual glm::vec4 GetTypeColor() override;
    virtual const char* GetTypeName() override;

    std::string mGame;     // package id, "com.recomp.digimonworld"
    std::string mRuntime;  // "ps1", "gcn", "n64", "gba"
    std::string mTitle;    // settings UI title
    std::string mSaveName; // end-user settings file name (default: from the game id)
    std::vector<std::string> mGroups; // display order of groups
    std::vector<ModEntry> mEntries;
    ModStyle mStyle;
    ModLauncherSettings mLauncher;

    const ModEntry* Find(const std::string& id) const;
    ModEntry* Find(const std::string& id);
    std::string SaveName() const;
    // Groups in display order: mGroups first, then any group only entries name.
    std::vector<std::string> OrderedGroups() const;
};

MODBASE_API const char* ModKindName(ModKind kind);
MODBASE_API const char* ModSourceName(ModSource source);

// Every ModMap asset in the project (loads them). For one game: pass its package id.
MODBASE_API std::vector<ModMap*> ModMap_FindAll(const std::string& game = std::string());
