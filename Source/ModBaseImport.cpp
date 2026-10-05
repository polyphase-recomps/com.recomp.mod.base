/**
 * @file ModBaseImport.cpp
 * @brief Automatic import of mod settings candidates (see ModBaseImport.h).
 */

#include "ModBaseImport.h"

#if EDITOR

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
// Comments out, strings kept (bridge names and help texts are strings).
std::string StripComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '"' || c == '\'')
        {
            const char quote = c;
            out += c;
            for (++i; i < text.size(); ++i)
            {
                out += text[i];
                if (text[i] == '\\' && i + 1 < text.size())
                {
                    out += text[++i];
                }
                else if (text[i] == quote)
                {
                    break;
                }
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n') ++i;
            out += '\n';
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            for (i += 2; i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'); ++i)
            {
                if (text[i] == '\n') out += '\n'; // keep line numbers
            }
            ++i;
            continue;
        }
        out += c;
    }
    return out;
}

std::string Unescape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            ++i;
            out += s[i] == 'n' ? ' ' : s[i];
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

int LineOf(const std::string& text, size_t pos)
{
    return 1 + (int)std::count(text.begin(), text.begin() + std::min(pos, text.size()), '\n');
}

RecompType TypeFromToken(const std::string& token)
{
    // PB_U8 .. PB_S32, PB_STR, PB_F32; GCN_VAR_U8 .. GCN_VAR_STR
    const size_t u = token.find_last_of('_');
    const std::string t = u == std::string::npos ? token : token.substr(u + 1);
    if (t == "U8") return RecompType::U8;
    if (t == "S8") return RecompType::S8;
    if (t == "U16") return RecompType::U16;
    if (t == "S16") return RecompType::S16;
    if (t == "U32") return RecompType::U32;
    if (t == "F32") return RecompType::F32;
    if (t == "STR") return RecompType::Str;
    return RecompType::S32;
}

int CountFromToken(const std::string& token)
{
    char* end = nullptr;
    const long v = strtol(token.c_str(), &end, 0);
    while (end && *end && std::isspace((unsigned char)*end)) ++end;
    return (end && *end == 0 && v > 0) ? (int)v : 1; // a macro or expression: unknown, 1
}

