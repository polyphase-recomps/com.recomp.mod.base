/**
 * @file ModBaseEditor.cpp
 * @brief Tools > Recomp > Mods windows (see ModBaseEditor.h).
 */

#include "ModBaseEditor.h"

#if EDITOR

#include "ModBaseDisplay.h"
#include "ModBaseExport.h"
#include "ModBaseImport.h"
#include "ModBaseModMap.h"
#include "ModBaseProvider.h"
#include "ModBaseSceneGen.h"
#include "ModBaseSettings.h"

#include "AssetDir.h"
#include "AssetManager.h"
#include "Assets/Font.h"
#include "Assets/Scene.h"
#include "Assets/SoundWave.h"
#include "Assets/Texture.h"
#include "Editor/EditorUtils.h"
#include "Engine.h"
#include "Input/InputTypes.h"
#include "Log.h"
#include "Nodes/Widgets/Quad.h"
#include "Plugins/EditorUIHooks.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// ModBaseWidgets.h (not included here: it pulls in the widget headers)
MODBASE_API std::vector<std::string> RecompInputActions();

namespace
{
EditorUIHooks* sHooks = nullptr;
uint64_t sHookId = 0;

const char* kEditorWindow = "recomp.mods.editor";
const char* kLiveWindow = "recomp.mods.live";
const char* kDisplayWindow = "recomp.mods.display";
const char* kStyleWindow = "recomp.mods.style";
const char* kLauncherWindow = "recomp.mods.launcher";
const char* kDisclaimerWindow = "recomp.mods.disclaimer";
const char* kExportWindow = "recomp.export.mods.manifest";
const char* kGenerateWindow = "recomp.mods.generate";
const char* kGenerateModal = "Generate Mod Settings Scene";

// OpenModal is a newer hook: older engines get a dockable window instead
#if defined(POLYPHASE_EDITOR_HOOKS_HAS_MODALS)
#define MODBASE_HAS_MODALS 1
#else
#define MODBASE_HAS_MODALS 0
#endif

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);
const ImVec4 kBad(1.0f, 0.5f, 0.4f, 1.0f);

// ---- state ------------------------------------------------------------------------------
std::string sMapName;          // the map open in the editor
int sSelected = -1;            // selected entry
std::string sStatus;           // last action's message
ImVec4 sStatusColor = kGood;

std::vector<ModImportCandidate> sCandidates;
char sImportFilter[64] = "";
int sImportSource = 0;         // 0 live, 1 sources

char sNewName[64] = "";
int sNewGame = 0;

ModSceneOptions sSceneOptions;
char sSceneName[96] = "";
int sToggleChoice = 1;
std::string sSceneMessage;

char sLiveFilter[64] = "";
std::vector<std::string> sLiveEdit;
std::vector<std::string> sRequestArgs;
std::vector<int> sRequestIds;

void SetStatus(const std::string& text, ImVec4 color = kGood)
{
    sStatus = text;
    sStatusColor = color;
}

ModMap* CurrentMap()
{
    if (sMapName.empty()) return nullptr;
    return LoadAsset<ModMap>(sMapName);
}

void MarkDirty(ModMap* map)
{
    if (map != nullptr) map->SetDirtyFlag();
}

bool InputString(const char* label, std::string& value, float width = -1.0f)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", value.c_str());
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, buf, sizeof(buf)))
    {
        value = buf;
        return true;
    }
    return false;
}

bool ContainsNoCase(const std::string& text, const char* filter)
{
    if (filter == nullptr || filter[0] == 0) return true;
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a.find(b) != std::string::npos;
}

// ---- game packages ----------------------------------------------------------------------
struct GamePackage
{
    std::string id;
    std::string title;
    std::string runtime;
    std::string dir; // ...\Packages\<id>\ (trailing slash)
};

bool ReadText(const std::string& path, std::string& text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

std::string JsonString(const std::string& text, const char* key)
{
    const std::string quoted = std::string("\"") + key + "\"";
    size_t p = text.find(quoted);
    if (p == std::string::npos) return std::string();
    p = text.find(':', p + quoted.size());
    if (p == std::string::npos) return std::string();
    p = text.find('"', p);
    if (p == std::string::npos) return std::string();
    const size_t e = text.find('"', p + 1);
    return e == std::string::npos ? std::string() : text.substr(p + 1, e - p - 1);
}

std::vector<GamePackage> FindGamePackages()
{
    std::vector<GamePackage> games;
#if defined(_WIN32)
    const std::string packages = GetEngineState()->mProjectDirectory + "Packages\\";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((packages + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return games;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        // A game package: it has Assets/game.json (PS1 games) or depends on a recomp
        // runtime in package.json (N64, GameCube, GBA games need no game.json).
        static const char* const kRuntimes[][2] = {
            {"com.recomp.ps1", "ps1"}, {"com.recomp.gcn", "gcn"}, {"com.recomp.gba", "gba"}, {"com.recomp.n64", "n64"}};
        const std::string id = fd.cFileName;
        bool isRuntime = id == "com.recomp.mod.base";
        for (const auto& r : kRuntimes) isRuntime = isRuntime || id == r[0];
        if (isRuntime) continue;
        const std::string dir = packages + id + "\\";
        std::string json, pkg;
        const bool hasGameJson = ReadText(dir + "Assets\\game.json", json);
        ReadText(dir + "package.json", pkg);
        std::string deps;
        const size_t depsAt = pkg.find("\"dependencies\"");
        if (depsAt != std::string::npos)
        {
            const size_t end = pkg.find_first_of("}]", depsAt);
            deps = pkg.substr(depsAt, end == std::string::npos ? std::string::npos : end - depsAt);
        }
        GamePackage g;
        g.id = id;
        g.dir = dir;
        for (const auto& r : kRuntimes)
        {
            if (deps.find(std::string("\"") + r[0] + "\"") != std::string::npos ||
                (hasGameJson && json.find(r[0]) != std::string::npos))
            {
                g.runtime = r[1];
                break;
            }
        }
        if (!hasGameJson && g.runtime.empty()) continue;
        // title: game.json "title", else package.json "displayName", else the start of its
        // description ("Snowboard Kids 2 (N64) compiled ..." -> "Snowboard Kids 2")
        g.title = JsonString(json, "title");
        if (g.title.empty()) g.title = JsonString(pkg, "displayName");
        if (g.title.empty())
        {
            const std::string description = JsonString(pkg, "description");
            const size_t cut = description.find(" (");
            if (cut != std::string::npos && cut > 0 && cut <= 40) g.title = description.substr(0, cut);
        }
        if (g.title.empty()) g.title = g.id;
        games.push_back(g);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#endif
    return games;
}

std::string PackageDir(const std::string& game)
{
    return GetEngineState()->mProjectDirectory + "Packages\\" + game + "\\";
}

AssetDir* PackageAssetDir(const std::string& game, const char* sub)
{
    AssetManager* am = AssetManager::Get();
    AssetDir* project = am ? am->FindProjectDirectory() : nullptr;
    AssetDir* packages = (project && project->mParentDir) ? project->mParentDir->GetSubdirectory("Packages") : nullptr;
    AssetDir* dir = packages ? packages->GetSubdirectory(game) : nullptr;
    if (dir == nullptr) dir = project;
    if (dir == nullptr) return nullptr;
    AssetDir* child = dir->GetSubdirectory(sub);
    return child ? child : dir->CreateSubdirectory(sub);
}

void SaveMap(ModMap* map)
{
    if (map == nullptr) return;
    AssetStub* stub = FetchAssetStub(map->GetName());
    if (stub == nullptr)
    {
        SetStatus("Cannot find the asset of " + map->GetName(), kBad);
        return;
    }
    AssetManager::Get()->SaveAsset(*stub);
    SetStatus("Saved " + map->GetName());
}

ModMap* CreateMap(const std::string& name, AssetDir* dir, const GamePackage* game)
{
    if (dir == nullptr)
    {
        SetStatus("No folder to create the Mod Map in.", kBad);
        return nullptr;
    }
    AssetStub* stub = EditorAddUniqueAsset(name.c_str(), dir, ModMap::GetStaticType(), true);
    ModMap* map = (stub && stub->mAsset) ? stub->mAsset->As<ModMap>() : nullptr;
    if (map == nullptr)
    {
        SetStatus("Cannot create the Mod Map asset.", kBad);
        return nullptr;
    }
    if (game != nullptr)
    {
        map->mGame = game->id;
        map->mRuntime = game->runtime;
        map->mTitle = game->title + " Mods";
    }
    AssetManager::Get()->SaveAsset(*stub);
    sMapName = stub->mName;
    sSelected = -1;
    SetStatus("Created " + stub->mName);
    return map;
}

// ---- Mod Map Editor ----------------------------------------------------------------------
void DrawEntryDetails(ModMap* map, ModEntry& e)
{
    bool changed = false;
    changed |= InputString("Id", e.mId, 260.0f);
    changed |= InputString("Label", e.mLabel, 260.0f);

    // group: a combo of the map's groups, or a new name
    std::vector<std::string> groups = map->OrderedGroups();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##groupcombo", e.mGroup.empty() ? "(none)" : e.mGroup.c_str()))
    {
        for (const std::string& g : groups)
        {
            if (ImGui::Selectable(g.empty() ? "(none)" : g.c_str(), g == e.mGroup))
            {
                e.mGroup = g;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    changed |= InputString("Group", e.mGroup, 100.0f);
    changed |= InputString("Help", e.mHelp, 360.0f);

    int kind = (int)e.mKind;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("Widget", &kind, "Toggle\0Int\0Float\0Choice\0Action\0Display\0Bar\0"))
    {
        e.mKind = (ModKind)kind;
        changed = true;
    }
    int source = (int)e.mSource;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("Source", &source, "Variable\0Address\0Symbol\0Request\0Startup Option\0Display\0"))
    {
        e.mSource = (ModSource)source;
        changed = true;
    }

    switch (e.mSource)
    {
    case ModSource::Variable:
        changed |= InputString("Variable", e.mName, 220.0f);
        ImGui::SetNextItemWidth(100.0f);
        changed |= ImGui::InputInt("Index", &e.mIndex);
        break;
    case ModSource::Address:
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)e.mAddress);
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputText("Address", buf, sizeof(buf)))
        {
            e.mAddress = strtoull(buf, nullptr, 0);
            changed = true;
        }
    }
        // fall through to the type
    case ModSource::Symbol:
    {
        if (e.mSource == ModSource::Symbol) changed |= InputString("Symbol", e.mName, 220.0f);
        int type = (int)e.mType;
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("Type", &type, "u8\0s8\0u16\0s16\0u32\0s32\0f32\0"))
        {
            e.mType = (RecompType)type;
            changed = true;
        }
        break;
    }
    case ModSource::Request:
        changed |= InputString("Request", e.mName, 220.0f);
        changed |= InputString("Arguments", e.mArgs, 160.0f);
        break;
    case ModSource::StartupOption:
        changed |= InputString("Option", e.mName, 220.0f);
        break;
    case ModSource::Display:
        changed |= InputString("Setting", e.mName, 220.0f);
        break;
    default:
        break;
    }

    if (e.mKind == ModKind::Int || e.mKind == ModKind::Float || e.mKind == ModKind::Toggle)
    {
        ImGui::SetNextItemWidth(260.0f);
        float range[3] = {e.mMin, e.mMax, e.mStep};
        if (ImGui::InputFloat3("Min / Max / Step", range))
        {
            e.mMin = range[0];
            e.mMax = range[1];
            e.mStep = range[2];
            changed = true;
        }
    }
    if (e.mKind != ModKind::Action && e.mKind != ModKind::Display && e.mKind != ModKind::Bar)
    {
        ImGui::SetNextItemWidth(120.0f);
        changed |= ImGui::InputFloat("Default", &e.mDefault);
    }
    if (e.mKind == ModKind::Choice)
    {
        ImGui::TextUnformatted("Choices");
        e.mChoiceValues.resize(e.mChoiceLabels.size());
        for (size_t i = 0; i < e.mChoiceLabels.size(); ++i)
        {
            ImGui::PushID((int)i);
            changed |= InputString("##label", e.mChoiceLabels[i], 160.0f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            changed |= ImGui::InputFloat("##value", &e.mChoiceValues[i]);
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
            {
                e.mChoiceLabels.erase(e.mChoiceLabels.begin() + i);
                e.mChoiceValues.erase(e.mChoiceValues.begin() + i);
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Choice"))
        {
            e.mChoiceLabels.push_back("Choice " + std::to_string(e.mChoiceLabels.size()));
            e.mChoiceValues.push_back((float)e.mChoiceValues.size());
            changed = true;
        }
    }
    if (e.mKind == ModKind::Display)
    {
        changed |= InputString("Format", e.mFormat, 260.0f);
        ImGui::TextDisabled("{v} = the value; other {tokens} work too, e.g. {hp}/{hp_max}");
    }
    if (e.mKind == ModKind::Bar)
    {
        changed |= InputString("Max Variable", e.mMaxName, 220.0f);
    }
    changed |= ImGui::Checkbox("Save with the user's settings", &e.mPersist);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Lock", &e.mLock);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Re-applies the user's value whenever the game changes it (cheats).");
    }

    // live value
    RecompProvider* provider = Recomp_FindProvider(map->mGame.c_str());
    if (provider != nullptr && provider->IsLive() && ModSettings::Get().GetMap() == map)
    {
        ImGui::TextColored(kGood, "Live: %s", ModSettings::Get().ValueText(e.mId).c_str());
    }
    if (changed) MarkDirty(map);
}

