/**
 * @file ModBaseSettings.h
 * @brief The end user's mod settings for the running game.
 *
 * ModSettings binds the ModMap of the game a provider runs, keeps the values the user
 * picked, writes them to the game (once its bridge is up, again after every game restart,
 * and whenever the game changes a "lock" entry), and saves them:
 *   Windows / Wii:  <project>/Saves/<save name>.mods
 *   GameCube:       memory card, slot A
 * Saving happens automatically a moment after a change, and on Save().
 *
 * Entries without a value picked by the user show the game's current value (variables,
 * addresses) or their default. The built-in display settings (resolution scaler) are
 * always available as "display.mode", "display.scale", "display.filter",
 * "display.resolution" and "display.window", whether or not the map lists them
 * ("display.resolution" is shown only when a runtime supports it: Recomp_MaxResolution()).
 *
 * Player nodes call ModSettings::Get().Tick(provider) every frame, and pass
 * StartupOptions(game) to the game when they start it.
 */
#pragma once

#include "ModBaseApi.h"
#include "ModBaseModMap.h"
#include "ModBaseProvider.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class MODBASE_API ModSettings
{
public:
    static ModSettings& Get();

    // Per frame, from the player node running the game.
    void Tick(RecompProvider* provider);

    // The map of a game package (found among the project's ModMap assets), or the bound one.
    ModMap* Bind(const std::string& game);
    void Bind(ModMap* map);
    ModMap* GetMap() const;

    // The value of an entry: the user's, else the game's, else the default.
    bool GetValue(const std::string& id, float& value) const;
    // "ON" / "OFF", a choice's label, or the number.
    std::string ValueText(const std::string& id) const;
    // Sets the user's value and writes it to the game.
    bool SetValue(const std::string& id, float value);
    // Toggle: flips. Int/Float: +/- step (direction -1 / +1) within min/max.
    // Choice: next / previous. Action: sends its request.
    bool Step(const std::string& id, int direction);
    bool Activate(const std::string& id) { return Step(id, 1); }
    // Back to the default (the user's value is forgotten).
    void Reset(const std::string& id);
    void ResetAll();
    bool IsUserSet(const std::string& id) const;

    bool Save();
    bool Load();
    // Startup options for a game (StartupOption entries with the user's or default values).
    std::vector<std::pair<std::string, int>> StartupOptions(const std::string& game);
    // True when a StartupOption changed since the game started (shows "restart to apply").
    bool NeedsRestart() const { return mNeedsRestart; }

    // Entry of the bound map, or a built-in display entry.
    const ModEntry* FindEntry(const std::string& id) const;

private:
    ModSettings();
    void ApplyAll(RecompProvider* provider, bool onlyPending);
    bool WriteToGame(RecompProvider* provider, const ModEntry& entry, float value);
    bool ReadFromGame(RecompProvider* provider, const ModEntry& entry, float& value) const;
    std::string SavePath() const;

    ModMap* mMap = nullptr;
    std::string mBoundGame;
    std::map<std::string, float> mUser;     // values the user picked (saved)
    std::map<std::string, bool> mApplied;   // written to the running game
    std::vector<ModEntry> mBuiltins;        // display.*
    bool mWasLive = false;
    bool mDirty = false;
    bool mNeedsRestart = false;
    int mSaveCountdown = 0;
    int mFrame = 0;
};
