/**
 * @file ComRecompModBase.cpp
 * @brief Native addon: com.recomp.mod.base
 *
 * The mod layer shared by every recomp runtime (com.recomp.ps1, com.recomp.gcn,
 * com.recomp.n64 / ssb64, com.recomp.gba):
 *   - RecompProvider: one API over each runtime's game bridge (ModBaseProvider.h)
 *   - ModMap assets: a game's mod settings (ModBaseModMap.h), edited and imported in
 *     Tools > Recomp > Mods > Mod Map Editor
 *   - ModSettings: the end user's values, applied to the game and saved (ModBaseSettings.h)
 *   - a generated Mod Settings UI scene (ModBaseSceneGen.h) built from Recomp* widgets
 *   - the resolution scaler players use (ModBaseDisplay.h)
 *   - game launchers and a generated Launcher scene (ModBaseLauncher.h)
 *   - Lua tables Recomp and Mods (ModBaseLua.h)
 */

#include "Plugins/PolyphasePluginAPI.h"
#include "Plugins/PolyphaseEngineAPI.h"
#if EDITOR
#include "Plugins/EditorUIHooks.h"
#endif

#include "ModBaseEditor.h"
#include "ModBaseLauncher.h"
#include "ModBaseLua.h"
#include "ModBaseModMap.h"
#include "ModBaseSettings.h"
#include "ModBaseWidgets.h"

static PolyphaseEngineAPI* sEngineAPI = nullptr;

static int OnLoad(PolyphaseEngineAPI* api)
{
    sEngineAPI = api;
    FORCE_LINK_CALL(ModMap);
    FORCE_LINK_CALL(RecompText);
    FORCE_LINK_CALL(RecompButton);
    FORCE_LINK_CALL(RecompBar);
    FORCE_LINK_CALL(RecompMenuController);
    FORCE_LINK_CALL(RecompLauncher);
    FORCE_LINK_CALL(RecompDisclaimer);
    if (api && api->LogDebug)
    {
        api->LogDebug("com.recomp.mod.base loaded");
    }
    return 0;
}

static void OnUnload()
{
    ModSettings::Get().Save();
#if EDITOR
    ModBaseEditor::Unregister();
#endif
    sEngineAPI = nullptr;
}

static void RegisterTypes(void* nodeFactory)
{
    (void)nodeFactory;
}

static void RegisterScriptFuncs(lua_State* L)
{
    ModBaseLua::Register(L, sEngineAPI);
}

#if EDITOR
static void RegisterEditorUI(EditorUIHooks* hooks, uint64_t hookId)
{
    ModBaseEditor::Register(hooks, hookId);
}
#endif

static int FillDesc(PolyphasePluginDesc* desc)
{
    desc->apiVersion = OCTAVE_PLUGIN_API_VERSION;
    desc->pluginName = "com.recomp.mod.base";
    desc->pluginVersion = "1.0.0";
    desc->OnLoad = OnLoad;
    desc->OnUnload = OnUnload;
    desc->Tick = nullptr;
    desc->TickEditor = nullptr;
    desc->RegisterTypes = RegisterTypes;
    desc->RegisterScriptFuncs = RegisterScriptFuncs;
#if EDITOR
    desc->RegisterEditorUI = RegisterEditorUI;
#else
    desc->RegisterEditorUI = nullptr;
#endif
    desc->OnEditorPreInit = nullptr;
    desc->OnEditorReady = nullptr;
    return 0;
}

#if EDITOR
extern "C" OCTAVE_PLUGIN_API int PolyphasePlugin_GetDesc(PolyphasePluginDesc* desc)
{
    return FillDesc(desc);
}
#else
extern "C" int PolyphasePlugin_GetDesc_com_recomp_mod_base(PolyphasePluginDesc* desc)
{
    return FillDesc(desc);
}
#endif
