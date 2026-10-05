/**
 * @file ModBaseProvider.h
 * @brief One API over every recomp runtime's game bridge.
 *
 * Each runtime addon (com.recomp.ps1, com.recomp.gcn, com.recomp.n64/ssb64, com.recomp.gba)
 * implements a RecompProvider over its own bridge and registers it. Everything in
 * com.recomp.mod.base (mod settings, widgets, the Recomp Lua table, the editor windows)
 * only talks to providers, so it works the same on every runtime.
 *
 * Registering: create the provider once (a static object is fine) and call
 * Recomp_RegisterProvider from the addon's OnLoad, Recomp_UnregisterProvider from
 * OnUnload. The registry lives in function-local statics, so it doesn't matter which
 * addon's OnLoad runs first in shipped builds.
 */
#pragma once

#include "ModBaseApi.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Value types of game variables. Each runtime maps its own type codes to these (they
// differ: PS1/N64 code 7 is a string, GameCube code 7 is a float).
enum class RecompType : uint8_t
{
    U8,
    S8,
    U16,
    S16,
    U32,
    S32,
    F32,
    Str,
    Count
};

struct RecompValue
{
    bool isText = false;
    double number = 0.0;
    std::string text;

    static RecompValue Number(double v)
    {
        RecompValue r;
        r.number = v;
        return r;
    }
    static RecompValue Text(const std::string& t)
    {
        RecompValue r;
        r.isText = true;
        r.text = t;
        return r;
    }
};

struct RecompVarInfo
{
    std::string name;
    std::string help;
    RecompType type = RecompType::S32;
    int count = 1;         // array length (strings: number of strings)
    bool writable = true;
};

struct RecompRequestInfo
{
    std::string name;
    std::string help;
};

// What the player shows: the game's own frame size and the shape it should have on
// screen (4:3 for most consoles, 3:2 for the GBA). Used by the resolution scaler.
struct RecompFrameInfo
{
    int width = 0;
    int height = 0;
    float displayAspect = 4.0f / 3.0f;
};

class RecompProvider
{
public:
    virtual ~RecompProvider() = default;

    // "ps1", "gcn", "n64", "gba"
    virtual const char* RuntimeId() const = 0;
    // Package id of the game running (or last run), "" if none.
    virtual std::string GamePackage() const = 0;
    // True while a game runs and its bridge can be used.
    virtual bool IsLive() const = 0;

    virtual void Variables(std::vector<RecompVarInfo>& out) const = 0;
    virtual void Requests(std::vector<RecompRequestInfo>& out) const = 0;
    // A published variable (element `index` of arrays). False if unknown or not running.
    virtual bool Get(const std::string& name, int index, RecompValue& out) = 0;
    // Writes a published variable. Runtimes may queue it to the game thread: the new
    // value can take a frame to read back.
    virtual bool Set(const std::string& name, int index, const RecompValue& value) = 0;
    // Queues a game request with integer arguments: request id (> 0), or 0 if refused.
    virtual int Request(const std::string& name, const std::vector<int>& args) = 0;
    // The request's result once the game ran it.
    virtual bool Result(int id, int& result) = 0;

    // Optional: raw game memory (PS1, GameCube), by the console's own addresses.
    virtual bool ReadAddress(uint64_t /*address*/, RecompType /*type*/, RecompValue& /*out*/) { return false; }
    virtual bool WriteAddress(uint64_t /*address*/, RecompType /*type*/, const RecompValue& /*value*/) { return false; }
    // Optional: address of a decomp global by name (GameCube).
    virtual bool ResolveSymbol(const std::string& /*name*/, uint64_t& /*address*/) { return false; }
    // Optional: startup options (PS1 game.json "options"), applied when the game starts.
    virtual void SetStartupOptions(const std::vector<std::pair<std::string, int>>& /*options*/) {}
    // The frame the player shows (for the resolution scaler).
    virtual RecompFrameInfo FrameInfo() const { return RecompFrameInfo(); }
};

MODBASE_API void Recomp_RegisterProvider(RecompProvider* provider);
MODBASE_API void Recomp_UnregisterProvider(RecompProvider* provider);
// The provider of a game package ("" / nullptr: the live one, else the first registered).
MODBASE_API RecompProvider* Recomp_FindProvider(const char* gamePackage = nullptr);
MODBASE_API const std::vector<RecompProvider*>& Recomp_Providers();

// Gamepad capture: true while a RecompMenuController UI that captures input is open, or a
// script blocked the game's input (Recomp.SetInputBlocked). Players give the game no
// input then.
MODBASE_API bool Recomp_IsInputCaptured();
MODBASE_API void Recomp_SetInputBlocked(bool blocked);

MODBASE_API const char* Recomp_TypeName(RecompType type);
MODBASE_API RecompType Recomp_ParseType(const std::string& name); // "s32", "u8", "f32", "str"
MODBASE_API int Recomp_TypeSize(RecompType type);

// Helpers over the live provider.
MODBASE_API bool Recomp_GetNumber(const std::string& name, int index, double& value);
MODBASE_API bool Recomp_GetText(const std::string& name, int index, std::string& value);
