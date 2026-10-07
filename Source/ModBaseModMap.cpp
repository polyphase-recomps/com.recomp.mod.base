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
// to ASSET_VERSION_*). 2: the menu style. 3: the launcher.
constexpr uint32_t kModMapVersion = 8;

void ReadStyle(Stream& stream, ModStyle& s)
{
    s.mPanelColor = stream.ReadVec4();
    stream.ReadAsset(s.mPanelTexture);
    for (int i = 0; i < ModStyle::StateCount; ++i)
    {
        stream.ReadAsset(s.mButtonTextures[i]);
        s.mButtonColors[i] = stream.ReadVec4();
    }
    s.mButtonTextColor = stream.ReadVec4();
    s.mButtonTextSize = stream.ReadFloat();
    s.mTabTextSize = stream.ReadFloat();
    s.mHighlightColor = stream.ReadVec4();
    s.mHighlightWidth = stream.ReadFloat();
    stream.ReadAsset(s.mFont);
    s.mTitleColor = stream.ReadVec4();
    s.mTitleSize = stream.ReadFloat();
    s.mLabelColor = stream.ReadVec4();
    s.mInfoColor = stream.ReadVec4();
    s.mLabelSize = stream.ReadFloat();
    s.mValueColor = stream.ReadVec4();
    s.mValueSize = stream.ReadFloat();
    s.mNoteSize = stream.ReadFloat();
}

void WriteStyle(Stream& stream, const ModStyle& s)
{
    stream.WriteVec4(s.mPanelColor);
    stream.WriteAsset(s.mPanelTexture);
    for (int i = 0; i < ModStyle::StateCount; ++i)
    {
        stream.WriteAsset(s.mButtonTextures[i]);
        stream.WriteVec4(s.mButtonColors[i]);
    }
    stream.WriteVec4(s.mButtonTextColor);
    stream.WriteFloat(s.mButtonTextSize);
    stream.WriteFloat(s.mTabTextSize);
    stream.WriteVec4(s.mHighlightColor);
    stream.WriteFloat(s.mHighlightWidth);
    stream.WriteAsset(s.mFont);
    stream.WriteVec4(s.mTitleColor);
    stream.WriteFloat(s.mTitleSize);
    stream.WriteVec4(s.mLabelColor);
    stream.WriteVec4(s.mInfoColor);
    stream.WriteFloat(s.mLabelSize);
    stream.WriteVec4(s.mValueColor);
    stream.WriteFloat(s.mValueSize);
    stream.WriteFloat(s.mNoteSize);
}

// version 4: the style's and the launcher's later settings
void ReadLook4(Stream& stream, ModStyle& s, ModLauncherSettings& l)
{
    const uint8_t flags = stream.ReadUint8();
    s.mShowHighlight = (flags & 1) != 0;
    s.mButtonStateTint = (flags & 2) != 0;
    l.mTintBackground = (flags & 4) != 0;
    l.mPanelFullScreen = (flags & 8) != 0;
    s.mTintPanel = (flags & 16) == 0;
    s.mButtonUvScale = stream.ReadVec2();
    s.mButtonUvOffset = stream.ReadVec2();
    s.mButtonFit = stream.ReadUint8();
    stream.ReadAsset(s.mHeaderFont);
    stream.ReadAsset(s.mBodyFont);
    stream.ReadAsset(s.mButtonFont);
}

void WriteLook4(Stream& stream, const ModStyle& s, const ModLauncherSettings& l)
{
    stream.WriteUint8((uint8_t)((s.mShowHighlight ? 1 : 0) | (s.mButtonStateTint ? 2 : 0) | (l.mTintBackground ? 4 : 0) |
                                (l.mPanelFullScreen ? 8 : 0) | (s.mTintPanel ? 0 : 16)));
    stream.WriteVec2(s.mButtonUvScale);
    stream.WriteVec2(s.mButtonUvOffset);
    stream.WriteUint8(s.mButtonFit);
    stream.WriteAsset(s.mHeaderFont);
    stream.WriteAsset(s.mBodyFont);
    stream.WriteAsset(s.mButtonFont);
}

// version 5: sounds
void ReadSounds5(Stream& stream, ModStyle& s, ModLauncherSettings& l)
{
    stream.ReadAsset(s.mSoundMove);
    stream.ReadAsset(s.mSoundSelect);
    stream.ReadAsset(s.mSoundCancel);
    stream.ReadAsset(s.mSoundBack);
    s.mSoundVolume = stream.ReadFloat();
    stream.ReadAsset(l.mSoundStart);
    stream.ReadAsset(l.mSoundQuit);
    stream.ReadAsset(l.mMusic);
    l.mMusicVolume = stream.ReadFloat();
}

void WriteSounds5(Stream& stream, const ModStyle& s, const ModLauncherSettings& l)
{
    stream.WriteAsset(s.mSoundMove);
    stream.WriteAsset(s.mSoundSelect);
    stream.WriteAsset(s.mSoundCancel);
    stream.WriteAsset(s.mSoundBack);
    stream.WriteFloat(s.mSoundVolume);
    stream.WriteAsset(l.mSoundStart);
    stream.WriteAsset(l.mSoundQuit);
    stream.WriteAsset(l.mMusic);
    stream.WriteFloat(l.mMusicVolume);
}

