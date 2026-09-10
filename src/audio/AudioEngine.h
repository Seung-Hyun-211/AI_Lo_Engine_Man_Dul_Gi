#pragma once

#include "core/NonCopyable.h"

#include <memory>
#include <string>

// Minimal audio: an XAudio2 mastering voice with two submixes (music, sfx),
// fire-and-forget PCM WAV one-shots, and one looping music track. Main-thread
// API; XAudio2 runs its own mixer thread. If XAudio2 is unavailable the engine
// constructs fine and every call is a silent no-op (the game still runs).
// See docs/audio-design.md, docs/roadmap.md (rec 3).
namespace engine::audio
{
    class AudioEngine final : private core::NonCopyable
    {
    public:
        AudioEngine();
        ~AudioEngine();

        // 0..1, clamped. Master scales everything; music/sfx scale their submix.
        void SetMasterVolume(float volume01);
        void SetMusicVolume(float volume01);
        void SetSfxVolume(float volume01);

        // Load + play once. Missing/undecodable file logs and does nothing.
        void PlaySfx(const std::string& wavPath);

        // Replace the current looping music (empty path just stops).
        void PlayMusic(const std::string& wavPath);
        void StopMusic();

        // Once per frame from the main loop: reaps finished one-shot voices.
        void Update();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
