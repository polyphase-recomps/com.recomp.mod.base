/**
 * @file ModBaseEditor.h
 * @brief Editor windows under Tools > Recomp > Mods (editor builds only).
 *
 *   Mod Map Editor          pick / create a ModMap; edit its groups and entries; Import
 *                           (live game or game sources); Validate; Save; Generate scene
 *   Generate Mod Settings Scene...   builds / updates the settings UI scene of a map
 *   Live Variables          the running game's variables (live, editable) and requests;
 *                           "+" adds one to the open map
 *   Display Settings        the resolution scaler of the running player
 *
 * Also: Create Asset > Recomp > Mod Map, and the ModMap inspector.
 */
#pragma once

#include <cstdint>

struct EditorUIHooks;

namespace ModBaseEditor
{
void Register(EditorUIHooks* hooks, uint64_t hookId);
void Unregister();
}
