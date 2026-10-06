/**
 * @file ModBaseSceneGen.h
 * @brief Generate / update a Mod Settings scene from a ModMap (editor only).
 *
 * The scene is a Canvas ("ModSettings") with a responsive panel that fits any screen
 * (640x480 Wii / GameCube up to PC; the menu controller caps its size and centres it):
 *
 *   ModSettings (Canvas)              stays visible: hidden widgets don't tick
 *     Panel (Quad)                    shown / hidden by the controller
 *       Layout (ArrayWidget, column)
 *         Title
 *         TabStrip (ScrollContainer, sideways) > Tabs (ArrayWidget, row) > Tab_<group>
 *         Pages (fills the rest)
 *           Page_<group> (ScrollContainer) > List (ArrayWidget, column) > Row_<id>
 *         Footer (ArrayWidget, row)   Save / Reset to defaults / Close
 *         Note                        when an entry applies on restart
 *     MenuController
 *
 * Rows by entry kind (they stretch with the panel):
 *   Toggle, Choice   a button "Label: ON" / "Label: Fit" (A changes it)
 *   Int, Float       the label, the value, and - / + buttons
 *   Action           a button
 *   Display, Bar     text / a bar (read only)
 * Gamepad: left/right over the tabs switches pages, down/up through the rows (the list
 * scrolls to the selected one), the right stick scrolls a page, B closes. Opened by its
 * toggle button (default Select), a PlayerInput action or the HOME menu. The "Open with"
 * choice is applied on every run, also when updating an existing scene.
 *
 * Saved as Assets/Scenes/<scene name>.oct (the project's). Running it again updates
 * that scene without touching what is already there (nodes are matched by name):
 * new entries get rows, edited rows stay as they are. A scene made by the first,
 * fixed-size version (no Panel/Layout) gets its panel rebuilt.
 */
#pragma once

#include "ModBaseApi.h"

#include <string>

class ModMap;
struct ModStyle;

struct ModSceneOptions
{
    std::string sceneName;     // default SC_<Title>ModSettings
    int position = 0;          // 0 centre, 1 left, 2 right (when the screen is wider than the panel)
    bool includeDisplay = true;
    int toggleButton = -1;     // GamepadButtonCode, -1 = none (HOME menu / scripts only)
    std::string toggleAction;  // PlayerInput action "Category/Name", "" = none
};

#if EDITOR
MODBASE_API bool ModScene_Generate(ModMap* map, const ModSceneOptions& options, std::string& outMessage);
MODBASE_API std::string ModScene_DefaultName(const ModMap* map);
// Restyles the scene with the map's style only (no layout / navigation changes), and the
// instances of it open in the editor right now, so the change shows at once.
MODBASE_API bool ModScene_ApplyStyle(ModMap* map, const std::string& sceneName, std::string& outMessage);
// Restyles the settings UIs open in the editor (live preview); returns how many.
MODBASE_API int ModScene_RestyleOpen(const ModStyle& style);

// The game's launcher scene (ModBaseLauncher.h), Assets/Scenes/<name>.oct (the project's):
//
//   Launcher (Canvas)
//     Background (Quad)               full screen: the launcher's background picture / color
//     Panel (Quad)                    fitted by the controller (launcher panel size, position)
//       Layout (ArrayWidget, column)  Logo, Title, Subtitle, Rom ({@launcher.romfile}),
//                                     Message ({@launcher.message}), Buttons
//         Buttons (ArrayWidget)       Play, Browse, Forget, Mods, Quit (@launcher:...)
//     MenuController                  always shown, gamepad navigation, B doesn't close it
//     Launcher (RecompLauncher)       the game, its Game Scene, the Mods Scene
//
// Like the settings scene, running it again keeps what you changed (nodes matched by name)
// and applies the look (menu style + launcher settings) again. The Mods Scene is the map's
// generated settings scene when there is one.
MODBASE_API bool ModLauncher_Generate(ModMap* map, const std::string& sceneName, std::string& outMessage);
MODBASE_API std::string ModLauncher_DefaultName(const ModMap* map);
// Applies the map's look to the launcher scene and the launchers open in the editor.
MODBASE_API bool ModLauncher_ApplyLookToScene(ModMap* map, const std::string& sceneName, std::string& outMessage);
// The launcher UIs open in the editor (live preview); returns how many.
MODBASE_API int ModLauncher_RestyleOpen(const ModMap& map);
#endif
