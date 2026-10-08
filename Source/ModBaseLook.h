/**
 * @file ModBaseLook.h
 * @brief Values the generated scenes' look sets, kept as the user left them.
 *
 * Generate / Update and the live previews apply the Mod Map's look (menu style, launcher and
 * disclaimer settings) to nodes the user may have edited since. A node remembers what the look
 * last set (a tag "look:<key>=<value>", saved with the scene): a value still equal to that is
 * the look's and follows the settings; one changed since in the scene is the user's and stays.
 * A node the generator has just made ("look:new") takes the look; a node from before this
 * existed keeps what it has the first time (that becomes what the look set).
 */
#pragma once

#include "ModBaseApi.h"

class Node;

namespace RecompLook
{
// The value to set: `wanted` when the look owns it, else `current` (the user's). Remembers it.
MODBASE_API float Apply(Node* node, const char* key, float current, float wanted);
// A value kept on the node by name (e.g. a button's height while it is hidden); `fallback`
// when there is none.
MODBASE_API float Recall(Node* node, const char* key, float fallback);
MODBASE_API void Remember(Node* node, const char* key, float value);
// Made by the generator just now: the look sets all its values.
MODBASE_API void MarkNew(Node* node);
}
