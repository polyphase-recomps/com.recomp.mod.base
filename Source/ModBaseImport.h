/**
 * @file ModBaseImport.h
 * @brief Finds mod settings candidates automatically (editor only).
 *
 * Sources:
 *   Live game     every variable and request the running game publishes (any runtime)
 *   Game sources  the game package's Native C files, scanned for bridge tables without
 *                 running anything:
 *                   PortBridgeVar     { "name", &x, PB_S32, count, stride, "help" }  (PS1, N64, GBA)
 *                   PortBridgeRequest { "name", fn, "help" }
 *                   gcn_mod_variable("name", &x, GCN_VAR_S32, count, stride, "help")    (GameCube)
 *                   gcn_mod_request("name", fn, "help")
 *                   port_debug_values("name", ...) and game.json "options"  -> startup options
 *
 * Each candidate becomes a ModEntry with a guessed widget kind, label and group (see
 * ModImport_MakeEntry), which the user can change in the Mod Map Editor.
 */
#pragma once

#include "ModBaseModMap.h"
#include "ModBaseProvider.h"

#include <string>
#include <vector>

struct ModImportCandidate
{
    enum class What : uint8_t
    {
        Variable,
        Request,
        Option
    };
    What what = What::Variable;
    std::string name;
    std::string help;
    std::string origin;   // "live", "game/bridge.c:120", "game.json"
    RecompType type = RecompType::S32;
    int count = 1;
    int defaultValue = 0; // options
    bool selected = false;
};

#if EDITOR
std::vector<ModImportCandidate> ModImport_FromLive(RecompProvider* provider);
// packageDir: ...\Packages\<game>\ (trailing slash)
std::vector<ModImportCandidate> ModImport_FromSources(const std::string& packageDir);
// The text of one source scan, for tests: `file` is only used for "origin".
std::vector<ModImportCandidate> ModImport_ScanText(const std::string& text, const std::string& file);
std::vector<ModImportCandidate> ModImport_ScanOptions(const std::string& gameJson);
ModEntry ModImport_MakeEntry(const ModImportCandidate& candidate);
// "cheat_infhp" -> "Cheat Infhp" (snake / camel case to words)
std::string ModImport_Label(const std::string& name);
#endif
