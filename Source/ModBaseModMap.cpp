/**
 * @file ModBaseModMap.cpp
 * @brief ModMap asset (see ModBaseModMap.h).
 */

#include "ModBaseModMap.h"

#include "AssetManager.h"
#include "Log.h"
#include "Property.h"
#include "Stream.h"

#include <algorithm>

FORCE_LINK_DEF(ModMap);
DEFINE_ASSET(ModMap);

namespace
{
// Our own format version, written after the engine's asset header (addons can't add
// to ASSET_VERSION_*).
constexpr uint32_t kModMapVersion = 1;
}

ModMap::ModMap()
{
    // the on-disk type the loader hands to CreateInstance(): must be set here
    mType = ModMap::GetStaticType();
    mName = "ModMap";
}

ModMap::~ModMap()
{
}

void ModMap::LoadStream(Stream& stream, Platform platform)
{
    Asset::LoadStream(stream, platform);

    const uint32_t version = stream.ReadUint32();
    if (version == 0 || version > kModMapVersion)
    {
        LogError("ModMap %s: unknown format version %u", GetName().c_str(), version);
        return;
    }
    stream.ReadString(mGame);
    stream.ReadString(mRuntime);
    stream.ReadString(mTitle);
    stream.ReadString(mSaveName);

    const uint32_t numGroups = stream.ReadUint32();
    mGroups.resize(numGroups);
    for (std::string& g : mGroups)
    {
        stream.ReadString(g);
    }

    const uint32_t numEntries = stream.ReadUint32();
    mEntries.resize(numEntries);
    for (ModEntry& e : mEntries)
    {
        stream.ReadString(e.mId);
        stream.ReadString(e.mLabel);
        stream.ReadString(e.mGroup);
        stream.ReadString(e.mHelp);
        e.mKind = (ModKind)stream.ReadUint8();
        e.mSource = (ModSource)stream.ReadUint8();
        stream.ReadString(e.mName);
        e.mIndex = stream.ReadInt32();
        const uint32_t lo = stream.ReadUint32();
        const uint32_t hi = stream.ReadUint32();
        e.mAddress = (uint64_t(hi) << 32) | lo;
        e.mType = (RecompType)stream.ReadUint8();
        stream.ReadString(e.mArgs);
        e.mMin = stream.ReadFloat();
        e.mMax = stream.ReadFloat();
        e.mStep = stream.ReadFloat();
        e.mDefault = stream.ReadFloat();
        const uint32_t numChoices = stream.ReadUint32();
        e.mChoiceLabels.resize(numChoices);
        e.mChoiceValues.resize(numChoices);
        for (uint32_t i = 0; i < numChoices; ++i)
        {
            stream.ReadString(e.mChoiceLabels[i]);
            e.mChoiceValues[i] = stream.ReadFloat();
        }
        stream.ReadString(e.mFormat);
        stream.ReadString(e.mMaxName);
        const uint8_t flags = stream.ReadUint8();
        e.mPersist = (flags & 1) != 0;
        e.mLock = (flags & 2) != 0;
        e.mOnTitle = (flags & 4) != 0;
    }
}

