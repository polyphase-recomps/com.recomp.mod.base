/**
 * @file ModBaseSound.h
 * @brief Menu sounds and music for the Recomp menus and the launcher.
 *
 * The engine's 2D sounds (AudioManager) aren't exported to addon DLLs, so these play through
 * Audio3D voices of their own: hidden, unsaved children of the node that plays them (a menu
 * controller, a launcher), with constant attenuation and kept at the world's audio receiver,
 * which gives full volume and no panning. A voice lives as long as its owner: a sound that must
 * outlast a scene change waits for it to end first (the launcher's Start / Quit sounds).
 */
#pragma once

#include "ModBaseApi.h"

class Node;
class SoundWave;

namespace RecompSound
{
// Plays `wave` on one of the owner's voices: `channel` "" = the next of a few effect voices
// (sounds overlap), else a named voice of its own (music: one at a time, replaced).
MODBASE_API void Play(Node* owner, SoundWave* wave, float volume, bool loop = false, const char* channel = "");
// Stops a named voice ("" = every voice of the owner).
MODBASE_API void Stop(Node* owner, const char* channel = "");
// Keeps the owner's voices at the audio receiver; call from the owner's Tick.
MODBASE_API void Follow(Node* owner);
// The length of a sound in seconds (0 = none).
MODBASE_API float Duration(SoundWave* wave);
}
