/**
 * @file ModBaseLua.cpp
 * @brief Lua tables Recomp and Mods (see ModBaseLua.h). Every call goes through the
 *        engine's Lua_* wrappers (an addon must not use its own copy of Lua).
 */

#include "ModBaseLua.h"

#include "ModBaseProvider.h"
#include "ModBaseSettings.h"
#include "ModBaseWidgets.h"

#include "Plugins/PolyphaseEngineAPI.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace
{
PolyphaseEngineAPI* sApi = nullptr;

typedef int (*LuaFunction)(lua_State* L);
struct LuaReg
{
    const char* name;
    LuaFunction func;
};

constexpr int kLuaTNumber = 3;
constexpr int kLuaTString = 4;

int OptInt(lua_State* L, int arg, int fallback)
{
    if (sApi->Lua_gettop(L) < arg || sApi->Lua_isnil(L, arg)) return fallback;
    return (int)sApi->LuaL_checkinteger(L, arg);
}

void PushValue(lua_State* L, const RecompValue& v)
{
    if (v.isText) sApi->Lua_pushstring(L, v.text.c_str());
    else if (v.number == (double)(long long)v.number) sApi->Lua_pushinteger(L, (long long)v.number);
    else sApi->Lua_pushnumber(L, v.number);
}

RecompProvider* Live()
{
    RecompProvider* p = Recomp_FindProvider();
    return (p != nullptr && p->IsLive()) ? p : nullptr;
}

void SetField(lua_State* L, const char* field, const std::string& value)
{
    sApi->Lua_pushstring(L, value.c_str());
    sApi->Lua_setfield(L, -2, field);
}

// address argument: a number, "0x8013..." or a symbol name (GameCube)
bool ResolveAddress(lua_State* L, int arg, RecompProvider* p, uint64_t& address)
{
    if (sApi->Lua_type(L, arg) == kLuaTNumber)
    {
        address = (uint64_t)(long long)sApi->Lua_tonumber(L, arg);
        return true;
    }
    const std::string text = sApi->LuaL_checkstring(L, arg);
    char* end = nullptr;
    const unsigned long long v = strtoull(text.c_str(), &end, 0);
    if (end != text.c_str() && *end == 0)
    {
        address = v;
        return true;
    }
    return p->ResolveSymbol(text, address);
}

// ---- Recomp ------------------------------------------------------------------------------
int R_IsRunning(lua_State* L)
{
    sApi->Lua_pushboolean(L, Live() != nullptr);
    return 1;
}

int R_Game(lua_State* L)
{
    RecompProvider* p = Recomp_FindProvider();
    if (p == nullptr)
    {
        sApi->Lua_pushnil(L);
        return 1;
    }
    sApi->Lua_pushstring(L, p->GamePackage().c_str());
    sApi->Lua_pushstring(L, p->RuntimeId());
    return 2;
}

int R_Get(lua_State* L)
{
    RecompProvider* p = Live();
    RecompValue v;
    if (p != nullptr && p->Get(sApi->LuaL_checkstring(L, 1), OptInt(L, 2, 0), v)) PushValue(L, v);
    else sApi->Lua_pushnil(L);
    return 1;
}

int R_Set(lua_State* L)
{
    RecompProvider* p = Live();
    const std::string name = sApi->LuaL_checkstring(L, 1);
    RecompValue v = sApi->Lua_type(L, 2) == kLuaTString ? RecompValue::Text(sApi->Lua_tostring(L, 2))
                                                         : RecompValue::Number(sApi->LuaL_checknumber(L, 2));
    sApi->Lua_pushboolean(L, p != nullptr && p->Set(name, OptInt(L, 3, 0), v));
    return 1;
}

int R_Request(lua_State* L)
{
    RecompProvider* p = Live();
    const std::string name = sApi->LuaL_checkstring(L, 1);
    std::vector<int> args;
    for (int i = 2; i <= sApi->Lua_gettop(L); ++i) args.push_back((int)sApi->LuaL_checkinteger(L, i));
    const int id = p != nullptr ? p->Request(name, args) : 0;
    if (id > 0) sApi->Lua_pushinteger(L, id);
    else sApi->Lua_pushnil(L);
    return 1;
}

int R_Result(lua_State* L)
{
    RecompProvider* p = Recomp_FindProvider();
    int result = 0;
    if (p != nullptr && p->Result((int)sApi->LuaL_checkinteger(L, 1), result)) sApi->Lua_pushinteger(L, result);
    else sApi->Lua_pushnil(L);
    return 1;
}

int R_Read(lua_State* L)
{
    RecompProvider* p = Live();
    uint64_t address = 0;
    RecompValue v;
    const RecompType type = Recomp_ParseType(sApi->Lua_gettop(L) >= 2 ? sApi->LuaL_checkstring(L, 2) : "s32");
    if (p != nullptr && ResolveAddress(L, 1, p, address) && p->ReadAddress(address, type, v)) PushValue(L, v);
    else sApi->Lua_pushnil(L);
    return 1;
}

int R_Write(lua_State* L)
{
    RecompProvider* p = Live();
    uint64_t address = 0;
    const RecompType type = Recomp_ParseType(sApi->LuaL_checkstring(L, 2));
    const RecompValue v = RecompValue::Number(sApi->LuaL_checknumber(L, 3));
    sApi->Lua_pushboolean(L, p != nullptr && ResolveAddress(L, 1, p, address) && p->WriteAddress(address, type, v));
    return 1;
}

int R_Variables(lua_State* L)
{
    std::vector<RecompVarInfo> vars;
    if (RecompProvider* p = Recomp_FindProvider()) p->Variables(vars);
    sApi->Lua_createtable(L, (int)vars.size(), 0);
    for (size_t i = 0; i < vars.size(); ++i)
    {
        sApi->Lua_pushinteger(L, (long long)i + 1);
        sApi->Lua_createtable(L, 0, 4);
        SetField(L, "name", vars[i].name);
        SetField(L, "type", Recomp_TypeName(vars[i].type));
        sApi->Lua_pushinteger(L, vars[i].count);
        sApi->Lua_setfield(L, -2, "count");
        SetField(L, "help", vars[i].help);
        sApi->Lua_rawset(L, -3);
    }
    return 1;
}

int R_Requests(lua_State* L)
{
    std::vector<RecompRequestInfo> reqs;
    if (RecompProvider* p = Recomp_FindProvider()) p->Requests(reqs);
    sApi->Lua_createtable(L, (int)reqs.size(), 0);
    for (size_t i = 0; i < reqs.size(); ++i)
    {
        sApi->Lua_pushinteger(L, (long long)i + 1);
        sApi->Lua_createtable(L, 0, 2);
        SetField(L, "name", reqs[i].name);
        SetField(L, "help", reqs[i].help);
        sApi->Lua_rawset(L, -3);
    }
    return 1;
}

int R_SetInputBlocked(lua_State* L)
{
    Recomp_SetInputBlocked(sApi->Lua_toboolean(L, 1) != 0);
    return 0;
}

int R_IsInputBlocked(lua_State* L)
{
    sApi->Lua_pushboolean(L, Recomp_IsInputCaptured());
    return 1;
}

// ---- Mods --------------------------------------------------------------------------------
int M_Get(lua_State* L)
{
    float value = 0.0f;
    if (ModSettings::Get().GetValue(sApi->LuaL_checkstring(L, 1), value)) sApi->Lua_pushnumber(L, value);
    else sApi->Lua_pushnil(L);
    return 1;
}

int M_Text(lua_State* L)
{
    sApi->Lua_pushstring(L, ModSettings::Get().ValueText(sApi->LuaL_checkstring(L, 1)).c_str());
    return 1;
}

int M_Set(lua_State* L)
{
    const std::string id = sApi->LuaL_checkstring(L, 1);
    sApi->Lua_pushboolean(L, ModSettings::Get().SetValue(id, (float)sApi->LuaL_checknumber(L, 2)));
    return 1;
}

int M_Step(lua_State* L)
{
    const std::string id = sApi->LuaL_checkstring(L, 1);
    sApi->Lua_pushboolean(L, ModSettings::Get().Step(id, OptInt(L, 2, 1)));
    return 1;
}

int M_Reset(lua_State* L)
{
    if (sApi->Lua_gettop(L) >= 1 && !sApi->Lua_isnil(L, 1)) ModSettings::Get().Reset(sApi->LuaL_checkstring(L, 1));
    else ModSettings::Get().ResetAll();
    return 0;
}

int M_Save(lua_State* L)
{
    sApi->Lua_pushboolean(L, ModSettings::Get().Save());
    return 1;
}

int M_List(lua_State* L)
{
    ModMap* map = ModSettings::Get().GetMap();
    const size_t n = map ? map->mEntries.size() : 0;
    sApi->Lua_createtable(L, (int)n, 0);
    for (size_t i = 0; i < n; ++i)
    {
        const ModEntry& e = map->mEntries[i];
        sApi->Lua_pushinteger(L, (long long)i + 1);
        sApi->Lua_createtable(L, 0, 5);
        SetField(L, "id", e.mId);
        SetField(L, "label", e.mLabel);
        SetField(L, "group", e.mGroup);
        SetField(L, "kind", ModKindName(e.mKind));
        SetField(L, "help", e.mHelp);
        sApi->Lua_rawset(L, -3);
    }
    return 1;
}

RecompMenuController* SettingsMenu()
{
    // the generated scene's controller: the one whose UI has "Pages" (else the first)
    RecompMenuController* first = nullptr;
    for (RecompMenuController* c : RecompMenuController::GetAll())
    {
        if (first == nullptr) first = c;
        Node* root = c->GetParent();
        if (root != nullptr && root->FindChild("Pages", true) != nullptr) return c;
    }
    return first;
}

int M_Open(lua_State*)
{
    if (RecompMenuController* c = SettingsMenu()) c->Open();
    return 0;
}

int M_Close(lua_State*)
{
    if (RecompMenuController* c = SettingsMenu()) c->Close();
    return 0;
}

int M_Toggle(lua_State*)
{
    if (RecompMenuController* c = SettingsMenu()) c->Toggle();
    return 0;
}
}

