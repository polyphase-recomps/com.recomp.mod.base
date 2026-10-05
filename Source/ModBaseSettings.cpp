/**
 * @file ModBaseSettings.cpp
 * @brief The end user's mod settings (see ModBaseSettings.h).
 */

#include "ModBaseSettings.h"

#include "ModBaseDisplay.h"

#include "Engine.h"
#include "Log.h"
#include "Stream.h"
#if !EDITOR
#include "System/System.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#if EDITOR && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
constexpr uint32_t kMagic = 0x444F4D52; // "RMOD"
constexpr uint32_t kVersion = 1;
constexpr int kSaveDelayFrames = 90;    // save 1.5 s after the last change
constexpr int kLockInterval = 10;       // frames between lock checks

std::vector<int> ParseArgs(const std::string& text)
{
    std::vector<int> args;
    const char* p = text.c_str();
    while (*p)
    {
        char* end = nullptr;
        const long v = strtol(p, &end, 0);
        if (end == p)
        {
            ++p;
            continue;
        }
        args.push_back((int)v);
        p = end;
    }
    return args;
}

ModEntry MakeChoice(const char* id, const char* label, const std::vector<std::string>& labels, float def)
{
    ModEntry e;
    e.mId = id;
    e.mLabel = label;
    e.mGroup = "Display";
    e.mKind = ModKind::Choice;
    e.mSource = ModSource::Display;
    e.mName = id;
    e.mChoiceLabels = labels;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        e.mChoiceValues.push_back((float)i);
    }
    e.mMin = 0.0f;
    e.mMax = labels.empty() ? 0.0f : float(labels.size() - 1);
    e.mDefault = def;
    return e;
}

float DisplayGet(const std::string& id)
{
    const RecompDisplaySettings& d = Recomp_DisplaySettings();
    if (id == "display.mode") return (float)d.mode;
    if (id == "display.scale") return (float)d.scale;
    if (id == "display.filter") return (float)d.filter;
    if (id == "display.window") return (float)d.window;
    return 0.0f;
}

void DisplaySet(const std::string& id, float value)
{
    RecompDisplaySettings& d = Recomp_DisplaySettings();
    const int v = (int)std::lround(value);
    if (id == "display.mode") d.mode = (RecompFitMode)std::max(0, std::min(v, (int)RecompFitMode::Count - 1));
    else if (id == "display.scale") d.scale = std::max(1, std::min(v, 8));
    else if (id == "display.filter") d.filter = std::max(0, std::min(v, 2));
    else if (id == "display.window") d.window = std::max(0, v);
}

bool IsDisplayId(const std::string& id)
{
    return id.compare(0, 8, "display.") == 0;
}
}

ModSettings& ModSettings::Get()
{
    static ModSettings sSettings;
    return sSettings;
}

ModSettings::ModSettings()
{
    std::vector<std::string> modes;
    for (int i = 0; i < (int)RecompFitMode::Count; ++i)
    {
        modes.push_back(Recomp_FitModeName((RecompFitMode)i));
    }
    mBuiltins.push_back(MakeChoice("display.mode", "Screen", modes, 0.0f));
    ModEntry scale;
    scale.mId = "display.scale";
    scale.mLabel = "Scale";
    scale.mGroup = "Display";
    scale.mKind = ModKind::Int;
    scale.mSource = ModSource::Display;
    scale.mName = scale.mId;
    scale.mMin = 1.0f;
    scale.mMax = 6.0f;
    scale.mStep = 1.0f;
    scale.mDefault = 2.0f;
    mBuiltins.push_back(scale);
    mBuiltins.push_back(MakeChoice("display.filter", "Filter", {"Auto", "Sharp", "Smooth"}, 0.0f));
    std::vector<std::string> windows;
    for (const RecompWindowPreset& p : Recomp_WindowPresets())
    {
        windows.push_back(p.label);
    }
    if (!windows.empty())
    {
        mBuiltins.push_back(MakeChoice("display.window", "Window", windows, 0.0f));
    }
}

const ModEntry* ModSettings::FindEntry(const std::string& id) const
{
    if (mMap != nullptr)
    {
        if (const ModEntry* e = mMap->Find(id))
        {
            return e;
        }
    }
    for (const ModEntry& e : mBuiltins)
    {
        if (e.mId == id) return &e;
    }
    return nullptr;
}

ModMap* ModSettings::GetMap() const
{
    return mMap;
}

