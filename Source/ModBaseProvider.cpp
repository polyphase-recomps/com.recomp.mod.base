/**
 * @file ModBaseProvider.cpp
 * @brief Provider registry and type helpers (see ModBaseProvider.h).
 */

#include "ModBaseProvider.h"

#include <algorithm>
#include <cctype>

namespace
{
// Function-local static: valid whichever addon registers first (shipped builds call the
// addons' OnLoad in no particular order).
std::vector<RecompProvider*>& Registry()
{
    static std::vector<RecompProvider*> sProviders;
    return sProviders;
}
}

void Recomp_RegisterProvider(RecompProvider* provider)
{
    std::vector<RecompProvider*>& providers = Registry();
    if (provider != nullptr && std::find(providers.begin(), providers.end(), provider) == providers.end())
    {
        providers.push_back(provider);
    }
}

void Recomp_UnregisterProvider(RecompProvider* provider)
{
    std::vector<RecompProvider*>& providers = Registry();
    providers.erase(std::remove(providers.begin(), providers.end(), provider), providers.end());
}

const std::vector<RecompProvider*>& Recomp_Providers()
{
    return Registry();
}

RecompProvider* Recomp_FindProvider(const char* gamePackage)
{
    const std::vector<RecompProvider*>& providers = Registry();
    if (gamePackage != nullptr && gamePackage[0] != 0)
    {
        for (RecompProvider* p : providers)
        {
            if (p->GamePackage() == gamePackage)
            {
                return p;
            }
        }
        return nullptr;
    }
    for (RecompProvider* p : providers)
    {
        if (p->IsLive())
        {
            return p;
        }
    }
    return providers.empty() ? nullptr : providers.front();
}

const char* Recomp_TypeName(RecompType type)
{
    switch (type)
    {
    case RecompType::U8: return "u8";
    case RecompType::S8: return "s8";
    case RecompType::U16: return "u16";
    case RecompType::S16: return "s16";
    case RecompType::U32: return "u32";
    case RecompType::S32: return "s32";
    case RecompType::F32: return "f32";
    case RecompType::Str: return "str";
    default: return "?";
    }
}

RecompType Recomp_ParseType(const std::string& name)
{
    std::string n;
    for (char c : name)
    {
        n += (char)std::tolower((unsigned char)c);
    }
    if (n == "u8" || n == "byte") return RecompType::U8;
    if (n == "s8") return RecompType::S8;
    if (n == "u16") return RecompType::U16;
    if (n == "s16" || n == "short") return RecompType::S16;
    if (n == "u32") return RecompType::U32;
    if (n == "f32" || n == "float") return RecompType::F32;
    if (n == "str" || n == "string" || n == "text") return RecompType::Str;
    return RecompType::S32;
}

int Recomp_TypeSize(RecompType type)
{
    switch (type)
    {
    case RecompType::U8:
    case RecompType::S8: return 1;
    case RecompType::U16:
    case RecompType::S16: return 2;
    case RecompType::Str: return 1;
    default: return 4;
    }
}

bool Recomp_GetNumber(const std::string& name, int index, double& value)
{
    RecompProvider* provider = Recomp_FindProvider();
    RecompValue v;
    if (provider == nullptr || !provider->IsLive() || !provider->Get(name, index, v) || v.isText)
    {
        return false;
    }
    value = v.number;
    return true;
}

bool Recomp_GetText(const std::string& name, int index, std::string& value)
{
    RecompProvider* provider = Recomp_FindProvider();
    RecompValue v;
    if (provider == nullptr || !provider->IsLive() || !provider->Get(name, index, v))
    {
        return false;
    }
    if (v.isText)
    {
        value = v.text;
    }
    else
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.10g", v.number);
        value = buf;
    }
    return true;
}