void DrawImportPopup(ModMap* map)
{
    if (!ImGui::BeginPopupModal("Import##mods", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    ImGui::RadioButton("Running game", &sImportSource, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Game sources (no game needed)", &sImportSource, 1);
    ImGui::SameLine();
    if (ImGui::Button("Scan"))
    {
        sCandidates = sImportSource == 0 ? ModImport_FromLive(Recomp_FindProvider(map->mGame.c_str()))
                                         : ModImport_FromSources(PackageDir(map->mGame));
        for (ModImportCandidate& c : sCandidates)
        {
            c.selected = map->Find(c.name) == nullptr && c.name.compare(0, 6, "cheat_") == 0;
        }
        if (sCandidates.empty())
        {
            SetStatus(sImportSource == 0 ? "No game running (or it publishes nothing yet)."
                                         : "Nothing found in Packages/" + map->mGame + "/Native.",
                      kWarn);
        }
    }
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter", sImportFilter, sizeof(sImportFilter));
    ImGui::SameLine();
    if (ImGui::SmallButton("All")) for (auto& c : sCandidates) if (ContainsNoCase(c.name, sImportFilter)) c.selected = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) for (auto& c : sCandidates) c.selected = false;

    if (ImGui::BeginTable("##candidates", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders,
                          ImVec2(760.0f, 340.0f)))
    {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Help", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < sCandidates.size(); ++i)
        {
            ModImportCandidate& c = sCandidates[i];
            if (!ContainsNoCase(c.name + " " + c.help, sImportFilter)) continue;
            ImGui::TableNextRow();
            ImGui::PushID((int)i);
            ImGui::TableNextColumn();
            ImGui::Checkbox("##sel", &c.selected);
            ImGui::TableNextColumn();
            const bool exists = map->Find(c.name) != nullptr;
            if (exists) ImGui::TextDisabled("%s", c.name.c_str());
            else ImGui::TextUnformatted(c.name.c_str());
            ImGui::TableNextColumn();
            const char* what = c.what == ModImportCandidate::What::Request ? "request"
                               : c.what == ModImportCandidate::What::Option ? "option"
                                                                             : Recomp_TypeName(c.type);
            ImGui::Text("%s%s", what, c.count > 1 ? " []" : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(exists ? "(already in the map)" : c.help.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", c.origin.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Add selected", ImVec2(140, 0)))
    {
        int added = 0;
        for (const ModImportCandidate& c : sCandidates)
        {
            if (!c.selected || map->Find(c.name) != nullptr) continue;
            map->mEntries.push_back(ModImport_MakeEntry(c));
            const std::string& g = map->mEntries.back().mGroup;
            if (std::find(map->mGroups.begin(), map->mGroups.end(), g) == map->mGroups.end()) map->mGroups.push_back(g);
            ++added;
        }
        MarkDirty(map);
        SetStatus("Added " + std::to_string(added) + " entries. Check their widget kinds and ranges, then Save.");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void DrawNewMapPopup()
{
    if (!ImGui::BeginPopupModal("New Mod Map##mods", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    static std::vector<GamePackage> games;
    if (ImGui::IsWindowAppearing()) games = FindGamePackages();
    if (games.empty())
    {
        ImGui::TextColored(kWarn, "No game package in this project (a package that depends on com.recomp.ps1, "
                                  "com.recomp.gcn, com.recomp.gba or com.recomp.n64, or has Assets/game.json).");
    }
    else
    {
        sNewGame = std::min(sNewGame, (int)games.size() - 1);
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo("Game", games[sNewGame].title.c_str()))
        {
            for (int i = 0; i < (int)games.size(); ++i)
            {
                if (ImGui::Selectable((games[i].title + "  (" + games[i].id + ")").c_str(), i == sNewGame)) sNewGame = i;
            }
            ImGui::EndCombo();
        }
        if (sNewName[0] == 0 || ImGui::IsWindowAppearing())
        {
            std::string name = "MM_";
            for (char c : games[sNewGame].title) if (isalnum((unsigned char)c)) name += c;
            snprintf(sNewName, sizeof(sNewName), "%s", name.c_str());
        }
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputText("Name", sNewName, sizeof(sNewName));
        ImGui::TextDisabled("Saved in Packages/%s/Assets/ModMaps", games[sNewGame].id.c_str());
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            CreateMap(sNewName, PackageAssetDir(games[sNewGame].id, "ModMaps"), &games[sNewGame]);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void Validate(ModMap* map)
{
    std::vector<std::string> problems;
    std::vector<std::string> ids;
    RecompProvider* provider = Recomp_FindProvider(map->mGame.c_str());
    const bool live = provider != nullptr && provider->IsLive();
    std::vector<RecompVarInfo> vars;
    std::vector<RecompRequestInfo> requests;
    std::vector<ModImportCandidate> scanned;
    if (live)
    {
        provider->Variables(vars);
        provider->Requests(requests);
    }
    else
    {
        scanned = ModImport_FromSources(PackageDir(map->mGame));
    }
    auto known = [&](const std::string& name, bool request) {
        if (live)
        {
            if (request)
            {
                for (auto& r : requests) if (r.name == name) return true;
                return name.compare(0, 4, "set ") == 0;
            }
            for (auto& v : vars) if (v.name == name) return true;
            return false;
        }
        for (auto& c : scanned)
        {
            if (c.name == name && (c.what == ModImportCandidate::What::Request) == request) return true;
        }
        return false;
    };
    for (const ModEntry& e : map->mEntries)
    {
        if (e.mId.empty()) problems.push_back("an entry has no id");
        if (std::find(ids.begin(), ids.end(), e.mId) != ids.end()) problems.push_back("duplicate id " + e.mId);
        ids.push_back(e.mId);
        if (e.mSource == ModSource::Variable && !known(e.mName, false))
            problems.push_back(e.mId + ": no variable " + e.mName);
        if (e.mSource == ModSource::Request && !known(e.mName, true))
            problems.push_back(e.mId + ": no request " + e.mName);
        if (e.mKind == ModKind::Choice && e.mChoiceLabels.empty()) problems.push_back(e.mId + ": no choices");
        if ((e.mKind == ModKind::Int || e.mKind == ModKind::Float) && e.mMax <= e.mMin)
            problems.push_back(e.mId + ": max is not above min");
    }
    if (problems.empty())
    {
        SetStatus(std::string("No problems found (checked against ") + (live ? "the running game" : "the game sources") + ").");
    }
    else
    {
        std::string text = std::to_string(problems.size()) + " problem(s): ";
        for (size_t i = 0; i < problems.size() && i < 8; ++i) text += (i ? "; " : "") + problems[i];
        SetStatus(text, kWarn);
    }
}

void OpenGenerate(const std::string& mapName);

void DrawModMapEditor(void*)
{
    // map picker
    std::vector<ModMap*> maps = ModMap_FindAll();
    ModMap* map = CurrentMap();
    if (map == nullptr && !maps.empty())
    {
        map = maps.front();
        sMapName = map->GetName();
    }
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Mod Map", map ? map->GetName().c_str() : "(none)"))
    {
        for (ModMap* m : maps)
        {
            const std::string label = m->GetName() + "  (" + m->mGame + ")";
            if (ImGui::Selectable(label.c_str(), m == map))
            {
                sMapName = m->GetName();
                sSelected = -1;
                map = m;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New...")) ImGui::OpenPopup("New Mod Map##mods");
    DrawNewMapPopup();
    if (map == nullptr)
    {
        ImGui::TextWrapped("No Mod Map yet. New... creates one for a game package; Import then fills it from the "
                           "running game or the game's sources.");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) SaveMap(map);
    ImGui::SameLine();
    if (ImGui::Button("Import...")) ImGui::OpenPopup("Import##mods");
    ImGui::SameLine();
    if (ImGui::Button("Validate")) Validate(map);
    ImGui::SameLine();
    if (ImGui::Button("Generate Scene...")) OpenGenerate(map->GetName());
    if (map->GetDirtyFlag())
    {
        ImGui::SameLine();
        ImGui::TextColored(kWarn, "(unsaved)");
    }
    DrawImportPopup(map);

    bool changed = false;
    if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Game: %s   Runtime: %s", map->mGame.c_str(), map->mRuntime.empty() ? "?" : map->mRuntime.c_str());
        changed |= InputString("Title", map->mTitle, 260.0f);
        changed |= InputString("Save Name", map->mSaveName, 160.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("file: %s.mods", map->SaveName().c_str());
    }
    if (changed) MarkDirty(map);

    // entries: list on the left, details on the right
    ImGui::Separator();
    ImGui::BeginChild("##entries", ImVec2(280.0f, 0.0f), true);
    if (ImGui::SmallButton("+ Add"))
    {
        ModEntry e;
        e.mId = "setting" + std::to_string(map->mEntries.size() + 1);
        e.mLabel = "New Setting";
        e.mGroup = map->mGroups.empty() ? std::string("Settings") : map->mGroups.front();
        map->mEntries.push_back(e);
        sSelected = (int)map->mEntries.size() - 1;
        MarkDirty(map);
    }
    const bool valid = sSelected >= 0 && sSelected < (int)map->mEntries.size();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy") && valid)
    {
        ModEntry e = map->mEntries[sSelected];
        e.mId += "_copy";
        map->mEntries.insert(map->mEntries.begin() + sSelected + 1, e);
        ++sSelected;
        MarkDirty(map);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Up") && valid && sSelected > 0)
    {
        std::swap(map->mEntries[sSelected], map->mEntries[sSelected - 1]);
        --sSelected;
        MarkDirty(map);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Down") && valid && sSelected + 1 < (int)map->mEntries.size())
    {
        std::swap(map->mEntries[sSelected], map->mEntries[sSelected + 1]);
        ++sSelected;
        MarkDirty(map);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete") && valid)
    {
        map->mEntries.erase(map->mEntries.begin() + sSelected);
        sSelected = std::min(sSelected, (int)map->mEntries.size() - 1);
        MarkDirty(map);
    }
    for (const std::string& group : map->OrderedGroups())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", group.empty() ? "(no group)" : group.c_str());
        for (int i = 0; i < (int)map->mEntries.size(); ++i)
        {
            const ModEntry& e = map->mEntries[i];
            if (e.mGroup != group) continue;
            const std::string label = "  " + (e.mLabel.empty() ? e.mId : e.mLabel) + "  [" + ModKindName(e.mKind) + "]##" +
                                      std::to_string(i);
            if (ImGui::Selectable(label.c_str(), i == sSelected)) sSelected = i;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f), true);
    if (sSelected >= 0 && sSelected < (int)map->mEntries.size())
    {
        DrawEntryDetails(map, map->mEntries[sSelected]);
    }
    else
    {
        ImGui::TextDisabled("Select an entry, or Import from the game.");
    }
    ImGui::EndChild();

    if (!sStatus.empty())
    {
        ImGui::TextColored(sStatusColor, "%s", sStatus.c_str());
    }
}

// ---- Generate scene ------------------------------------------------------------------------
const int kToggleCodes[] = {-1, GAMEPAD_SELECT, GAMEPAD_THUMBR, GAMEPAD_THUMBL, GAMEPAD_Z};
const char* const kToggleNames[] = {"None (HOME menu / scripts)", "Select", "Right stick click", "Left stick click", "Z"};
const int kToggleCount = (int)(sizeof(kToggleCodes) / sizeof(kToggleCodes[0]));
std::string sToggleAction; // a PlayerInput action instead of a button ("" = use sToggleChoice)

// "Open with": the gamepad buttons, then the project's PlayerInput actions (when the engine
// exports PlayerInputSystem and the project has actions).
void DrawOpenWith()
{
    const std::string preview = !sToggleAction.empty() ? "Action: " + sToggleAction
                                                       : kToggleNames[std::max(0, std::min(sToggleChoice, kToggleCount - 1))];
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Open with", preview.c_str()))
    {
        for (int i = 0; i < kToggleCount; ++i)
        {
            if (ImGui::Selectable(kToggleNames[i], sToggleAction.empty() && sToggleChoice == i))
            {
                sToggleChoice = i;
                sToggleAction.clear();
            }
        }
        const std::vector<std::string> actions = RecompInputActions();
        ImGui::Separator();
        if (actions.empty())
        {
            ImGui::TextDisabled("No PlayerInput actions (needs project input actions and an engine that exports them)");
        }
        for (const std::string& a : actions)
        {
            if (ImGui::Selectable(("Action: " + a).c_str(), sToggleAction == a))
            {
                sToggleAction = a;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Gamepad buttons are read from controller 1 (Select = Back on XInput).\n"
                          "An action uses its PlayerInput bindings, so keyboard keys work too.\n"
                          "Play logs \"Recomp menu '<title>': opens with ...\" when the menu is in the scene.");
    }
}

bool DrawGenerate(void*)
{
    std::vector<ModMap*> maps = ModMap_FindAll();
    ModMap* map = CurrentMap();
    if (map == nullptr && !maps.empty()) map = maps.front();
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo("Mod Map", map ? map->GetName().c_str() : "(none)"))
    {
        for (ModMap* m : maps)
        {
            if (ImGui::Selectable(m->GetName().c_str(), m == map))
            {
                sMapName = m->GetName();
                map = m;
                snprintf(sSceneName, sizeof(sSceneName), "%s", ModScene_DefaultName(map).c_str());
            }
        }
        ImGui::EndCombo();
    }
    if (map == nullptr)
    {
        ImGui::TextColored(kWarn, "Create a Mod Map first (Tools > Recomp > Mods > Mod Map Editor).");
        return !ImGui::Button("Close");
    }
    if (sSceneName[0] == 0) snprintf(sSceneName, sizeof(sSceneName), "%s", ModScene_DefaultName(map).c_str());
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("Scene", sSceneName, sizeof(sSceneName));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Position", &sSceneOptions.position, "Centre\0Left\0Right\0");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Where the panel sits on screens wider than it (it fills small ones, e.g. Wii / GameCube).");
    }
    DrawOpenWith();
    ImGui::Checkbox("Include Display settings (resolution scaler)", &sSceneOptions.includeDisplay);
    ImGui::TextDisabled("Saved in Packages/%s/Assets/Scenes. Generating again updates it: nodes you changed stay.",
                        map->mGame.c_str());
    if (!sSceneMessage.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextWrapped("%s", sSceneMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    bool keep = true;
    if (ImGui::Button(FetchAssetStub(sSceneName) ? "Update" : "Generate", ImVec2(120, 0)))
    {
        if (map->GetDirtyFlag()) SaveMap(map);
        sSceneOptions.sceneName = sSceneName;
        sSceneOptions.toggleButton = sToggleAction.empty() ? kToggleCodes[std::max(0, std::min(sToggleChoice, kToggleCount - 1))] : -1;
        sSceneOptions.toggleAction = sToggleAction;
        ModScene_Generate(map, sSceneOptions, sSceneMessage);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 0))) keep = false;
    return keep;
}

void OpenGenerate(const std::string& mapName)
{
    if (!mapName.empty()) sMapName = mapName;
    sSceneMessage.clear();
    sSceneName[0] = 0;
#if MODBASE_HAS_MODALS
    if (sHooks != nullptr && sHooks->OpenModal != nullptr)
    {
        sHooks->OpenModal(sHookId, kGenerateModal, DrawGenerate, nullptr);
        return;
    }
#endif
    if (sHooks != nullptr && sHooks->OpenWindow != nullptr)
    {
        sHooks->OpenWindow(kGenerateWindow);
    }
}

// ---- Live Variables ----------------------------------------------------------------------
void DrawLiveVariables(void*)
{
    RecompProvider* provider = Recomp_FindProvider();
    if (provider == nullptr)
    {
        ImGui::TextWrapped("No recomp runtime is loaded.");
        return;
    }
    const bool live = provider->IsLive();
    ImGui::Text("%s  [%s]  %s", provider->GamePackage().c_str(), provider->RuntimeId(), live ? "running" : "not running");
    if (!live)
    {
        ImGui::TextDisabled("Play a scene with the game's player node to see its variables.");
        return;
    }
    ModMap* map = CurrentMap();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter", sLiveFilter, sizeof(sLiveFilter));
    if (map) { ImGui::SameLine(); ImGui::TextDisabled("+ adds to %s", map->GetName().c_str()); }

    std::vector<RecompVarInfo> vars;
    provider->Variables(vars);
    sLiveEdit.resize(vars.size());
    if (ImGui::BeginTable("##vars", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders,
                          ImVec2(0.0f, 320.0f)))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn("Set", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Help", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < vars.size(); ++i)
        {
            const RecompVarInfo& v = vars[i];
            if (!ContainsNoCase(v.name + " " + v.help, sLiveFilter)) continue;
            ImGui::TableNextRow();
            ImGui::PushID((int)i);
            ImGui::TableNextColumn();
            if (map != nullptr && map->Find(v.name) == nullptr)
            {
                if (ImGui::SmallButton("+"))
                {
                    ModImportCandidate c;
                    c.name = v.name;
                    c.help = v.help;
                    c.type = v.type;
                    c.count = v.count;
                    map->mEntries.push_back(ModImport_MakeEntry(c));
                    MarkDirty(map);
                }
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(v.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", Recomp_TypeName(v.type), v.count > 1 ? "[]" : "");
            ImGui::TableNextColumn();
            RecompValue value;
            if (provider->Get(v.name, 0, value))
            {
                if (value.isText) ImGui::TextUnformatted(value.text.c_str());
                else ImGui::Text("%.10g", value.number);
            }
            else
            {
                ImGui::TextDisabled("--");
            }
            ImGui::TableNextColumn();
            if (v.writable && v.type != RecompType::Str)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%s", sLiveEdit[i].c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("##v", buf, sizeof(buf))) sLiveEdit[i] = buf;
                ImGui::SameLine();
                if (ImGui::SmallButton("Set") && !sLiveEdit[i].empty())
                {
                    provider->Set(v.name, 0, RecompValue::Number(atof(sLiveEdit[i].c_str())));
                }
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", v.help.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    std::vector<RecompRequestInfo> requests;
    provider->Requests(requests);
    sRequestArgs.resize(requests.size());
    sRequestIds.resize(requests.size(), 0);
    ImGui::TextUnformatted("Requests");
    for (size_t i = 0; i < requests.size(); ++i)
    {
        const RecompRequestInfo& r = requests[i];
        ImGui::PushID(1000 + (int)i);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", sRequestArgs[i].c_str());
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputText("##args", buf, sizeof(buf))) sRequestArgs[i] = buf;
        ImGui::SameLine();
        if (ImGui::SmallButton(r.name.c_str()))
        {
            std::vector<int> args;
            std::istringstream in(sRequestArgs[i]);
            std::string word;
            while (std::getline(in, word, ','))
            {
                if (!word.empty()) args.push_back((int)strtol(word.c_str(), nullptr, 0));
            }
            sRequestIds[i] = provider->Request(r.name, args);
        }
        int result = 0;
        ImGui::SameLine();
        if (sRequestIds[i] != 0 && provider->Result(sRequestIds[i], result))
        {
            ImGui::TextColored(result < 0 ? kWarn : kGood, "-> %d", result);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", r.help.c_str());
        ImGui::PopID();
    }
}

// ---- Display settings ----------------------------------------------------------------------
void DrawDisplaySettings(void*)
{
    ModSettings& settings = ModSettings::Get();
    for (const char* id : {"display.mode", "display.scale", "display.filter", "display.resolution", "display.window"})
    {
        if (std::strcmp(id, "display.resolution") == 0 && Recomp_MaxResolution() <= 1)
        {
            continue; // no runtime here can draw larger
        }
        const ModEntry* e = settings.FindEntry(id);
        if (e == nullptr) continue;
        float value = 0.0f;
        settings.GetValue(id, value);
        ImGui::PushID(id);
        ImGui::SetNextItemWidth(200.0f);
        if (e->mKind == ModKind::Choice)
        {
            int current = (int)value;
            if (ImGui::BeginCombo(e->mLabel.c_str(), settings.ValueText(id).c_str()))
            {
                for (size_t i = 0; i < e->mChoiceLabels.size(); ++i)
                {
                    if (ImGui::Selectable(e->mChoiceLabels[i].c_str(), (int)i == current)) settings.SetValue(id, (float)i);
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            int v = (int)value;
            if (ImGui::SliderInt(e->mLabel.c_str(), &v, (int)e->mMin, (int)e->mMax)) settings.SetValue(id, (float)v);
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Saved with the user's mod settings. Fit keeps the game's shape; Integer gives sharp,\n"
                        "evenly sized pixels; Native is 1:1; Full Screen stretches. Window sizes apply to\n"
                        "packaged Windows builds (the editor keeps its own window).");
    if (RecompProvider* provider = Recomp_FindProvider())
    {
        const RecompFrameInfo info = provider->FrameInfo();
        if (info.width > 0)
        {
            const float vw = (float)GetEngineState()->mWindowWidth, vh = (float)GetEngineState()->mWindowHeight;
            const RecompRect r = Recomp_DisplayFit(info.width, info.height, info.displayAspect, vw, vh, 1.0f,
                                                   Recomp_DisplaySettings());
            ImGui::Text("Game frame %dx%d -> %.0fx%.0f at (%.0f, %.0f) in a %.0fx%.0f screen", info.width,
                        info.height, r.w, r.h, r.x, r.y, vw, vh);
        }
    }
}

// ---- Menu Style ----------------------------------------------------------------------------
char sStyleScene[128] = "";
bool sStyleLive = true;
std::string sStyleMessage;

// A project asset of one type: a filterable list, "(none)" clears; an asset dragged from the
// asset browser can be dropped on it.
bool AssetPicker(const char* label, AssetRef& ref, TypeId type)
{
    Asset* current = ref.Get();
    bool changed = false;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo(label, current ? current->GetName().c_str() : "(none)", ImGuiComboFlags_HeightLarge))
    {
        static char filter[64] = "";
        if (ImGui::IsWindowAppearing())
        {
            filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "filter", filter, sizeof(filter));
        if (ImGui::Selectable("(none)", current == nullptr))
        {
            ref = (const Asset*)nullptr;
            changed = true;
        }
        std::vector<std::string> names;
        for (const auto& kv : AssetManager::Get()->GetAssetMap())
        {
            if (kv.second != nullptr && kv.second->mType == type) names.push_back(kv.first);
        }
        std::sort(names.begin(), names.end());
        std::string f = filter;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        for (const std::string& name : names)
        {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (!f.empty() && lower.find(f) == std::string::npos) continue;
            if (ImGui::Selectable(name.c_str(), current != nullptr && current->GetName() == name))
            {
                ref = LoadAsset(name);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_ASSET"))
        {
            AssetStub* stub = *(AssetStub**)payload->Data;
            if (stub != nullptr && stub->mType == type)
            {
                ref = LoadAsset(stub->mName);
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

// The style window's Update Scene: saves the style with the map, restyles the scene.
void UpdateStyledScene(ModMap* map)
{
    SaveMap(map);
    ModScene_ApplyStyle(map, sStyleScene, sStyleMessage);
    // the launcher follows the menu style too
    if (FetchAssetStub(ModLauncher_DefaultName(map)) != nullptr)
    {
        std::string launcher;
        ModLauncher_ApplyLookToScene(map, "", launcher);
        sStyleMessage += "\n" + launcher;
    }
}

// The engine's Crop Texture (its texture crop editor, which addons can't open themselves): a
// Quad of our own, never in a level, carries the texture and the UVs through it.
bool CropTextureButton(int slot, Texture* texture, glm::vec2& uvScale, glm::vec2& uvOffset)
{
    constexpr int kSlots = 4;
    static SharedPtr<Quad> sCropQuads[kSlots];
    // what the quad was last given or gave back: a change typed into the fields goes to it, a
    // crop applied in the editor (its callback sets the quad, maybe while another button draws
    // the editor's window) comes back from it
    static glm::vec2 sSynced[kSlots][2];
    slot = slot < 0 ? 0 : (slot >= kSlots ? kSlots - 1 : slot);
    ImGui::PushID(slot);
    struct PopId
    {
        ~PopId() { ImGui::PopID(); }
    } popId;
    if (texture == nullptr)
    {
        ImGui::BeginDisabled();
        ImGui::Button("Crop Texture");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Pick the Normal texture first.");
        }
        return false;
    }
    if (sCropQuads[slot].Get() == nullptr)
    {
        sCropQuads[slot] = Node::Construct<Quad>();
        sSynced[slot][0] = sCropQuads[slot]->GetUvScale();
        sSynced[slot][1] = sCropQuads[slot]->GetUvOffset();
    }
    Quad* quad = sCropQuads[slot].Get();
    if (quad->GetTexture() != texture) quad->SetTexture(texture);
    if (uvScale != sSynced[slot][0] || uvOffset != sSynced[slot][1])
    {
        quad->SetUvScale(uvScale);
        quad->SetUvOffset(uvOffset);
        sSynced[slot][0] = uvScale;
        sSynced[slot][1] = uvOffset;
    }
    static bool sUnused = false;
    Property crop(DatumType::Bool, "Crop Texture", quad, &sUnused);
    quad->DrawCustomProperty(crop); // the button, and the crop editor's window while it is open
    const glm::vec2 scale = quad->GetUvScale();
    const glm::vec2 offset = quad->GetUvOffset();
    if (scale == sSynced[slot][0] && offset == sSynced[slot][1])
    {
        return false;
    }
    uvScale = sSynced[slot][0] = scale;
    uvOffset = sSynced[slot][1] = offset;
    return true;
}

bool FitCombo(const char* label, uint8_t& fit)
{
    int value = fit;
    ImGui::SetNextItemWidth(160.0f);
    const bool changed = ImGui::Combo(label, &value, "Fill\0Contain\0Cover\0None\0");
    if (changed) fit = (uint8_t)value;
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("How the picture fits its box: Contain = all of it, its shape kept;\n"
                          "Cover = fills the box, cropped; Fill = stretched; None = its own size.");
    }
    return changed;
}

void DrawMenuStyle(void*)
{
    std::vector<ModMap*> maps = ModMap_FindAll();
    ModMap* map = CurrentMap();
    if (map == nullptr && !maps.empty()) map = maps.front();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Mod Map", map ? map->GetName().c_str() : "(none)"))
    {
        for (ModMap* m : maps)
        {
            if (ImGui::Selectable(m->GetName().c_str(), m == map))
            {
                sMapName = m->GetName();
                map = m;
                sStyleScene[0] = 0;
            }
        }
        ImGui::EndCombo();
    }
    if (map == nullptr)
    {
        ImGui::TextColored(kWarn, "Create a Mod Map first (Tools > Recomp > Mods > Mod Map Editor).");
        return;
    }
    if (sStyleScene[0] == 0) snprintf(sStyleScene, sizeof(sStyleScene), "%s", ModScene_DefaultName(map).c_str());

    ModStyle& s = map->mStyle;
    bool changed = false;
    auto color = [&](const char* label, glm::vec4& c) {
        changed |= ImGui::ColorEdit4(label, &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    };
    auto size = [&](const char* label, float& v) {
        ImGui::SetNextItemWidth(120.0f);
        changed |= ImGui::DragFloat(label, &v, 0.25f, 6.0f, 96.0f, "%.1f px");
    };

    ImGui::BeginChild("style", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.2f));
    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Panel background", &s.mPanelBackground);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Off: the panel draws no fill or texture; what is behind it shows (a 3D scene).\n"
                              "Its content is unaffected either way.");
        }
        if (!s.mPanelBackground) ImGui::BeginDisabled();
        color(s.mPanelTexture.Get() != nullptr && !s.mTintPanel ? "Color (no texture)##panel" : "Tint##panel",
              s.mPanelColor);
        changed |= AssetPicker("Texture##panel", s.mPanelTexture, Texture::GetStaticType());
        changed |= ImGui::Checkbox("Tint the texture##panel", &s.mTintPanel);
        ImGui::TextDisabled("The panel behind the menu's content. With a texture the tint multiplies it;\n"
                            "untinted it shows as it is. The launcher's full-screen picture is set in\n"
                            "Tools > Recomp > Mods > Launcher (Pictures).");
        if (!s.mPanelBackground) ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Buttons", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* const kStates[ModStyle::StateCount] = {"Normal", "Hovered", "Pressed", "Locked"};
        if (ImGui::BeginTable("states", 3, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Color");
            ImGui::TableSetupColumn("Texture");
            ImGui::TableHeadersRow();
            for (int i = 0; i < ModStyle::StateCount; ++i)
            {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kStates[i]);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(220.0f);
                changed |= ImGui::ColorEdit4("##color", &s.mButtonColors[i].x, ImGuiColorEditFlags_AlphaBar);
                ImGui::TableNextColumn();
                changed |= AssetPicker("##texture", s.mButtonTextures[i], Texture::GetStaticType());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("A state without a texture uses Normal's. Hovered = mouse over; the gamepad's\n"
                            "selected button gets the border below.");
        changed |= ImGui::Checkbox("State colors tint the whole button", &s.mButtonStateTint);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("On: hovering, pressing or selecting a button tints all of it with that state's color.\n"
                              "Off: every state keeps the Normal color (white = a texture as it is); the state\n"
                              "textures still change, and the gamepad's selection shows by the border.");
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Texture");
        changed |= CropTextureButton(0, s.mButtonTextures[ModStyle::Normal].Get<Texture>(), s.mButtonUvScale,
                                     s.mButtonUvOffset);
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("UV Scale", &s.mButtonUvScale.x, 0.005f, 0.0f, 16.0f, "%.3f");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("UV Offset", &s.mButtonUvOffset.x, 0.005f, -16.0f, 16.0f, "%.3f");
        changed |= FitCombo("Object Fit", s.mButtonFit);
        ImGui::Spacing();
        color("Text color##button", s.mButtonTextColor);
        size("Button text size", s.mButtonTextSize);
        size("Tab text size", s.mTabTextSize);
        changed |= ImGui::Checkbox("Selected border", &s.mShowHighlight);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The border around the button the gamepad (or keyboard) has selected.");
        }
        if (!s.mShowHighlight) ImGui::BeginDisabled();
        ImGui::SameLine();
        changed |= ImGui::ColorEdit4("##border", &s.mHighlightColor.x,
                                     ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
        ImGui::SetNextItemWidth(120.0f);
        changed |= ImGui::DragFloat("Border width", &s.mHighlightWidth, 0.1f, 0.0f, 12.0f, "%.1f px");
        if (!s.mShowHighlight) ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= AssetPicker("Font", s.mFont, Font::GetStaticType());
        ImGui::TextDisabled("(none) = the engine's default font. The fonts below default to it.");
        changed |= AssetPicker("Header font", s.mHeaderFont, Font::GetStaticType());
        changed |= AssetPicker("Body font", s.mBodyFont, Font::GetStaticType());
        changed |= AssetPicker("Button font", s.mButtonFont, Font::GetStaticType());
        ImGui::TextDisabled("Header: titles. Body: labels, values, notes, the launcher's subtitle,\n"
                            "ROM line and messages. Button: buttons and tabs.");
        color("Title color", s.mTitleColor);
        size("Title size", s.mTitleSize);
        color("Label color", s.mLabelColor);
        color("Read-only label color", s.mInfoColor);
        size("Label size", s.mLabelSize);
        color("Value color", s.mValueColor);
        size("Value size", s.mValueSize);
        size("Note size", s.mNoteSize);
    }
    if (ImGui::CollapsingHeader("Launcher", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ModLauncherSettings& l = map->mLauncher;
        changed |= AssetPicker("Logo##ms", l.mLogo, Texture::GetStaticType());
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("Logo size##ms", &l.mLogoSize.x, 1.0f, 16.0f, 2048.0f, "%.0f px");
        changed |= CropTextureButton(1, l.mLogo.Get<Texture>(), l.mLogoUvScale, l.mLogoUvOffset);
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("Logo UV Scale##ms", &l.mLogoUvScale.x, 0.005f, 0.0f, 16.0f, "%.3f");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("Logo UV Offset##ms", &l.mLogoUvOffset.x, 0.005f, -16.0f, 16.0f, "%.3f");
        changed |= FitCombo("Logo Object Fit##ms", l.mLogoFit);
        ImGui::Spacing();
        changed |= AssetPicker("Background music##ms", l.mMusic, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loops while the launcher is up; stops when the game starts.");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::SliderFloat("Music volume##ms", &l.mMusicVolume, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("The launcher's text, pictures and buttons: Tools > Recomp > Mods > Launcher.");
    }
    if (ImGui::CollapsingHeader("Sounds", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= AssetPicker("Move##snd", s.mSoundMove, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The gamepad / keyboard (or mouse) selection moves to another button.");
        changed |= AssetPicker("Select##snd", s.mSoundSelect, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A button is pressed (the launcher's Play and Quit have sounds of their own).");
        changed |= AssetPicker("Cancel##snd", s.mSoundCancel, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A Close button is pressed.");
        changed |= AssetPicker("Gamepad back##snd", s.mSoundBack, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The gamepad's B (or the menu's close action) closes the menu.");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::SliderFloat("Volume##snd", &s.mSoundVolume, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("(none) = silent. The launcher's Start, Quit and music: Tools > Recomp > Mods > Launcher.");
    }
    ImGui::EndChild();

    if (changed)
    {
        map->SetDirtyFlag();
        if (sStyleLive)
        {
            ModScene_RestyleOpen(s);
            ModLauncher_RestyleOpen(*map);
            ModDisclaimer_RestyleOpen(*map);
        }
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Scene", sStyleScene, sizeof(sStyleScene));
    ImGui::SameLine();
    ImGui::Checkbox("Live preview", &sStyleLive);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Restyle the settings menus in the open level as you edit (Update Scene saves it).");
    }
    if (ImGui::Button("Update Scene", ImVec2(130, 0)))
    {
        UpdateStyledScene(map);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Saves the style with the Mod Map and restyles the scene (layout and navigation untouched).\n"
                          "Generate / Update in the Generate dialog applies it too.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(90, 0)))
    {
        SaveMap(map);
        sStyleMessage = sStatus;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults", ImVec2(150, 0)))
    {
        s = ModStyle();
        map->SetDirtyFlag();
        if (sStyleLive)
        {
            ModScene_RestyleOpen(s);
            ModLauncher_RestyleOpen(*map);
        }
    }
    if (!sStyleMessage.empty())
    {
        ImGui::TextWrapped("%s", sStyleMessage.c_str());
    }
}

// ---- Launcher ------------------------------------------------------------------------------
char sLauncherScene[128] = "";
bool sLauncherLive = true;
std::string sLauncherMessage;

bool InputLabel(const char* label, std::string& value)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", value.c_str());
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputText(label, buf, sizeof(buf)))
    {
        value = buf;
        return true;
    }
    return false;
}

void DrawLauncher(void*)
{
    std::vector<ModMap*> maps = ModMap_FindAll();
    ModMap* map = CurrentMap();
    if (map == nullptr && !maps.empty()) map = maps.front();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Mod Map", map ? map->GetName().c_str() : "(none)"))
    {
        for (ModMap* m : maps)
        {
            if (ImGui::Selectable(m->GetName().c_str(), m == map))
            {
                sMapName = m->GetName();
                map = m;
                sLauncherScene[0] = 0;
            }
        }
        ImGui::EndCombo();
    }
    if (map == nullptr)
    {
        ImGui::TextColored(kWarn, "Create a Mod Map first (Tools > Recomp > Mods > Mod Map Editor).");
        return;
    }
    if (sLauncherScene[0] == 0) snprintf(sLauncherScene, sizeof(sLauncherScene), "%s", ModLauncher_DefaultName(map).c_str());
    ImGui::TextDisabled("The front-end scene of %s: the player sets the ROM up, opens the mods, presses Play.",
                        map->mGame.c_str());

    ModLauncherSettings& l = map->mLauncher;
    bool changed = false;
    ImGui::BeginChild("launcher", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.2f));
    if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= InputLabel("Title", l.mTitle);
        ImGui::TextDisabled("Empty: the Mod Map's title (%s).", map->mTitle.empty() ? "none" : map->mTitle.c_str());
        changed |= InputLabel("Subtitle", l.mSubtitle);
    }
    if (ImGui::CollapsingHeader("Pictures", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextDisabled("Logo (picture, size, crop, fit) and music: Menu Style > Launcher.");
        changed |= ImGui::Checkbox("Show background", &l.mShowBackground);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The full-screen picture / color behind the panel. Off (and Menu Style >\n"
                              "Panel background off): the scene behind the launcher shows, e.g. a 3D one.");
        }
        changed |= AssetPicker("Background", l.mBackground, Texture::GetStaticType());
        changed |= ImGui::Checkbox("Tint the picture", &l.mTintBackground);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Off: the background picture as it is. Without a picture the color below is the fill.");
        }
        changed |= ImGui::ColorEdit4(l.mBackground.Get() != nullptr && !l.mTintBackground ? "Background color (no picture)"
                                                                                            : "Background tint",
                                     &l.mBackgroundColor.x, ImGuiColorEditFlags_AlphaBar);
        ImGui::TextDisabled("The panel, buttons and fonts follow the Menu Style (Tools > Recomp > Mods > Menu Style).");
    }
    if (ImGui::CollapsingHeader("Panel and buttons", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Centre vertically", &l.mCenterContent);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("The logo, title and buttons in the middle of a screen taller than them\n"
                              "(off: at the top). A shorter screen scrolls them either way.");
        }
        changed |= ImGui::Checkbox("Full screen", &l.mPanelFullScreen);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("On: the panel fills the screen, in play as in the editor.\n"
                              "Off: in play it is fitted to the screen: at most Panel size, by Position.\n"
                              "(The editor shows the panel full size either way.)");
        }
        if (l.mPanelFullScreen) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("Panel size", &l.mPanelSize.x, 1.0f, 200.0f, 4096.0f, "%.0f px");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::Combo("Position", &l.mPosition, "Centre\0Left\0Right\0");
        if (l.mPanelFullScreen) ImGui::EndDisabled();
        changed |= InputLabel("Play", l.mPlayLabel);
        changed |= InputLabel("Choose ROM", l.mBrowseLabel);
        changed |= InputLabel("Forget ROM", l.mForgetLabel);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Show##forget", &l.mShowForget);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shown only while a ROM is set.");
        changed |= InputLabel("Mods", l.mModsLabel);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Show##mods", &l.mShowMods);
        changed |= InputLabel("Quit", l.mQuitLabel);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Show##quit", &l.mShowQuit);
    }
    if (ImGui::CollapsingHeader("Sounds", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= AssetPicker("Start game##lsnd", l.mSoundStart, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Play: the game scene opens once it has played (2 s at most).\n"
                              "None = the menu style's Select sound.");
        }
        changed |= AssetPicker("Quit##lsnd", l.mSoundQuit, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Quit: a packaged game closes once it has played (1.5 s at most).");
        }
        changed |= AssetPicker("Deny##lsnd", l.mSoundDeny, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Play with no ROM set, a ROM that is refused (another game or revision),\n"
                              "or a start that fails. None = silent.");
        }
        changed |= AssetPicker("Forget ROM##lsnd", l.mSoundForget, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Forget ROM. None = the menu style's Select sound.");
        changed |= AssetPicker("Music##lsnd", l.mMusic, SoundWave::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loops while the launcher is up; stops when the game starts.");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::SliderFloat("Music volume##lsnd", &l.mMusicVolume, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("Moving, selecting and going back use the Menu Style's sounds.");
    }
    if (ImGui::CollapsingHeader("Footer", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Show footer", &l.mShowFooter);
        if (!l.mShowFooter) ImGui::BeginDisabled();
        changed |= InputLabel("Footer text", l.mFooterText);
        ImGui::SetNextItemWidth(120.0f);
        changed |= ImGui::DragFloat("Footer text size", &l.mFooterTextSize, 0.25f, 2.0f, 48.0f, "%.1f px");
        changed |= AssetPicker("Footer logo", l.mFooterLogo, Texture::GetStaticType());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("After the footer text, bottom left (e.g. the Polyphase logo).");
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("Footer logo size", &l.mFooterLogoSize.x, 1.0f, 2.0f, 1024.0f, "%.0f px");
        changed |= InputLabel("Version", l.mVersionFormat);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Bottom right. {@launcher.version} = the app's Version (App Settings),\n"
                              "{@launcher.project} = its name. Empty = no version line.");
        }
        if (!l.mShowFooter) ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Starting the game", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= AssetPicker("Game Scene", l.mGameScene, Scene::GetStaticType());
        ImGui::TextDisabled("Opened once the game starts: the scene with the game's player node.\n"
                            "(none) = stay; the launcher then steps aside for the game in its own scene.");
        changed |= ImGui::Checkbox("Start at once when the ROM is known", &l.mAutoStart);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Later launches go straight to the game; the launcher shows only while no ROM is set\n"
                              "(or the ROM stopped working).");
        }
    }
    ImGui::EndChild();

    if (changed)
    {
        map->SetDirtyFlag();
        if (sLauncherLive) ModLauncher_RestyleOpen(*map);
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Scene", sLauncherScene, sizeof(sLauncherScene));
    ImGui::SameLine();
    ImGui::Checkbox("Live preview", &sLauncherLive);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Update the launchers in the open level as you edit (Generate / Update saves it).");
    }
    const bool exists = FetchAssetStub(sLauncherScene) != nullptr;
    if (ImGui::Button(exists ? "Update Scene" : "Generate Scene", ImVec2(140, 0)))
    {
        SaveMap(map);
        ModLauncher_Generate(map, sLauncherScene, sLauncherMessage);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Saves these settings with the Mod Map and makes / updates the launcher scene.\n"
                          "Nodes you changed or added stay; the look is applied again.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(90, 0)))
    {
        SaveMap(map);
        sLauncherMessage = sStatus;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults", ImVec2(150, 0)))
    {
        l = ModLauncherSettings();
        map->SetDirtyFlag();
        if (sLauncherLive) ModLauncher_RestyleOpen(*map);
    }
    if (!sLauncherMessage.empty())
    {
        ImGui::TextWrapped("%s", sLauncherMessage.c_str());
    }
}

// ---- Disclaimer ----------------------------------------------------------------------------
char sDisclaimerScene[128] = "";
bool sDisclaimerLive = true;
std::string sDisclaimerMessage;

bool InputMultiline(const char* label, std::string& value)
{
    static char buf[4096];
    snprintf(buf, sizeof(buf), "%s", value.c_str());
    if (ImGui::InputTextMultiline(label, buf, sizeof(buf), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f)))
    {
        value = buf;
        return true;
    }
    return false;
}

void DrawDisclaimer(void*)
{
    std::vector<ModMap*> maps = ModMap_FindAll();
    ModMap* map = CurrentMap();
    if (map == nullptr && !maps.empty()) map = maps.front();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Mod Map", map ? map->GetName().c_str() : "(none)"))
    {
        for (ModMap* m : maps)
        {
            if (ImGui::Selectable(m->GetName().c_str(), m == map))
            {
                sMapName = m->GetName();
                map = m;
                sDisclaimerScene[0] = 0;
            }
        }
        ImGui::EndCombo();
    }
    if (map == nullptr)
    {
        ImGui::TextColored(kWarn, "Create a Mod Map first (Tools > Recomp > Mods > Mod Map Editor).");
        return;
    }
    if (sDisclaimerScene[0] == 0)
    {
        snprintf(sDisclaimerScene, sizeof(sDisclaimerScene), "%s", ModDisclaimer_DefaultName(map).c_str());
    }
    ImGui::TextDisabled("Pages the player accepts once before %s starts; later runs show each for a moment.",
                        map->mTitle.empty() ? map->mGame.c_str() : map->mTitle.c_str());

    ModDisclaimerSettings& d = map->mDisclaimer;
    bool changed = false;
    ImGui::BeginChild("disclaimer", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.2f));
    if (ImGui::CollapsingHeader("Pages", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int remove = -1;
        for (int i = 0; i < (int)d.mPages.size(); ++i)
        {
            ImGui::PushID(i);
            ImGui::Text("Page %d", i + 1);
            if (d.mPages.size() > 1)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) remove = i;
            }
            changed |= InputLabel("Title", d.mPages[i].mTitle);
            changed |= InputMultiline("##text", d.mPages[i].mText);
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (remove >= 0)
        {
            d.mPages.erase(d.mPages.begin() + remove);
            changed = true;
        }
        if ((int)d.mPages.size() < ModDisclaimerSettings::kMaxPages && ImGui::Button("Add page"))
        {
            d.mPages.push_back({"Notice", ""});
            changed = true;
        }
        ImGui::TextDisabled("A change to any title or text asks players who accepted before to accept again.");
    }
    if (ImGui::CollapsingHeader("Buttons and timing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= InputLabel("Accept", d.mAcceptLabel);
        changed |= InputLabel("Decline", d.mDeclineLabel);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quits a packaged game.");
        ImGui::SetNextItemWidth(120.0f);
        changed |= ImGui::DragFloat("Seconds per page (accepted)", &d.mHoldSeconds, 0.1f, 0.0f, 30.0f, "%.1f s");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Once accepted, later runs show each page this long (A / Start skips) and go on.");
        }
        changed |= AssetPicker("Next Scene", d.mNextScene, Scene::GetStaticType());
        ImGui::TextDisabled("Opened after the last page: the launcher, usually.");
    }
    if (ImGui::CollapsingHeader("Background and panel", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Show background", &d.mShowBackground);
        changed |= AssetPicker("Background", d.mBackground, Texture::GetStaticType());
        changed |= ImGui::Checkbox("Tint the picture", &d.mTintBackground);
        changed |= ImGui::ColorEdit4(d.mBackground.Get() != nullptr && !d.mTintBackground ? "Background color (no picture)"
                                                                                            : "Background tint",
                                     &d.mBackgroundColor.x, ImGuiColorEditFlags_AlphaBar);
        changed |= ImGui::Checkbox("Full screen panel", &d.mPanelFullScreen);
        if (d.mPanelFullScreen) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::DragFloat2("Panel size", &d.mPanelSize.x, 1.0f, 200.0f, 4096.0f, "%.0f px");
        if (d.mPanelFullScreen) ImGui::EndDisabled();
        ImGui::TextDisabled("Panel, buttons, fonts and sounds follow the Menu Style.");
    }
    ImGui::EndChild();

    if (changed)
    {
        map->SetDirtyFlag();
        if (sDisclaimerLive) ModDisclaimer_RestyleOpen(*map);
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("Scene", sDisclaimerScene, sizeof(sDisclaimerScene));
    ImGui::SameLine();
    ImGui::Checkbox("Live preview", &sDisclaimerLive);
    const bool exists = FetchAssetStub(sDisclaimerScene) != nullptr;
    if (ImGui::Button(exists ? "Update Scene" : "Generate Scene", ImVec2(140, 0)))
    {
        SaveMap(map);
        ModDisclaimer_Generate(map, sDisclaimerScene, sDisclaimerMessage);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Saves these settings with the Mod Map and makes / updates the disclaimer scene.\n"
                          "Nodes you changed or added stay; the look is applied again.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(90, 0)))
    {
        SaveMap(map);
        sDisclaimerMessage = sStatus;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults", ImVec2(150, 0)))
    {
        d = ModDisclaimerSettings();
        map->SetDirtyFlag();
        if (sDisclaimerLive) ModDisclaimer_RestyleOpen(*map);
    }
    if (!sDisclaimerMessage.empty())
    {
        ImGui::TextWrapped("%s", sDisclaimerMessage.c_str());
    }
}

// ---- inspector, create asset ---------------------------------------------------------------
// ---- Export > Mods > Manifest: the entries as a Recomp Zoo mod import ------------------------
std::string sExportMessage;
ImVec4 sExportColor = kGood;
std::vector<std::string> sExportDetails;

// The Zoo's game id when none is set: the game package's title ("Super Smash Bros. (US)" ->
// "super-smash-bros"), else the map's title without " Mods", else the package id's last part.
std::string GuessZooGameId(const ModMap* map)
{
    std::string title;
    for (const GamePackage& g : FindGamePackages())
    {
        if (g.id == map->mGame && g.title != g.id) title = g.title;
    }
    if (title.empty())
    {
        title = map->mTitle;
        const std::string suffix = " Mods";
        if (title.size() > suffix.size() && title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0)
            title.resize(title.size() - suffix.size());
    }
    std::string id = ModZoo_Slug(title);
    if (id.empty())
    {
        const size_t dot = map->mGame.find_last_of('.');
        id = ModZoo_Slug(dot == std::string::npos ? map->mGame : map->mGame.substr(dot + 1));
    }
    return id;
}

std::string ZooExportPath(const ModMap* map, const std::string& gameId)
{
    if (!map->mZooExportPath.empty()) return map->mZooExportPath;
    return GetEngineState()->mProjectDirectory + "Exports/RecompZoo/" + (gameId.empty() ? std::string("mods") : gameId) +
           ".mods.json";
}

void ExportZoo(ModMap* map, const std::string& strategy)
{
    sExportDetails.clear();
    ModZooExportOptions options;
    options.gameId = map->mZooGameId.empty() ? GuessZooGameId(map) : map->mZooGameId;
    options.strategy = strategy;
    const ModZooExportResult result = ModMap_ExportZoo(*map, options);
    if (!result.errors.empty())
    {
        sExportMessage = result.errors.front();
        sExportColor = kBad;
        return;
    }
    const std::string path = ZooExportPath(map, options.gameId);
    std::error_code ec;
    const std::filesystem::path file = std::filesystem::u8path(path);
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << result.json;
    out.close();
    if (!out)
    {
        sExportMessage = "Could not write " + path;
        sExportColor = kBad;
        return;
    }
    sExportMessage = "Exported " + std::to_string(result.exported) + " mod(s) of " + options.gameId + " to " + path;
    sExportColor = result.skipped.empty() ? kGood : kWarn;
    for (const std::string& s : result.skipped) sExportDetails.push_back("Left out: " + s);
    for (const std::string& n : result.notes) sExportDetails.push_back("Note: " + n);
    LogDebug("Recomp Zoo export: %s", sExportMessage.c_str());
}

void DrawExportManifest(void*)
{
    static int sStrategy = 0; // merge, replace
    std::vector<ModMap*> maps = ModMap_FindAll();
    ModMap* map = CurrentMap();
    if (map == nullptr && !maps.empty()) map = maps.front();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("Mod Map", map ? map->GetName().c_str() : "(none)"))
    {
        for (ModMap* m : maps)
        {
            if (ImGui::Selectable(m->GetName().c_str(), m == map))
            {
                sMapName = m->GetName();
                map = m;
                sExportMessage.clear();
                sExportDetails.clear();
            }
        }
        ImGui::EndCombo();
    }
    if (map == nullptr)
    {
        ImGui::TextColored(kWarn, "Create a Mod Map first (Tools > Recomp > Mods > Mod Map Editor).");
        return;
    }
    ImGui::TextDisabled("Writes the map's entries as a Recomp Zoo mod import (polyphase-recomp-zoo/mods, schema 1).");
    ImGui::Separator();

    // the Zoo's game id (kept on the map; empty = from the title)
    const std::string guess = GuessZooGameId(map);
    std::string gameId = map->mZooGameId;
    if (gameId.empty()) gameId = guess;
    if (InputString("Zoo Game Id", gameId, 260.0f))
    {
        map->mZooGameId = gameId == guess ? std::string() : gameId;
        MarkDirty(map);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("From title"))
    {
        map->mZooGameId.clear();
        gameId = guess;
        MarkDirty(map);
    }
    if (!ModZoo_IsValidGameId(gameId))
        ImGui::TextColored(kBad, "Lowercase letters, digits and single dashes (the game's id in Recomp Zoo).");

    const char* strategies[] = {"merge", "replace"};
    ImGui::SetNextItemWidth(260.0f);
    ImGui::Combo("Strategy", &sStrategy, strategies, 2);
    ImGui::SameLine();
    ImGui::TextDisabled(sStrategy == 0 ? "(updates mods with the same id, adds new ones)" : "(replaces the game's whole mod list)");

    // where
    std::string path = ZooExportPath(map, gameId);
    if (InputString("File", path, 420.0f))
    {
        map->mZooExportPath = path;
        MarkDirty(map);
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...") && sHooks != nullptr && sHooks->ShowSaveFileDialog != nullptr)
    {
        char picked[1024] = {};
        const std::string name = gameId + ".mods.json";
        if (sHooks->ShowSaveFileDialog("Export Recomp Zoo Mods", "JSON|*.json", name.c_str(), picked, sizeof(picked)) != 0)
        {
            std::string chosen = picked;
            if (chosen.size() < 5 || chosen.compare(chosen.size() - 5, 5, ".json") != 0) chosen += ".json";
            map->mZooExportPath = chosen;
            MarkDirty(map);
        }
    }
    if (!map->mZooExportPath.empty())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Default")) { map->mZooExportPath.clear(); MarkDirty(map); }
    }

    // what goes in
    ModZooExportOptions preview;
    preview.gameId = ModZoo_IsValidGameId(gameId) ? gameId : std::string("preview");
    const ModZooExportResult check = ModMap_ExportZoo(*map, preview);
    ImGui::Text("%d of %d entries will be exported.", check.exported, (int)map->mEntries.size());
    if (!check.skipped.empty())
    {
        ImGui::TextColored(kWarn, "%d left out (the Zoo would refuse them; fix them in the Mod Map Editor):",
                           (int)check.skipped.size());
        for (const std::string& s : check.skipped) ImGui::BulletText("%s", s.c_str());
    }
    ImGui::TextDisabled("Built-in display.* settings aren't Mod Map entries and aren't exported.");

    ImGui::Separator();
    ImGui::BeginDisabled(!ModZoo_IsValidGameId(gameId) || check.exported == 0);
    if (ImGui::Button("Export", ImVec2(140.0f, 0.0f)))
    {
        ExportZoo(map, strategies[sStrategy]);
        if (!map->mZooExportPath.empty() || !map->mZooGameId.empty()) SaveMap(map); // keeps the id and the file
    }
    ImGui::EndDisabled();
    if (!sExportMessage.empty())
    {
        ImGui::TextColored(sExportColor, "%s", sExportMessage.c_str());
        for (const std::string& d : sExportDetails) ImGui::BulletText("%s", d.c_str());
    }
}

void DrawModMapInspector(void* object, void*)
{
    ModMap* map = static_cast<ModMap*>(object);
    if (map == nullptr) return;
    ImGui::Separator();
    ImGui::Text("Game: %s  (%s)", map->mGame.c_str(), map->mRuntime.c_str());
    ImGui::Text("%d entries in %d groups", (int)map->mEntries.size(), (int)map->OrderedGroups().size());
    if (ImGui::Button("Open in Mod Map Editor"))
    {
        sMapName = map->GetName();
        sSelected = -1;
        if (sHooks && sHooks->OpenWindow) sHooks->OpenWindow(kEditorWindow);
    }
    ImGui::SameLine();
    if (ImGui::Button("Generate Scene...")) OpenGenerate(map->GetName());
    ImGui::SameLine();
    if (ImGui::Button("Menu Style"))
    {
        sMapName = map->GetName();
        sStyleScene[0] = 0;
        if (sHooks && sHooks->OpenWindow) sHooks->OpenWindow(kStyleWindow);
    }
    ImGui::SameLine();
    if (ImGui::Button("Launcher"))
    {
        sMapName = map->GetName();
        sLauncherScene[0] = 0;
        if (sHooks && sHooks->OpenWindow) sHooks->OpenWindow(kLauncherWindow);
    }
}

void CreateModMapAsset(void*)
{
    ModMap* map = CreateMap("MM_NewMods", GetCurrentAssetDir(), nullptr);
    if (map != nullptr && sHooks && sHooks->OpenWindow) sHooks->OpenWindow(kEditorWindow);
}

void OpenWindow(void* id)
{
    if (sHooks && sHooks->OpenWindow) sHooks->OpenWindow(static_cast<const char*>(id));
}
}

void ModBaseEditor::Register(EditorUIHooks* hooks, uint64_t hookId)
{
    sHooks = hooks;
    sHookId = hookId;
    if (hooks == nullptr)
    {
        return;
    }
    if (hooks->RegisterWindow != nullptr)
    {
        hooks->RegisterWindow(hookId, "Mod Map Editor", kEditorWindow, DrawModMapEditor, nullptr);
        hooks->RegisterWindow(hookId, "Live Variables", kLiveWindow, DrawLiveVariables, nullptr);
        hooks->RegisterWindow(hookId, "Display Settings", kDisplayWindow, DrawDisplaySettings, nullptr);
        hooks->RegisterWindow(hookId, "Menu Style", kStyleWindow, DrawMenuStyle, nullptr);
        hooks->RegisterWindow(hookId, "Launcher", kLauncherWindow, DrawLauncher, nullptr);
        hooks->RegisterWindow(hookId, "Disclaimer", kDisclaimerWindow, DrawDisclaimer, nullptr);
        hooks->RegisterWindow(hookId, "Export Mods Manifest", kExportWindow, DrawExportManifest, nullptr);
#if MODBASE_HAS_MODALS
        if (hooks->OpenModal == nullptr)
#endif
        {
            hooks->RegisterWindow(hookId, kGenerateModal, kGenerateWindow,
                                  [](void* ud) {
                                      if (!DrawGenerate(ud) && sHooks && sHooks->CloseWindow)
                                          sHooks->CloseWindow(kGenerateWindow);
                                  },
                                  nullptr);
        }
    }
    if (hooks->AddMenuItem != nullptr)
    {
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Mod Map Editor", OpenWindow, (void*)kEditorWindow, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Generate Mod Settings Scene...",
                           [](void*) { OpenGenerate(std::string()); }, nullptr, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Live Variables", OpenWindow, (void*)kLiveWindow, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Display Settings", OpenWindow, (void*)kDisplayWindow, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Menu Style", OpenWindow, (void*)kStyleWindow, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Launcher", OpenWindow, (void*)kLauncherWindow, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Mods/Disclaimer", OpenWindow, (void*)kDisclaimerWindow, nullptr);
        hooks->AddMenuItem(hookId, "Tools", "Recomp/Export/Mods/Manifest", OpenWindow, (void*)kExportWindow, nullptr);
    }
    if (hooks->RegisterInspector != nullptr)
    {
        hooks->RegisterInspector(hookId, "ModMap", DrawModMapInspector, nullptr);
    }
    if (hooks->AddCreateAssetItem != nullptr)
    {
        hooks->AddCreateAssetItem(hookId, "Recomp/Mod Map", CreateModMapAsset, nullptr);
    }
}

void ModBaseEditor::Unregister()
{
    sHooks = nullptr;
}

#else

void ModBaseEditor::Register(EditorUIHooks*, uint64_t)
{
}

void ModBaseEditor::Unregister()
{
}

#endif
