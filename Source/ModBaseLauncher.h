/**
 * @file ModBaseLauncher.h
 * @brief A launcher for any recomp game: a front-end scene where the player sets their ROM
 *        (or disc image) up, opens the mod settings and starts the game.
 *
 * Runtimes whose games start from a file the player owns implement a RecompGameLauncher
 * per game and register it (com.recomp.n64 does, through its N64Launcher). Everything here
 * only talks to those, so the same launcher scene, widgets and Lua work on every runtime.
 *
 * A launcher needs no script:
 *   RecompLauncher (node)   put it in the launcher UI's root. It finds the game (Game, or
 *                           the only one registered), loads its mod settings, adds the Mods
 *                           Scene when the scene has no settings menu yet, starts the game a
 *                           frame after Play shows "Starting..." and then opens Game Scene.
 *   RecompButton Setting    @launcher:play, @launcher:browse (file dialog, then checks and
 *                           remembers the ROM), @launcher:forget, @launcher:mods (opens the
 *                           mod settings menu), @launcher:quit (packaged games)
 *   RecompText tokens       {@launcher.title}, {@launcher.rom} (path), {@launcher.romfile}
 *                           (file name, or "No ROM chosen"), {@launcher.message} (what
 *                           happened last), {@launcher.status} (idle / starting / running /
 *                           failed), {@launcher.ready} ("1" once Play can start the game)
 * Tools > Recomp > Mods > Launcher makes such a scene from a Mod Map, with the map's menu
 * style and its launcher settings (title, logo, background, labels, game scene).
 *
 * Scripts on the RecompLauncher node get OnRomChosen(node, path, ok), OnGameStarted(node)
 * and OnGameStartFailed(node, message), and the same signals (RomChosen, GameStarted,
 * GameStartFailed). Lua: Recomp.SetRomLocation / StartGame / ... (ModBaseLua.h).
 */
#pragma once

#include "ModBaseApi.h"

#include "AssetRef.h"
#include "Nodes/Widgets/Widget.h"

#include <string>
#include <vector>

class ModMap;

// One game a launcher can start. Implemented by the game's runtime.
class RecompGameLauncher
{
public:
    virtual ~RecompGameLauncher() = default;

    // "n64", "ps1", ...
    virtual const char* RuntimeId() const = 0;
    // "com.recomp.ssb64"
    virtual std::string GamePackage() const = 0;
    // "Super Smash Bros. (US)"
    virtual std::string GameTitle() const = 0;

    // The player's ROM (or disc image). Check: whether it is the game, with a message to show
    // (what it is, or why it won't do). Set: checks it, then remembers it for every start.
    virtual bool CheckRom(const std::string& path, std::string& message) = 0;
    virtual bool SetRomLocation(const std::string& path, std::string& message) = 0;
    // The remembered one, "" if none.
    virtual std::string GetRomLocation() = 0;
    virtual void ClearRomLocation() = 0;
    // The build ships the game's data itself (development builds): Play works without a ROM.
    virtual bool HasShippedData() { return false; }

    // Starts the game now (it may take a moment: a live recompile). False, with message
    // saying why, when it can't.
    virtual bool StartGame(std::string& message) = 0;
    virtual bool IsStarted() = 0;
    // The last start's message.
    virtual std::string LastMessage() = 0;
};

MODBASE_API void Recomp_RegisterLauncher(RecompGameLauncher* launcher);
MODBASE_API void Recomp_UnregisterLauncher(RecompGameLauncher* launcher);
// A game's launcher (nullptr / "": the first registered).
MODBASE_API RecompGameLauncher* Recomp_FindLauncher(const char* gamePackage = nullptr);
MODBASE_API const std::vector<RecompGameLauncher*>& Recomp_Launchers();

// A file dialog (Windows, Linux): the picked path, "" if cancelled or not available.
MODBASE_API std::string Recomp_BrowseForFile();
// Loads a game's mod settings (its Mod Map and the player's values), so ModSettings and the
// Mods table work before the game runs. False if the game has no Mod Map.
MODBASE_API bool Recomp_LoadMods(const std::string& gamePackage);

// {@launcher.<name>} tokens and @launcher:<command> buttons (see above). `from` finds the
// launcher of that UI (else the first one running).
MODBASE_API bool RecompLauncher_Token(const std::string& name, std::string& out);
MODBASE_API bool RecompLauncher_Command(const std::string& command, Node* from);

// Applies a map's menu style and launcher settings to a launcher UI (a generated scene's
// root or a live instance): background, logo, titles, button labels and which are shown.
MODBASE_API void ModLauncher_ApplyLook(Node* root, const ModMap& map);

class MODBASE_API RecompLauncher : public Widget
{
public:
    DECLARE_NODE(RecompLauncher, Widget);

    virtual void Start() override;
    virtual void Stop() override;
    virtual void Destroy() override;
    virtual void Tick(float deltaTime) override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    void Play();
    void Browse();
    void ForgetRom();
    void OpenMods();
    void Quit();

    RecompGameLauncher* Game() const;
    const std::string& GetMessage() const;
    // idle / starting / running / failed
    const char* GetStatus() const;
    // Play can start the game: a ROM is set, or the build ships the game's data.
    bool IsReady() const;

    void SetGame(const std::string& gamePackage);
    void SetGameScene(const AssetRef& scene);
    void SetModsScene(const AssetRef& scene);
    void SetAutoStart(bool autoStart);
    // Play's and Quit's sounds, the music while the launcher is up, its volume.
    void SetSounds(const AssetRef& start, const AssetRef& quit, const AssetRef& music, float musicVolume);
    // Deny (Play without a ROM, a ROM refused, a failed start) and Forget ROM's sounds.
    void SetMoreSounds(const AssetRef& deny, const AssetRef& forget);

    // The launcher of a UI (in `from`'s tree), else the first one running.
    static RecompLauncher* Find(Node* from = nullptr);

protected:
    void SetMessage(const std::string& message);
    void DescribeRom();
    void StartNow();
    void AddModsScene();

    std::string mGame;
    AssetRef mGameScene;
    AssetRef mModsScene;
    bool mAutoStart = false;
    bool mLoadMods = true;

    std::string mMessage;
    int32_t mStartCountdown = 0;
    float mStartWait = 0.0f; // the Start sound still playing (the game scene opens after it)
    float mQuitWait = -1.0f; // the Quit sound still playing (>= 0: quitting)
    AssetRef mSoundStart;
    AssetRef mSoundQuit;
    AssetRef mMusic;
    AssetRef mSoundDeny;
    AssetRef mSoundForget;
    float mMusicVolume = 0.7f;
    bool mMusicStarted = false;
    bool mSetUp = false;
    bool mFailed = false;
};