void ModBaseLua::Register(lua_State* L, PolyphaseEngineAPI* api)
{
    if (L == nullptr || api == nullptr || api->Lua_createtable == nullptr || api->LuaL_setfuncs == nullptr ||
        api->Lua_setglobal == nullptr)
    {
        return;
    }
    sApi = api;
    static const LuaReg kRecomp[] = {
        {"IsRunning", R_IsRunning}, {"Game", R_Game},       {"Get", R_Get},
        {"Set", R_Set},             {"Request", R_Request}, {"Result", R_Result},
        {"Read", R_Read},           {"Write", R_Write},     {"Variables", R_Variables},
        {"Requests", R_Requests},   {"SetInputBlocked", R_SetInputBlocked},
        {"IsInputBlocked", R_IsInputBlocked}, {nullptr, nullptr},
    };
    sApi->Lua_createtable(L, 0, 12);
    sApi->LuaL_setfuncs(L, kRecomp, 0);
    sApi->Lua_setglobal(L, "Recomp");

    static const LuaReg kMods[] = {
        {"Get", M_Get},     {"Text", M_Text}, {"Set", M_Set},   {"Step", M_Step},
        {"Reset", M_Reset}, {"Save", M_Save}, {"List", M_List}, {"Open", M_Open},
        {"Close", M_Close}, {"Toggle", M_Toggle}, {nullptr, nullptr},
    };
    sApi->Lua_createtable(L, 0, 10);
    sApi->LuaL_setfuncs(L, kMods, 0);
    sApi->Lua_setglobal(L, "Mods");
}
