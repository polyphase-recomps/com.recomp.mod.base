/**
 * @file ModBaseSceneGen.h
 * @brief Generate / update a Mod Settings scene from a ModMap (editor only).
 *
 * The scene is a Canvas ("ModSettings") with a panel: the title, one tab per group,
 * a page of rows per group, and Save / Reset / Close at the bottom. Rows by entry kind:
 *   Toggle, Choice   a button "Label: ON" / "Label: Fit" (A changes it)
 *   Int, Float       the label, the value, and - / + buttons
 *   Action           a button
 *   Display, Bar     text / a bar (read only)
 * A RecompMenuController makes it a gamepad menu: hidden at start, opened by its toggle
 * button (default Select), a PlayerInput action or the HOME menu, B closes it. The
 * "Open with" choice is applied on every run, also when updating an existing scene.
 *
 * Saved as Packages/<game>/Assets/Scenes/<scene name>.oct. Running it again updates
 * that scene without touching what is already there (nodes are matched by name):
 * new entries get rows, edited rows stay as they are.
 */
#pragma once

#include "ModBaseApi.h"

#include <string>

class ModMap;

struct ModSceneOptions
{
    std::string sceneName;     // default SC_<Title>ModSettings
    int position = 0;          // 0 centre, 1 left, 2 right
    bool includeDisplay = true;
    int toggleButton = -1;     // GamepadButtonCode, -1 = none (HOME menu / scripts only)
    std::string toggleAction;  // PlayerInput action "Category/Name", "" = none
};

#if EDITOR
MODBASE_API bool ModScene_Generate(ModMap* map, const ModSceneOptions& options, std::string& outMessage);
MODBASE_API std::string ModScene_DefaultName(const ModMap* map);
#endif