void ModSettings::Bind(ModMap* map)
{
    if (map == mMap && map != nullptr)
    {
        return;
    }
    if (mDirty)
    {
        Save();
    }
    mMap = map;
    mBoundGame = map != nullptr ? map->mGame : std::string();
    mUser.clear();
    mApplied.clear();
    Load();
}

ModMap* ModSettings::Bind(const std::string& game)
{
    if (mMap != nullptr && mBoundGame == game)
    {
        return mMap;
    }
    std::vector<ModMap*> maps = ModMap_FindAll(game);
    ModMap* map = maps.empty() ? nullptr : maps.front();
    if (map != mMap || mBoundGame != game)
    {
        Bind(map); // loads the user's values when the map changes
    }
    mBoundGame = game;
    return mMap;
}

void ModSettings::Tick(RecompProvider* provider)
{
    if (provider == nullptr)
    {
        return;
    }
    const std::string game = provider->GamePackage();
    ++mFrame;
    // a new game, or no map yet (one may be created while the game runs: look again now
    // and then)
    if (!game.empty() && (game != mBoundGame || (mMap == nullptr && mFrame % 120 == 0)))
    {
        Bind(game);
    }

    const bool live = provider->IsLive();
    if (live && !mWasLive)
    {
        // a game (re)started: everything the user picked goes to it again
        mApplied.clear();
        mNeedsRestart = false;
    }
    mWasLive = live;
    if (live)
    {
        ApplyAll(provider, true);
        if (mFrame % kLockInterval == 0)
        {
            ApplyAll(provider, false);
        }
    }
    if (mDirty && --mSaveCountdown <= 0)
    {
        Save();
    }
}

void ModSettings::ApplyAll(RecompProvider* provider, bool onlyPending)
{
    if (mMap == nullptr)
    {
        return;
    }
    for (const ModEntry& e : mMap->mEntries)
    {
        auto it = mUser.find(e.mId);
        if (it == mUser.end() || !e.mPersist)
        {
            continue;
        }
        if (e.mSource != ModSource::Variable && e.mSource != ModSource::Address && e.mSource != ModSource::Symbol)
        {
            continue;
        }
        if (onlyPending)
        {
            // until the game takes it (its variables appear once it reaches its main loop)
            if (!mApplied[e.mId])
            {
                mApplied[e.mId] = WriteToGame(provider, e, it->second);
            }
        }
        else if (e.mLock)
        {
            float current = 0.0f;
            if (ReadFromGame(provider, e, current) && std::fabs(current - it->second) > 1e-4f)
            {
                WriteToGame(provider, e, it->second);
            }
        }
    }
}

bool ModSettings::WriteToGame(RecompProvider* provider, const ModEntry& entry, float value)
{
    if (provider == nullptr || !provider->IsLive())
    {
        return false;
    }
    switch (entry.mSource)
    {
    case ModSource::Variable:
    {
        // only once the game publishes it (Set on an unknown name would be lost)
        RecompValue probe;
        if (!provider->Get(entry.mName, entry.mIndex, probe))
        {
            return false;
        }
        return provider->Set(entry.mName, entry.mIndex, RecompValue::Number(value));
    }
    case ModSource::Address:
        return provider->WriteAddress(entry.mAddress, entry.mType, RecompValue::Number(value));
    case ModSource::Symbol:
    {
        uint64_t address = 0;
        return provider->ResolveSymbol(entry.mName, address) &&
               provider->WriteAddress(address, entry.mType, RecompValue::Number(value));
    }
    default:
        return false;
    }
}

bool ModSettings::ReadFromGame(RecompProvider* provider, const ModEntry& entry, float& value) const
{
    if (provider == nullptr || !provider->IsLive())
    {
        return false;
    }
    RecompValue v;
    bool ok = false;
    switch (entry.mSource)
    {
    case ModSource::Variable:
        ok = provider->Get(entry.mName, entry.mIndex, v);
        break;
    case ModSource::Address:
        ok = provider->ReadAddress(entry.mAddress, entry.mType, v);
        break;
    case ModSource::Symbol:
    {
        uint64_t address = 0;
        ok = provider->ResolveSymbol(entry.mName, address) && provider->ReadAddress(address, entry.mType, v);
        break;
    }
    default:
        break;
    }
    if (!ok || v.isText)
    {
        return false;
    }
    value = (float)v.number;
    return true;
}