bool ReadText(const std::string& path, std::string& text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

#if defined(_WIN32)
void CollectSources(const std::string& dir, int depth, std::vector<std::string>& out)
{
    if (depth > 5) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            std::string lower;
            for (char c : name) lower += (char)std::tolower((unsigned char)c);
            // build output and tools; Native/ holds no generated code otherwise
            if (lower == "build" || lower == "bin" || lower == ".git" || lower == "lib" || lower == "patches" ||
                lower == "tools" || lower == "__pycache__")
            {
                continue;
            }
            CollectSources(dir + name + "\\", depth + 1, out);
        }
        else if (name.size() > 2 && (name.compare(name.size() - 2, 2, ".c") == 0 ||
                                     (name.size() > 4 && name.compare(name.size() - 4, 4, ".cpp") == 0)))
        {
            out.push_back(dir + name);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#endif

bool StartsWith(const std::string& s, const char* prefix)
{
    return s.compare(0, strlen(prefix), prefix) == 0;
}

bool IsIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

// Arguments of a call starting after its "(" at `pos`: split at top-level commas; `end`
// gets the position after the ")".
std::vector<std::string> SplitArgs(const std::string& text, size_t pos, size_t& end)
{
    std::vector<std::string> args;
    std::string current;
    int depth = 0;
    for (size_t i = pos; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '"' || c == '\'')
        {
            const char quote = c;
            current += c;
            for (++i; i < text.size(); ++i)
            {
                current += text[i];
                if (text[i] == '\\' && i + 1 < text.size()) current += text[++i];
                else if (text[i] == quote) break;
            }
            continue;
        }
        if (c == '(' || c == '{' || c == '[') ++depth;
        if (c == ')' || c == '}' || c == ']')
        {
            if (depth == 0 && c == ')')
            {
                args.push_back(current);
                end = i + 1;
                return args;
            }
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            args.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    end = text.size();
    return args;
}

// Table rows written through function-like macros
//   #define CARE(name, field, help) { name, &PARTNER_PARA.field, PB_S16, 1, 0, help }
//   CARE("happiness", happiness, "-100..100"),
// are expanded (one level) so the row patterns see them.
std::string ExpandRowMacros(const std::string& text)
{
    static const std::regex kDefine(R"rx(#define\s+([A-Za-z_]\w*)\(([^)]*)\)\s*(\{[^\n]*\}))rx");
    std::string expansions;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kDefine); it != std::sregex_iterator(); ++it)
    {
        const std::string name = (*it)[1];
        std::vector<std::string> params;
        std::istringstream in((*it)[2].str());
        std::string p;
        while (std::getline(in, p, ','))
        {
            p.erase(0, p.find_first_not_of(" \t"));
            p.erase(p.find_last_not_of(" \t") + 1);
            params.push_back(p);
        }
        const std::string body = (*it)[3];
        // each use: NAME( not preceded by an identifier character or #define
        for (size_t pos = text.find(name + "("); pos != std::string::npos; pos = text.find(name + "(", pos + 1))
        {
            if (pos > 0 && IsIdent(text[pos - 1])) continue;
            if (pos >= 8 && text.compare(pos - 8, 8, "#define ") == 0) continue;
            size_t end = 0;
            std::vector<std::string> args = SplitArgs(text, pos + name.size() + 1, end);
            if (args.size() != params.size()) continue;
            std::string out;
            for (size_t i = 0; i < body.size();)
            {
                if (IsIdent(body[i]) && (i == 0 || !IsIdent(body[i - 1])))
                {
                    size_t j = i;
                    while (j < body.size() && IsIdent(body[j])) ++j;
                    const std::string word = body.substr(i, j - i);
                    const auto found = std::find(params.begin(), params.end(), word);
                    out += found == params.end() ? word : args[found - params.begin()];
                    i = j;
                }
                else
                {
                    out += body[i++];
                }
            }
            expansions += out + ",\n";
        }
    }
    return expansions;
}
}

std::string ModImport_Label(const std::string& name)
{
    std::string out;
    bool upper = true;
    for (size_t i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        if (c == '_' || c == '.' || c == '-' || c == ' ')
        {
            if (!out.empty() && out.back() != ' ') out += ' ';
            upper = true;
            continue;
        }
        if (std::isupper((unsigned char)c) && i > 0 && std::islower((unsigned char)name[i - 1]) && out.back() != ' ')
        {
            out += ' ';
        }
        out += upper ? (char)std::toupper((unsigned char)c) : c;
        upper = false;
    }
    return out;
}

