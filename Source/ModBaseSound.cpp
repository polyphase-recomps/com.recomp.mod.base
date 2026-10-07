/**
 * @file ModBaseSound.cpp
 * @brief Menu sounds and music through hidden Audio3D voices (see ModBaseSound.h).
 */

#include "ModBaseSound.h"

#include "Assets/SoundWave.h"
#include "Nodes/3D/Audio3d.h"
#include "World.h"

#include <cstdio>
#include <string>

namespace
{
constexpr int kEffectVoices = 4;
constexpr const char* kVoicePrefix = "RecompSound_";

Audio3D* Voice(Node* owner, const std::string& name)
{
    Node* node = owner->FindChild(name, false);
    Audio3D* voice = node != nullptr ? node->As<Audio3D>() : nullptr;
    if (voice == nullptr)
    {
        voice = owner->CreateChild<Audio3D>(name.c_str());
        if (voice == nullptr) return nullptr;
        voice->SetTransient(true);
#if EDITOR
        voice->mHiddenInTree = true;
#endif
        voice->SetAutoPlay(false);
        // heard the same everywhere: no fall-off, and (at the receiver) no panning
        voice->SetAttenuationFunc(AttenuationFunc::Constant);
        voice->SetInnerRadius(1.0e6f);
        voice->SetOuterRadius(2.0e6f);
    }
    return voice;
}

void Place(Audio3D* voice)
{
    World* world = voice->GetWorld();
    Node3D* receiver = world != nullptr ? world->GetAudioReceiver() : nullptr;
    voice->SetWorldPosition(receiver != nullptr ? receiver->GetWorldPosition() : glm::vec3(0.0f));
}

bool IsVoice(Node* node)
{
    return node->As<Audio3D>() != nullptr && node->GetName().compare(0, 12, kVoicePrefix) == 0;
}

int& NextEffect(Node* owner)
{
    // one round-robin counter is enough: owners rarely play at the same moment
    static int sNext = 0;
    (void)owner;
    return sNext;
}
}

namespace RecompSound
{
void Play(Node* owner, SoundWave* wave, float volume, bool loop, const char* channel)
{
    if (owner == nullptr || wave == nullptr)
    {
        return;
    }
    std::string name = kVoicePrefix;
    if (channel != nullptr && channel[0] != 0)
    {
        name += channel;
    }
    else
    {
        int& next = NextEffect(owner);
        char index[8];
        snprintf(index, sizeof(index), "fx%d", next);
        next = (next + 1) % kEffectVoices;
        name += index;
    }
    Audio3D* voice = Voice(owner, name);
    if (voice == nullptr)
    {
        return;
    }
    voice->StopAudio();
    voice->SetSoundWave(wave);
    voice->SetVolume(volume);
    voice->SetLoop(loop);
    voice->ResetAudio();
    Place(voice);
    voice->PlayAudio();
}

void Stop(Node* owner, const char* channel)
{
    if (owner == nullptr)
    {
        return;
    }
    const bool all = channel == nullptr || channel[0] == 0;
    const std::string name = std::string(kVoicePrefix) + (all ? "" : channel);
    for (uint32_t i = 0; i < owner->GetNumChildren(); ++i)
    {
        Node* child = owner->GetChild((int32_t)i);
        if (IsVoice(child) && (all || child->GetName() == name))
        {
            child->As<Audio3D>()->StopAudio();
        }
    }
}

void Follow(Node* owner)
{
    if (owner == nullptr)
    {
        return;
    }
    for (uint32_t i = 0; i < owner->GetNumChildren(); ++i)
    {
        Node* child = owner->GetChild((int32_t)i);
        if (IsVoice(child))
        {
            Audio3D* voice = child->As<Audio3D>();
            if (voice->IsPlaying()) Place(voice);
        }
    }
}

float Duration(SoundWave* wave)
{
    return wave != nullptr ? wave->GetDuration() : 0.0f;
}
}