// version 6: the logo's crop and fit
void ReadLogo6(Stream& stream, ModLauncherSettings& l)
{
    l.mLogoUvScale = stream.ReadVec2();
    l.mLogoUvOffset = stream.ReadVec2();
    l.mLogoFit = stream.ReadUint8();
}

void WriteLogo6(Stream& stream, const ModLauncherSettings& l)
{
    stream.WriteVec2(l.mLogoUvScale);
    stream.WriteVec2(l.mLogoUvOffset);
    stream.WriteUint8(l.mLogoFit);
}

// version 7: the launcher's deny and forget sounds
void ReadSounds7(Stream& stream, ModLauncherSettings& l)
{
    stream.ReadAsset(l.mSoundDeny);
    stream.ReadAsset(l.mSoundForget);
}

void WriteSounds7(Stream& stream, const ModLauncherSettings& l)
{
    stream.WriteAsset(l.mSoundDeny);
    stream.WriteAsset(l.mSoundForget);
}

// version 8: the launcher's footer
void ReadFooter8(Stream& stream, ModLauncherSettings& l)
{
    l.mShowFooter = stream.ReadUint8() != 0;
    stream.ReadString(l.mFooterText);
    stream.ReadAsset(l.mFooterLogo);
    l.mFooterLogoSize = stream.ReadVec2();
    stream.ReadString(l.mVersionFormat);
}

void WriteFooter8(Stream& stream, const ModLauncherSettings& l)
{
    stream.WriteUint8(l.mShowFooter ? 1 : 0);
    stream.WriteString(l.mFooterText);
    stream.WriteAsset(l.mFooterLogo);
    stream.WriteVec2(l.mFooterLogoSize);
    stream.WriteString(l.mVersionFormat);
}

void ReadLauncher(Stream& stream, ModLauncherSettings& l)
{
    stream.ReadString(l.mTitle);
    stream.ReadString(l.mSubtitle);
    stream.ReadAsset(l.mLogo);
    l.mLogoSize = stream.ReadVec2();
    stream.ReadAsset(l.mBackground);
    l.mBackgroundColor = stream.ReadVec4();
    l.mPanelSize = stream.ReadVec2();
    l.mPosition = stream.ReadInt32();
    stream.ReadString(l.mPlayLabel);
    stream.ReadString(l.mBrowseLabel);
    stream.ReadString(l.mForgetLabel);
    stream.ReadString(l.mModsLabel);
    stream.ReadString(l.mQuitLabel);
    const uint8_t flags = stream.ReadUint8();
    l.mShowForget = (flags & 1) != 0;
    l.mShowMods = (flags & 2) != 0;
    l.mShowQuit = (flags & 4) != 0;
    l.mAutoStart = (flags & 8) != 0;
    stream.ReadAsset(l.mGameScene);
}

void WriteLauncher(Stream& stream, const ModLauncherSettings& l)
{
    stream.WriteString(l.mTitle);
    stream.WriteString(l.mSubtitle);
    stream.WriteAsset(l.mLogo);
    stream.WriteVec2(l.mLogoSize);
    stream.WriteAsset(l.mBackground);
    stream.WriteVec4(l.mBackgroundColor);
    stream.WriteVec2(l.mPanelSize);
    stream.WriteInt32(l.mPosition);
    stream.WriteString(l.mPlayLabel);
    stream.WriteString(l.mBrowseLabel);
    stream.WriteString(l.mForgetLabel);
    stream.WriteString(l.mModsLabel);
    stream.WriteString(l.mQuitLabel);
    stream.WriteUint8((uint8_t)((l.mShowForget ? 1 : 0) | (l.mShowMods ? 2 : 0) | (l.mShowQuit ? 4 : 0) |
                                (l.mAutoStart ? 8 : 0)));
    stream.WriteAsset(l.mGameScene);
}
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
    mStyle = ModStyle();
    if (version >= 2)
    {
        ReadStyle(stream, mStyle);
    }
    mLauncher = ModLauncherSettings();
    if (version >= 3)
    {
        ReadLauncher(stream, mLauncher);
    }
    if (version >= 4)
    {
        ReadLook4(stream, mStyle, mLauncher);
    }
    if (version >= 5)
    {
        ReadSounds5(stream, mStyle, mLauncher);
    }
    if (version >= 6)
    {
        ReadLogo6(stream, mLauncher);
    }
    if (version >= 7)
    {
        ReadSounds7(stream, mLauncher);
    }
    if (version >= 8)
    {
        ReadFooter8(stream, mLauncher);
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
    WriteStyle(stream, mStyle);
    WriteLauncher(stream, mLauncher);
    WriteLook4(stream, mStyle, mLauncher);
    WriteSounds5(stream, mStyle, mLauncher);
    WriteLogo6(stream, mLauncher);
    WriteSounds7(stream, mLauncher);
    WriteFooter8(stream, mLauncher);
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