std::vector<ModImportCandidate> ModImport_ScanText(const std::string& source, const std::string& file)
{
    std::vector<ModImportCandidate> out;
    std::string text = StripComments(source);
    // macro-made rows are appended (their line numbers point past the end of the file)
    text += "\n" + ExpandRowMacros(text);
    static const std::regex kVar(
        R"rx(\{\s*"([A-Za-z_][\w.]*)"\s*,\s*[^,{}]+,\s*((?:PB|GCN_VAR|AGB_VAR)_\w+)\s*,\s*([^,{}]+),\s*([^,{}]+),\s*"((?:[^"\\]|\\.)*)"\s*\})rx");
    static const std::regex kGcnVar(
        R"rx(gcn_mod_variable\s*\(\s*"([A-Za-z_][\w.]*)"\s*,\s*[^,]+,\s*(GCN_VAR_\w+)\s*,\s*([^,]+),\s*([^,]+),\s*"((?:[^"\\]|\\.)*)")rx");
    static const std::regex kRequest(R"rx(\{\s*"([A-Za-z_][\w]*)"\s*,\s*&?\s*([A-Za-z_]\w*)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\})rx");
    static const std::regex kGcnRequest(R"rx(gcn_mod_request\s*\(\s*"([A-Za-z_][\w]*)"\s*,\s*[^,]+,\s*"((?:[^"\\]|\\.)*)")rx");
    static const std::regex kOption(R"rx(port_debug_values\s*\(\s*"([A-Za-z_]\w*)")rx");

    auto add = [&](ModImportCandidate c, size_t pos) {
        c.origin = file + ":" + std::to_string(LineOf(text, pos));
        out.push_back(c);
    };
    for (const std::regex* re : {&kVar, &kGcnVar})
    {
        for (auto it = std::sregex_iterator(text.begin(), text.end(), *re); it != std::sregex_iterator(); ++it)
        {
            ModImportCandidate c;
            c.what = ModImportCandidate::What::Variable;
            c.name = (*it)[1];
            c.type = TypeFromToken((*it)[2]);
            c.count = CountFromToken((*it)[3]);
            c.help = Unescape((*it)[5]);
            add(c, (size_t)it->position());
        }
    }
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kRequest); it != std::sregex_iterator(); ++it)
    {
        ModImportCandidate c;
        c.what = ModImportCandidate::What::Request;
        c.name = (*it)[1];
        c.help = Unescape((*it)[3]);
        add(c, (size_t)it->position());
    }
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kGcnRequest); it != std::sregex_iterator(); ++it)
    {
        ModImportCandidate c;
        c.what = ModImportCandidate::What::Request;
        c.name = (*it)[1];
        c.help = Unescape((*it)[2]);
        add(c, (size_t)it->position());
    }
    for (auto it = std::sregex_iterator(text.begin(), text.end(), kOption); it != std::sregex_iterator(); ++it)
    {
        ModImportCandidate c;
        c.what = ModImportCandidate::What::Option;
        c.name = (*it)[1];
        c.help = "game option (game.json \"options\")";
        add(c, (size_t)it->position());
    }
    return out;
}

std::vector<ModImportCandidate> ModImport_ScanOptions(const std::string& json)
{
    std::vector<ModImportCandidate> out;
    static const std::regex kOptions(R"rx("options"\s*:\s*"([^"]*)")rx");
    std::smatch m;
    if (!std::regex_search(json, m, kOptions))
    {
        return out;
    }
    std::istringstream words(m[1].str());
    std::string word;
    while (words >> word)
    {
        const size_t eq = word.find('=');
        ModImportCandidate c;
        c.what = ModImportCandidate::What::Option;
        c.name = word.substr(0, eq);
        c.defaultValue = eq == std::string::npos ? 1 : atoi(word.c_str() + eq + 1);
        c.help = "game.json option, default " + std::to_string(c.defaultValue);
        c.origin = "game.json";
        if (!c.name.empty()) out.push_back(c);
    }
    return out;
}

std::vector<ModImportCandidate> ModImport_FromSources(const std::string& packageDir)
{
    std::vector<ModImportCandidate> out;
#if defined(_WIN32)
    std::vector<std::string> files;
    CollectSources(packageDir + "Native\\", 0, files);
    for (const std::string& path : files)
    {
        std::string text;
        if (!ReadText(path, text)) continue;
        std::string rel = path.substr(std::min(path.size(), packageDir.size()));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        std::vector<ModImportCandidate> found = ModImport_ScanText(text, rel);
        out.insert(out.end(), found.begin(), found.end());
    }
#endif
    std::string json;
    if (ReadText(packageDir + "Assets/game.json", json))
    {
        std::vector<ModImportCandidate> options = ModImport_ScanOptions(json);
        out.insert(out.end(), options.begin(), options.end());
    }
    // one candidate per name and kind (an option named in both game.json and the code
    // keeps game.json's default)
    std::vector<ModImportCandidate> unique;
    std::set<std::string> seen;
    for (auto it = out.rbegin(); it != out.rend(); ++it)
    {
        const std::string key = std::to_string((int)it->what) + ":" + it->name;
        if (seen.insert(key).second) unique.push_back(*it);
    }
    std::reverse(unique.begin(), unique.end());
    return unique;
}

std::vector<ModImportCandidate> ModImport_FromLive(RecompProvider* provider)
{
    std::vector<ModImportCandidate> out;
    if (provider == nullptr)
    {
        return out;
    }
    std::vector<RecompVarInfo> vars;
    provider->Variables(vars);
    for (const RecompVarInfo& v : vars)
    {
        ModImportCandidate c;
        c.what = ModImportCandidate::What::Variable;
        c.name = v.name;
        c.help = v.help;
        c.type = v.type;
        c.count = v.count;
        c.origin = "live";
        out.push_back(c);
    }
    std::vector<RecompRequestInfo> requests;
    provider->Requests(requests);
    for (const RecompRequestInfo& r : requests)
    {
        ModImportCandidate c;
        c.what = ModImportCandidate::What::Request;
        c.name = r.name;
        c.help = r.help;
        c.origin = "live";
        out.push_back(c);
    }
    return out;
}

ModEntry ModImport_MakeEntry(const ModImportCandidate& c)
{
    ModEntry e;
    e.mId = c.name;
    e.mName = c.name;
    e.mHelp = c.help;
    std::string base = c.name;
    // group from a prefix: cheat_infhp -> "Cheats"
    if (StartsWith(c.name, "cheat_"))
    {
        e.mGroup = "Cheats";
        base = c.name.substr(6);
    }
    e.mLabel = ModImport_Label(base);
    std::string help;
    for (char ch : c.help) help += (char)std::tolower((unsigned char)ch);
    const bool looksBool = help.find("0/1") != std::string::npos || help.find("on/off") != std::string::npos ||
                           help.find("0 = off") != std::string::npos || help.find("1 = on") != std::string::npos ||
                           help.find("(bool") != std::string::npos || StartsWith(help, "1:") ||
                           StartsWith(help, "1 while");

    switch (c.what)
    {
    case ModImportCandidate::What::Request:
        e.mKind = ModKind::Action;
        e.mSource = ModSource::Request;
        if (e.mGroup.empty()) e.mGroup = "Actions";
        e.mPersist = false;
        break;
    case ModImportCandidate::What::Option:
        e.mSource = ModSource::StartupOption;
        e.mKind = (c.defaultValue == 0 || c.defaultValue == 1) ? ModKind::Toggle : ModKind::Int;
        e.mDefault = (float)c.defaultValue;
        e.mMin = 0.0f;
        e.mMax = e.mKind == ModKind::Toggle ? 1.0f : std::max(10.0f, (float)c.defaultValue * 4.0f);
        if (e.mGroup.empty()) e.mGroup = "Options";
        break;
    case ModImportCandidate::What::Variable:
    default:
        e.mSource = ModSource::Variable;
        if (c.type == RecompType::Str || c.count > 1)
        {
            e.mKind = ModKind::Display;
            e.mPersist = false;
        }
        else if (c.type == RecompType::F32)
        {
            e.mKind = ModKind::Float;
            e.mMin = -1000.0f;
            e.mMax = 1000.0f;
            e.mStep = 0.1f;
        }
        else if (StartsWith(c.name, "cheat_"))
        {
            // cheats are switches, except multipliers / amounts
            const bool amount = StartsWith(help, "n:") || help.find("x n") != std::string::npos ||
                                help.find("mult") != std::string::npos || help.find("amount") != std::string::npos;
            e.mKind = amount ? ModKind::Int : ModKind::Toggle;
        }
        else if (looksBool)
        {
            e.mKind = ModKind::Toggle;
        }
        else
        {
            e.mKind = ModKind::Display; // most published values are stats to watch
            e.mPersist = false;
        }
        if (e.mKind == ModKind::Toggle)
        {
            e.mMin = 0.0f;
            e.mMax = 1.0f;
        }
        else if (e.mKind == ModKind::Int)
        {
            e.mMin = 1.0f;
            e.mMax = 10.0f;
            e.mDefault = 1.0f;
        }
        if (e.mGroup.empty()) e.mGroup = e.mKind == ModKind::Display ? "Stats" : "Settings";
        if (StartsWith(c.name, "cheat_")) e.mLock = true;
        break;
    }
    if (c.count > 1 && c.what == ModImportCandidate::What::Variable)
    {
        e.mHelp += " (element 0 of " + std::to_string(c.count) + ")";
    }
    return e;
}

#endif