void ModMap::SaveStream(Stream& stream, Platform platform)
{
    Asset::SaveStream(stream, platform);

    stream.WriteUint32(kModMapVersion);
    stream.WriteString(mGame);
    stream.WriteString(mRuntime);
    stream.WriteString(mTitle);
    stream.WriteString(mSaveName);

    stream.WriteUint32((uint32_t)mGroups.size());
    for (const std::string& g : mGroups)
    {
        stream.WriteString(g);
    }

    stream.WriteUint32((uint32_t)mEntries.size());
    for (const ModEntry& e : mEntries)
    {
        stream.WriteString(e.mId);
        stream.WriteString(e.mLabel);
        stream.WriteString(e.mGroup);
        stream.WriteString(e.mHelp);
        stream.WriteUint8((uint8_t)e.mKind);
        stream.WriteUint8((uint8_t)e.mSource);
        stream.WriteString(e.mName);
        stream.WriteInt32(e.mIndex);
        stream.WriteUint32((uint32_t)(e.mAddress & 0xFFFFFFFFu));
        stream.WriteUint32((uint32_t)(e.mAddress >> 32));
        stream.WriteUint8((uint8_t)e.mType);
        stream.WriteString(e.mArgs);
        stream.WriteFloat(e.mMin);
        stream.WriteFloat(e.mMax);
        stream.WriteFloat(e.mStep);
        stream.WriteFloat(e.mDefault);
        const uint32_t numChoices = (uint32_t)std::min(e.mChoiceLabels.size(), e.mChoiceValues.size());
        stream.WriteUint32(numChoices);
        for (uint32_t i = 0; i < numChoices; ++i)
        {
            stream.WriteString(e.mChoiceLabels[i]);
            stream.WriteFloat(e.mChoiceValues[i]);
        }
        stream.WriteString(e.mFormat);
        stream.WriteString(e.mMaxName);
        stream.WriteUint8((uint8_t)((e.mPersist ? 1 : 0) | (e.mLock ? 2 : 0) | (e.mOnTitle ? 4 : 0)));
    }
}

void ModMap::GatherProperties(std::vector<Property>& outProps)
{
    Asset::GatherProperties(outProps);
    // Entries are edited in the Mod Map Editor (Tools > Recomp > Mods > Mod Map Editor);
    // the inspector shows a summary and a button to open it.
}

glm::vec4 ModMap::GetTypeColor()
{
    return glm::vec4(0.95f, 0.6f, 0.2f, 1.0f);
}

const char* ModMap::GetTypeName()
{
    return "ModMap";
}

const ModEntry* ModMap::Find(const std::string& id) const
{
    for (const ModEntry& e : mEntries)
    {
        if (e.mId == id) return &e;
    }
    return nullptr;
}

ModEntry* ModMap::Find(const std::string& id)
{
    for (ModEntry& e : mEntries)
    {
        if (e.mId == id) return &e;
    }
    return nullptr;
}

std::string ModMap::SaveName() const
{
    if (!mSaveName.empty())
    {
        return mSaveName;
    }
    // "com.recomp.digimonworld" -> "digimonworld" (short: memory card file names)
    const size_t dot = mGame.find_last_of('.');
    std::string name = dot == std::string::npos ? mGame : mGame.substr(dot + 1);
    if (name.empty()) name = GetName();
    if (name.size() > 16) name.resize(16);
    return name;
}

std::vector<std::string> ModMap::OrderedGroups() const
{
    std::vector<std::string> groups = mGroups;
    for (const ModEntry& e : mEntries)
    {
        if (std::find(groups.begin(), groups.end(), e.mGroup) == groups.end())
        {
            groups.push_back(e.mGroup);
        }
    }
    return groups;
}

const char* ModKindName(ModKind kind)
{
    switch (kind)
    {
    case ModKind::Toggle: return "Toggle";
    case ModKind::Int: return "Int";
    case ModKind::Float: return "Float";
    case ModKind::Choice: return "Choice";
    case ModKind::Action: return "Action";
    case ModKind::Display: return "Display";
    case ModKind::Bar: return "Bar";
    default: return "?";
    }
}

const char* ModSourceName(ModSource source)
{
    switch (source)
    {
    case ModSource::Variable: return "Variable";
    case ModSource::Address: return "Address";
    case ModSource::Symbol: return "Symbol";
    case ModSource::Request: return "Request";
    case ModSource::StartupOption: return "Startup Option";
    case ModSource::Display: return "Display";
    default: return "?";
    }
}

std::vector<ModMap*> ModMap_FindAll(const std::string& game)
{
    std::vector<ModMap*> maps;
    AssetManager* am = AssetManager::Get();
    if (am == nullptr)
    {
        return maps;
    }
    std::vector<std::string> names;
    for (auto& pair : am->GetAssetMap())
    {
        if (pair.second != nullptr && pair.second->mType == ModMap::GetStaticType())
        {
            names.push_back(pair.first);
        }
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names)
    {
        ModMap* map = LoadAsset<ModMap>(name);
        if (map != nullptr && (game.empty() || map->mGame == game))
        {
            maps.push_back(map);
        }
    }
    return maps;
}
