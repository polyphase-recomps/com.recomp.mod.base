/**
 * @file ModBaseUtil.h
 * @brief Small helpers every recomp runtime needs (header only, no engine dependency): SHA-1,
 *        reading a text file, and the few game.json values the runtimes look up.
 *
 * game.json is the recomp game package's description (Recomp/game.json, shipped as
 * Assets/Recomp/Live/game.json): "title", "config" (the recompiler config) and a "rom" block
 * naming the player's file ("file", "sha1", "size"). Its keys are unique, so a value is found
 * by its key alone.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace RecompUtil
{
// SHA-1 of a buffer, as 40 lowercase hex digits.
inline std::string Sha1Hex(const uint8_t* data, size_t size)
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    auto block = [&](const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
        {
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        }
        for (int i = 16; i < 80; i++)
        {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    };
    const size_t full = size / 64 * 64;
    for (size_t i = 0; i < full; i += 64)
    {
        block(data + i);
    }
    uint8_t tail[128] = {0};
    const size_t rest = size - full;
    if (rest > 0)
    {
        memcpy(tail, data + full, rest);
    }
    tail[rest] = 0x80;
    const size_t tailSize = (rest + 9 <= 64) ? 64 : 128;
    const uint64_t bits = (uint64_t)size * 8;
    for (int i = 0; i < 8; i++)
    {
        tail[tailSize - 1 - i] = (uint8_t)(bits >> (i * 8));
    }
    block(tail);
    if (tailSize == 128)
    {
        block(tail + 64);
    }
    char out[41];
    for (int i = 0; i < 5; i++)
    {
        snprintf(out + i * 8, 9, "%08x", h[i]);
    }
    return out;
}

inline std::string Sha1Hex(const std::vector<uint8_t>& data)
{
    return Sha1Hex(data.data(), data.size());
}

// A whole file as text ("" if it can't be read).
inline std::string ReadText(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::stringstream text;
    if (in.is_open())
    {
        text << in.rdbuf();
    }
    return text.str();
}

// A string value by key ("" if absent): "config", "title", "sha1", "file" in game.json.
inline std::string JsonString(const std::string& json, const char* key)
{
    const std::string quoted = std::string("\"") + key + "\"";
    size_t at = json.find(quoted);
    if (at == std::string::npos) return "";
    at = json.find(':', at + quoted.size());
    if (at == std::string::npos) return "";
    at = json.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos || json[at] != '"') return "";
    const size_t end = json.find('"', at + 1);
    return end == std::string::npos ? "" : json.substr(at + 1, end - at - 1);
}

// A number value by key (fallback if absent): "size" in game.json.
inline long long JsonNumber(const std::string& json, const char* key, long long fallback = 0)
{
    const std::string quoted = std::string("\"") + key + "\"";
    size_t at = json.find(quoted);
    if (at == std::string::npos) return fallback;
    at = json.find(':', at + quoted.size());
    if (at == std::string::npos) return fallback;
    at = json.find_first_not_of(" \t\r\n", at + 1);
    if (at == std::string::npos) return fallback;
    char* end = nullptr;
    const long long value = strtoll(json.c_str() + at, &end, 0);
    return end == json.c_str() + at ? fallback : value;
}

// The file name of a path.
inline std::string FileName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace RecompUtil
