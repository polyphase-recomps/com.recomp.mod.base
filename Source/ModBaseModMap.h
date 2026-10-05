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
