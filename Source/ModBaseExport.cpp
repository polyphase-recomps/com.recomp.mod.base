/**
 * @file ModBaseExport.cpp
 * @brief A Mod Map's entries as a Recomp Zoo mod import (ModBaseExport.h).
 */

#include "ModBaseExport.h"

#include "ModBaseModMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <sstream>

namespace
{
const char* kTypeNames[] = {"U8", "S8", "U16", "S16", "U32", "S32", "F32", "Str"};

std::string Quote(const std::string& text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                out += esc;
            }
            else
            {
                out += (char)c; // UTF-8 as it is
            }
        }
    }
    return out + "\"";
}

// A float as JSON: the shortest text that reads back as the same float, so a Choice's default
// and its values compare equal in the Zoo.
std::string Number(float value)
{
    char text[32];
    for (int digits = 6; digits <= 9; ++digits)
    {
        snprintf(text, sizeof(text), "%.*g", digits, value);
        if (std::strtof(text, nullptr) == value) break;
    }
    return text;
}

std::string Bool(bool value)
{
    return value ? "true" : "false";
}

std::string NowUtc()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc = {};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

bool Finite(const ModEntry& e)
{
    for (float v : {e.mMin, e.mMax, e.mStep, e.mDefault})
    {
        if (!std::isfinite(v)) return false;
    }
    for (float v : e.mChoiceValues)
    {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

// Why the Zoo would refuse the entry ("" = it's accepted): the schema's and the Zoo's own checks.
std::string Refusal(const ModEntry& e)
{
    const bool needsName = e.mSource == ModSource::Variable || e.mSource == ModSource::Symbol ||
                           e.mSource == ModSource::Request || e.mSource == ModSource::StartupOption ||
                           e.mSource == ModSource::Display;
    if (e.mKind >= ModKind::Count || e.mSource >= ModSource::Count) return "unknown kind or source";
    if (!Finite(e)) return "a number is not finite";
    if (needsName && e.mName.find_first_not_of(" \t") == std::string::npos)
        return std::string(ModSourceName(e.mSource)) + " source needs a name";
    if (e.mSource == ModSource::Display && e.mName.compare(0, 8, "display.") != 0)
        return "a Display source's name must start with display.";
    if (e.mSource == ModSource::Address && e.mAddress == 0) return "Address source needs an address";
    if (e.mSource == ModSource::Variable && e.mIndex < 0) return "negative index";
    if ((e.mSource == ModSource::Address || e.mSource == ModSource::Symbol) && e.mType >= RecompType::Count)
        return "unknown type";
    if (e.mKind == ModKind::Int || e.mKind == ModKind::Float)
    {
        if (e.mStep <= 0.0f) return "step must be above 0";
        if (e.mDefault < e.mMin || e.mDefault > e.mMax) return "default is not between min and max";
    }
    if (e.mKind == ModKind::Toggle && e.mDefault != 0.0f && e.mDefault != 1.0f) return "a Toggle's default must be 0 or 1";
    if (e.mKind == ModKind::Choice)
    {
        const size_t count = std::min(e.mChoiceLabels.size(), e.mChoiceValues.size());
        if (count == 0) return "a Choice needs choices";
        bool found = false;
        for (size_t i = 0; i < count; ++i) found |= e.mChoiceValues[i] == e.mDefault;
        if (!found) return "the default is not one of the choices' values";
    }
    if (e.mKind == ModKind::Bar && e.mMaxName.find_first_not_of(" \t") == std::string::npos)
        return "a Bar needs its maximum's variable";
    return std::string();
}
}

std::string ModZoo_Slug(const std::string& title)
{
    std::string out;
    int depth = 0;
    bool dash = false;
    for (size_t i = 0; i < title.size(); ++i)
    {
        const unsigned char c = (unsigned char)title[i];
        if (c == '(' || c == '[') { ++depth; continue; }
        if (c == ')' || c == ']') { depth = depth > 0 ? depth - 1 : 0; dash = true; continue; }
        if (depth > 0) continue;
        if (c == '\'') continue; // "Kirby's" -> "kirbys"
        if (c == '&')
        {
            dash = true;
            if (!out.empty()) out += '-';
            out += "and";
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
        {
            if (dash && !out.empty() && out.back() != '-') out += '-';
            dash = false;
            out += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        else
        {
            dash = true; // spaces, punctuation, non-ASCII
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

bool ModZoo_IsValidGameId(const std::string& id)
{
    // ^[a-z0-9]+(?:-[a-z0-9]+)*$
    if (id.empty() || id.front() == '-' || id.back() == '-') return false;
    for (size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && !(c == '-' && id[i - 1] != '-')) return false;
    }
    return true;
}

ModZooExportResult ModMap_ExportZoo(const ModMap& map, const ModZooExportOptions& options)
{
    ModZooExportResult result;
    if (!ModZoo_IsValidGameId(options.gameId))
    {
        result.errors.push_back("Game id \"" + options.gameId + "\" is not lowercase kebab-case (e.g. super-smash-bros)");
    }
    if (options.strategy != "merge" && options.strategy != "replace")
    {
        result.errors.push_back("Strategy must be merge or replace");
    }
    if (!result.errors.empty())
    {
        return result;
    }

    std::ostringstream o;
    o << "{\n";
    o << "  \"$schema\": \"https://polyphase-recomps.github.io/recomp-zoo/schemas/mod-import.schema.json\",\n";
    o << "  \"type\": \"polyphase-recomp-zoo/mods\",\n";
    o << "  \"schemaVersion\": 1,\n";
    o << "  \"gameId\": " << Quote(options.gameId) << ",\n";
    o << "  \"strategy\": " << Quote(options.strategy) << ",\n";
    o << "  \"generatedBy\": " << Quote("Polyphase Engine, com.recomp.mod.base (" + map.mGame + ")") << ",\n";
    o << "  \"generatedAt\": " << Quote(NowUtc()) << ",\n";
    o << "  \"mods\": [";

    std::set<std::string> ids;
    for (const ModEntry& e : map.mEntries)
    {
        const std::string name = e.mId.empty() ? "(no id)" : e.mId;
        if (e.mId.empty())
        {
            result.skipped.push_back(name + ": no id");
            continue;
        }
        if (!ids.insert(e.mId).second)
        {
            result.skipped.push_back(name + ": duplicate id");
            continue;
        }
        const std::string refusal = Refusal(e);
        if (!refusal.empty())
        {
            result.skipped.push_back(name + ": " + refusal);
            continue;
        }

        std::string label = e.mLabel;
        if (label.empty())
        {
            label = e.mId;
            result.notes.push_back(name + ": no label, exported with its id as the label");
        }
        // (the schema wants a step above 0 for every kind; only Int / Float use it)
        const float step = e.mStep > 0.0f ? e.mStep : 1.0f;
        const int typeIndex = e.mType < RecompType::Count ? (int)e.mType : (int)RecompType::S32;

        o << (result.exported > 0 ? ",\n" : "\n");
        o << "    {\n";
        o << "      \"id\": " << Quote(e.mId) << ",\n";
        o << "      \"label\": " << Quote(label) << ",\n";
        o << "      \"group\": " << Quote(e.mGroup) << ",\n";
        o << "      \"help\": " << Quote(e.mHelp) << ",\n";
        o << "      \"kind\": " << Quote(ModKindName(e.mKind)) << ",\n";
        o << "      \"source\": " << Quote(ModSourceName(e.mSource)) << ",\n";
        o << "      \"name\": " << Quote(e.mName) << ",\n";
        o << "      \"index\": " << (e.mIndex > 0 ? e.mIndex : 0) << ",\n";
        if (e.mAddress != 0)
        {
            char address[24];
            snprintf(address, sizeof(address), "0x%llX", (unsigned long long)e.mAddress);
            o << "      \"address\": " << Quote(address) << ",\n";
        }
        else
        {
            o << "      \"address\": 0,\n";
        }
        o << "      \"type\": " << Quote(kTypeNames[typeIndex]) << ",\n";
        o << "      \"args\": " << Quote(e.mArgs) << ",\n";
        o << "      \"min\": " << Number(e.mMin) << ",\n";
        o << "      \"max\": " << Number(e.mMax) << ",\n";
        o << "      \"step\": " << Number(step) << ",\n";
        o << "      \"default\": " << Number(e.mDefault) << ",\n";
        o << "      \"choices\": [";
        if (e.mKind == ModKind::Choice)
        {
            const size_t count = std::min(e.mChoiceLabels.size(), e.mChoiceValues.size());
            for (size_t i = 0; i < count; ++i)
            {
                std::string choiceLabel = e.mChoiceLabels[i];
                if (choiceLabel.empty())
                {
                    choiceLabel = Number(e.mChoiceValues[i]);
                    result.notes.push_back(name + ": a choice without a label, exported as " + choiceLabel);
                }
                o << (i ? ", " : "") << "{ \"label\": " << Quote(choiceLabel)
                  << ", \"value\": " << Number(e.mChoiceValues[i]) << " }";
            }
        }
        o << "],\n";
        o << "      \"format\": " << Quote(e.mFormat) << ",\n";
        o << "      \"maxName\": " << Quote(e.mMaxName) << ",\n";
        o << "      \"persist\": " << Bool(e.mPersist) << ",\n";
        o << "      \"lock\": " << Bool(e.mLock) << ",\n";
        o << "      \"onTitle\": " << Bool(e.mOnTitle) << "\n";
        o << "    }";
        ++result.exported;
    }
    o << (result.exported > 0 ? "\n  ]\n" : "]\n");
    o << "}\n";
    result.json = o.str();
    return result;
}
