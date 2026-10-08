/**
 * @file ModBaseExport.h
 * @brief A Mod Map's entries as a Recomp Zoo mod import ("polyphase-recomp-zoo/mods", schema
 *        version 1: RecompZoo public/schemas/mod-import.schema.json).
 *
 * Tools > Recomp > Export > Mods > Manifest writes it. Each entry keeps its id, which is the
 * player's settings key, and its field names and enum strings as the Mod Map has them. An entry
 * the Zoo would refuse (no id, a Choice whose default is not one of its values, a Bar without
 * its maximum, ...) is left out and reported, so the rest of the file still imports.
 */
#pragma once

#include "ModBaseApi.h"

#include <string>
#include <vector>

class ModMap;

struct ModZooExportOptions
{
    std::string gameId;            // the Zoo's game id: lowercase kebab-case ("super-smash-bros")
    std::string strategy = "merge"; // "merge" (update / add by id) or "replace" (the whole list)
};

struct ModZooExportResult
{
    std::string json;                  // the document ("" when it can't be made: see errors)
    int exported = 0;
    std::vector<std::string> skipped;  // "<id>: why", entries left out
    std::vector<std::string> notes;    // values adjusted so the Zoo accepts them
    std::vector<std::string> errors;   // the document as a whole (a bad game id)
};

MODBASE_API ModZooExportResult ModMap_ExportZoo(const ModMap& map, const ModZooExportOptions& options);

// "Super Smash Bros. (US)" -> "super-smash-bros": lowercase, (...) and [...] dropped, & = and,
// anything else not a letter or digit a single '-'.
MODBASE_API std::string ModZoo_Slug(const std::string& title);
MODBASE_API bool ModZoo_IsValidGameId(const std::string& id);
