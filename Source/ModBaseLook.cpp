/**
 * @file ModBaseLook.cpp
 * @brief Values the generated scenes' look sets, kept as the user left them (ModBaseLook.h).
 */

#include "ModBaseLook.h"

#include "Nodes/Node.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
constexpr const char* kNewTag = "look:new";

std::string Prefix(const char* key)
{
    return std::string("look:") + key + "=";
}

// Node::mTags (protected, saved as the "Tags" property), read through a member pointer.
struct TagAccess : Node
{
    static std::vector<std::string>& Of(Node* node)
    {
        return node->*(&TagAccess::mTags);
    }
};

// The node's tag "look:<key>=<value>" ("" = none).
std::string FindTag(Node* node, const char* key)
{
    const std::string prefix = Prefix(key);
    for (const std::string& tag : TagAccess::Of(node))
    {
        if (tag.compare(0, prefix.size(), prefix) == 0) return tag;
    }
    return std::string();
}

bool Same(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}
}

namespace RecompLook
{
void Remember(Node* node, const char* key, float value)
{
    if (node == nullptr) return;
    const std::string old = FindTag(node, key);
    char text[64];
    snprintf(text, sizeof(text), "%s%g", Prefix(key).c_str(), value);
    if (old == text) return;
    if (!old.empty()) node->RemoveTag(old);
    node->AddTag(text);
}

float Recall(Node* node, const char* key, float fallback)
{
    if (node == nullptr) return fallback;
    const std::string tag = FindTag(node, key);
    return tag.empty() ? fallback : (float)atof(tag.c_str() + Prefix(key).size());
}

float Apply(Node* node, const char* key, float current, float wanted)
{
    if (node == nullptr) return wanted;
    const std::string tag = FindTag(node, key);
    float value = wanted;
    if (!tag.empty())
    {
        // changed in the scene since the look set it: the user's
        const float set = (float)atof(tag.c_str() + Prefix(key).size());
        if (!Same(current, set)) value = current;
    }
    else if (!node->HasTag(kNewTag))
    {
        // a node from before the look remembered anything: what it has stays (this time)
        value = current;
    }
    Remember(node, key, value);
    return value;
}

void MarkNew(Node* node)
{
    if (node != nullptr) node->AddTag(kNewTag);
}
}