bool ModSettings::GetValue(const std::string& id, float& value) const
{
    const ModEntry* e = FindEntry(id);
    if (e == nullptr)
    {
        return false;
    }
    if (e->mSource == ModSource::Display)
    {
        value = DisplayGet(id);
        return true;
    }
    auto it = mUser.find(id);
    if (it != mUser.end())
    {
        value = it->second;
        return true;
    }
    RecompProvider* provider = Recomp_FindProvider(mBoundGame.empty() ? nullptr : mBoundGame.c_str());
    if (ReadFromGame(provider, *e, value))
    {
        return true;
    }
    value = e->mDefault;
    return e->mSource == ModSource::StartupOption || e->mKind == ModKind::Toggle || e->mKind == ModKind::Int ||
           e->mKind == ModKind::Float || e->mKind == ModKind::Choice;
}

std::string ModSettings::ValueText(const std::string& id) const
{
    const ModEntry* e = FindEntry(id);
    float value = 0.0f;
    if (e == nullptr || !GetValue(id, value))
    {
        return "--";
    }
    char buf[48];
    switch (e->mKind)
    {
    case ModKind::Toggle:
        return value != 0.0f ? "ON" : "OFF";
    case ModKind::Choice:
        for (size_t i = 0; i < e->mChoiceValues.size() && i < e->mChoiceLabels.size(); ++i)
        {
            if (std::fabs(e->mChoiceValues[i] - value) < 1e-4f)
            {
                return e->mChoiceLabels[i];
            }
        }
        snprintf(buf, sizeof(buf), "%g", value);
        return buf;
    case ModKind::Float:
        snprintf(buf, sizeof(buf), "%.2f", value);
        return buf;
    default:
        snprintf(buf, sizeof(buf), "%lld", (long long)std::llround(value));
        return buf;
    }
}

bool ModSettings::SetValue(const std::string& id, float value)
{
    const ModEntry* e = FindEntry(id);
    if (e == nullptr || e->mKind == ModKind::Display || e->mKind == ModKind::Bar || e->mKind == ModKind::Action)
    {
        return false;
    }
    if (e->mKind != ModKind::Choice && e->mMax > e->mMin)
    {
        value = std::max(e->mMin, std::min(value, e->mMax));
    }
    if (e->mSource == ModSource::Display)
    {
        DisplaySet(id, value);
    }
    if (e->mPersist || e->mSource == ModSource::Display)
    {
        mUser[id] = value;
        mDirty = true;
        mSaveCountdown = kSaveDelayFrames;
    }
    if (e->mSource == ModSource::StartupOption)
    {
        mNeedsRestart = true;
        return true;
    }
    RecompProvider* provider = Recomp_FindProvider(mBoundGame.empty() ? nullptr : mBoundGame.c_str());
    if (e->mSource != ModSource::Display)
    {
        mApplied[id] = WriteToGame(provider, *e, value);
    }
    return true;
}

bool ModSettings::Step(const std::string& id, int direction)
{
    const ModEntry* e = FindEntry(id);
    if (e == nullptr)
    {
        return false;
    }
    float value = 0.0f;
    GetValue(id, value);
    switch (e->mKind)
    {
    case ModKind::Toggle:
        return SetValue(id, value != 0.0f ? 0.0f : (e->mMax != 0.0f ? e->mMax : 1.0f));
    case ModKind::Int:
    case ModKind::Float:
        return SetValue(id, value + (e->mStep != 0.0f ? e->mStep : 1.0f) * float(direction < 0 ? -1 : 1));
    case ModKind::Choice:
    {
        const int n = (int)std::min(e->mChoiceValues.size(), e->mChoiceLabels.size());
        if (n == 0) return false;
        int current = 0;
        for (int i = 0; i < n; ++i)
        {
            if (std::fabs(e->mChoiceValues[i] - value) < 1e-4f) current = i;
        }
        const int next = ((current + (direction < 0 ? -1 : 1)) % n + n) % n;
        return SetValue(id, e->mChoiceValues[next]);
    }
    case ModKind::Action:
    {
        RecompProvider* provider = Recomp_FindProvider(mBoundGame.empty() ? nullptr : mBoundGame.c_str());
        if (provider == nullptr || !provider->IsLive() || e->mSource != ModSource::Request)
        {
            return false;
        }
        return provider->Request(e->mName, ParseArgs(e->mArgs)) != 0;
    }
    default:
        return false;
    }
}

