/**
 * @file ModBaseLua.h
 * @brief Lua tables `Recomp` (the running game, any runtime) and `Mods` (mod settings).
 *
 *   Recomp.IsRunning()             true while a game runs and its bridge works
 *   Recomp.Game()                  package id of the running game, runtime id ("ps1", ...)
 *   Recomp.Get(name [, index])     a published variable (number or string), nil if unknown
 *   Recomp.Set(name, value [, i])  writes it; true if sent
 *   Recomp.Request(name, ...)      a game request with integer arguments: id or nil
 *   Recomp.Result(id)              its result once the game ran it, else nil
 *   Recomp.Read(address|symbol, type)       raw memory (PS1 / GameCube), type "s32", "u8", "f32"...
 *   Recomp.Write(address|symbol, type, v)
 *   Recomp.Variables() / Requests()          { {name=, type=, count=, help=}, ... }
 *   Recomp.SetInputBlocked(bool)   keep the gamepad away from the game (a script's own menu)
 *   Recomp.IsInputBlocked()
 *
 *   Launching (ModBaseLauncher.h; `game` = a package id, optional: else the only / first game):
 *   Recomp.Games()                      { {package=, title=, runtime=, rom=, started=}, ... }
 *   Recomp.SetRomLocation(path [, game])  checks the ROM and remembers it: ok, message
 *   Recomp.GetRomLocation([game])       the remembered ROM, or nil
 *   Recomp.ClearRomLocation([game])
 *   Recomp.CheckRom(path [, game])      ok, message (nothing saved)
 *   Recomp.BrowseForRom()               a file dialog: the path, or nil
 *   Recomp.LoadMods([game])             the game's mod settings, so Mods.* works before it runs
 *   Recomp.StartGame([game])            starts it now: ok, message (also Recomp.StartRecomp)
 *   Recomp.IsStarted([game])
 *   Recomp.LaunchStatus([game])         "idle" / "running", and the last start's message
 *
 *   Mods.Get(id)        the setting's value (number), nil if unknown
 *   Mods.Text(id)       as shown in menus ("ON", "Fit", "3")
 *   Mods.Set(id, v)     sets it (and writes it to the game)
 *   Mods.Step(id, dir)  toggle / +- step / next or previous choice / run an action
 *   Mods.Reset([id])    one setting, or all, back to the default
 *   Mods.Save()         saves now (it also saves by itself a moment after a change)
 *   Mods.List()         { {id=, label=, group=, kind=, help=}, ... }
 *   Mods.Open() / Close() / Toggle()   the generated settings menu (its RecompMenuController)
 *
 * The same scripts run on every recomp runtime; the runtime tables (Ps1, Gcn, N64) stay
 * for runtime-specific calls.
 */
#pragma once

struct lua_State;
struct PolyphaseEngineAPI;

namespace ModBaseLua
{
void Register(lua_State* L, PolyphaseEngineAPI* api);
}