bool ModSettings::IsUserSet(const std::string& id) const
{
    return mUser.find(id) != mUser.end();
}

void ModSettings::Reset(const std::string& id)
{
    const ModEntry* e = FindEntry(id);
    if (e == nullptr)
    {
        return;
    }
    if (e->mSource == ModSource::Display)
    {
        DisplaySet(id, e->mDefault);
    }
    else if (e->mSource == ModSource::Variable || e->mSource == ModSource::Address ||
             e->mSource == ModSource::Symbol)
    {
        // the game keeps the value until it changes it itself; write the default back
        // only for entries that have one in range
        RecompProvider* provider = Recomp_FindProvider(mBoundGame.empty() ? nullptr : mBoundGame.c_str());
        if (mUser.count(id) != 0)
        {
            WriteToGame(provider, *e, e->mDefault);
        }
    }
    else if (e->mSource == ModSource::StartupOption)
    {
        mNeedsRestart = true;
    }
    mUser.erase(id);
    mApplied.erase(id);
    mDirty = true;
    mSaveCountdown = kSaveDelayFrames;
}

void ModSettings::ResetAll()
{
    std::vector<std::string> ids;
    for (auto& pair : mUser)
    {
        ids.push_back(pair.first);
    }
    for (const std::string& id : ids)
    {
        Reset(id);
    }
}

std::vector<std::pair<std::string, int>> ModSettings::StartupOptions(const std::string& game)
{
    std::vector<std::pair<std::string, int>> options;
    ModMap* map = Bind(game);
    if (map == nullptr)
    {
        return options;
    }
    for (const ModEntry& e : map->mEntries)
    {
        if (e.mSource != ModSource::StartupOption)
        {
            continue;
        }
        auto it = mUser.find(e.mId);
        if (it == mUser.end())
        {
            continue; // not changed by the user: the game.json value stays
        }
        options.emplace_back(e.mName, (int)std::lround(it->second));
    }
    return options;
}

std::string ModSettings::SavePath() const
{
    std::string name = mMap != nullptr ? mMap->SaveName() : std::string("recomp");
    return name + ".mods";
}

bool ModSettings::Save()
{
    mDirty = false;
    Stream stream;
    stream.WriteUint32(kMagic);
    stream.WriteUint32(kVersion);
    stream.WriteUint32((uint32_t)mUser.size());
    for (auto& pair : mUser)
    {
        stream.WriteString(pair.first);
        stream.WriteFloat(pair.second);
    }
    const std::string name = SavePath();
#if EDITOR
    // SYS_WriteSave isn't exported to editor addons: same place, written directly
    const std::string dir = GetEngineState()->mProjectDirectory + "Saves";
#if defined(_WIN32)
    CreateDirectoryA(dir.c_str(), nullptr);
#endif
    std::ofstream file(dir + "/" + name, std::ios::binary | std::ios::trunc);
    file.write(stream.GetData(), stream.GetSize());
    const bool ok = file.good();
#else
    const bool ok = SYS_WriteSave(name.c_str(), stream);
#endif
    if (!ok)
    {
        LogWarning("Mods: could not save %s", name.c_str());
    }
    return ok;
}

bool ModSettings::Load()
{
    mUser.clear();
    const std::string name = SavePath();
    Stream stream;
#if EDITOR
    std::ifstream file(GetEngineState()->mProjectDirectory + "Saves/" + name, std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() < 12)
    {
        return false;
    }
    stream.WriteBytes((const uint8_t*)data.data(), (uint32_t)data.size());
    stream.SetPos(0);
#else
    if (!SYS_DoesSaveExist(name.c_str()) || !SYS_ReadSave(name.c_str(), stream))
    {
        return false;
    }
    stream.SetPos(0);
#endif
    if (stream.ReadUint32() != kMagic || stream.ReadUint32() != kVersion)
    {
        LogWarning("Mods: %s is not a settings file of this version, ignored", name.c_str());
        return false;
    }
    const uint32_t count = stream.ReadUint32();
    for (uint32_t i = 0; i < count && stream.GetPos() < stream.GetSize(); ++i)
    {
        std::string id;
        stream.ReadString(id);
        const float value = stream.ReadFloat();
        mUser[id] = value;
        if (IsDisplayId(id))
        {
            DisplaySet(id, value);
        }
    }
    return true;
}
