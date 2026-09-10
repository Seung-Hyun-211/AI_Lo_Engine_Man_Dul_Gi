#include "audio/AudioEngine.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace engine::audio
{
    namespace
    {
        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        struct WavData
        {
            WAVEFORMATEX format{};
            std::vector<std::uint8_t> samples;
        };

        std::uint32_t Read32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
        std::uint16_t Read16(const std::uint8_t* p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }

        // Minimal RIFF/WAVE reader: PCM (or IEEE float) `fmt ` + `data`. Other
        // chunks are skipped. Returns false on anything it does not understand.
        bool LoadWav(const std::string& path, WavData& out)
        {
            std::ifstream file;
            for (const std::string& prefix : { std::string{}, std::string{ "../../" }, std::string{ "../../../" } })
            {
                file.open(prefix + path, std::ios::binary);
                if (file.is_open()) break;
            }
            if (!file.is_open()) return false;

            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0
                || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
                return false;

            bool haveFmt = false, haveData = false;
            std::size_t pos = 12;
            while (pos + 8 <= bytes.size())
            {
                const char* id = reinterpret_cast<const char*>(bytes.data() + pos);
                const std::uint32_t size = Read32(bytes.data() + pos + 4);
                const std::size_t body = pos + 8;
                if (body + size > bytes.size()) break;

                if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16)
                {
                    const std::uint8_t* f = bytes.data() + body;
                    out.format.wFormatTag = Read16(f + 0);
                    out.format.nChannels = Read16(f + 2);
                    out.format.nSamplesPerSec = Read32(f + 4);
                    out.format.nAvgBytesPerSec = Read32(f + 8);
                    out.format.nBlockAlign = Read16(f + 12);
                    out.format.wBitsPerSample = Read16(f + 14);
                    out.format.cbSize = 0;
                    haveFmt = true;
                }
                else if (std::memcmp(id, "data", 4) == 0)
                {
                    out.samples.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                                       bytes.begin() + static_cast<std::ptrdiff_t>(body + size));
                    haveData = true;
                }
                pos = body + size + (size & 1u);   // chunks are word-aligned
            }
            return haveFmt && haveData && !out.samples.empty();
        }

        // Flags one-shot source voices done via a shared queue that Update() drains.
        struct VoiceCallback final : IXAudio2VoiceCallback
        {
            std::mutex mutex;
            std::vector<IXAudio2SourceVoice*> finished;

            void STDMETHODCALLTYPE OnBufferEnd(void* context) override
            {
                std::scoped_lock lock(mutex);
                finished.push_back(static_cast<IXAudio2SourceVoice*>(context));
            }
            void STDMETHODCALLTYPE OnStreamEnd() override {}
            void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
            void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
            void STDMETHODCALLTYPE OnBufferStart(void*) override {}
            void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
            void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
        };

        constexpr std::size_t kMaxSfxVoices = 48;
    }

    struct AudioEngine::Impl
    {
        IXAudio2* xaudio{};
        IXAudio2MasteringVoice* master{};
        IXAudio2SubmixVoice* musicSubmix{};
        IXAudio2SubmixVoice* sfxSubmix{};
        VoiceCallback callback;

        struct ActiveSfx { IXAudio2SourceVoice* voice{}; std::shared_ptr<WavData> wav; };
        std::vector<ActiveSfx> activeSfx;

        IXAudio2SourceVoice* musicVoice{};
        std::shared_ptr<WavData> musicWav;

        bool ok() const { return xaudio != nullptr; }

        IXAudio2SourceVoice* CreateVoice(const WAVEFORMATEX& fmt, IXAudio2SubmixVoice* dest)
        {
            const XAUDIO2_SEND_DESCRIPTOR send{ 0, dest };
            const XAUDIO2_VOICE_SENDS sends{ 1, const_cast<XAUDIO2_SEND_DESCRIPTOR*>(&send) };
            IXAudio2SourceVoice* voice = nullptr;
            if (FAILED(xaudio->CreateSourceVoice(&voice, &fmt, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &callback, &sends)))
                return nullptr;
            return voice;
        }
    };

    AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>())
    {
        if (FAILED(XAudio2Create(&m_impl->xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)))
        {
            OutputDebugStringA("AudioEngine: XAudio2Create failed - running silent\n");
            m_impl->xaudio = nullptr;
            return;
        }
        if (FAILED(m_impl->xaudio->CreateMasteringVoice(&m_impl->master)))
        {
            OutputDebugStringA("AudioEngine: CreateMasteringVoice failed - running silent\n");
            m_impl->xaudio->Release();
            m_impl->xaudio = nullptr;
            return;
        }
        m_impl->xaudio->CreateSubmixVoice(&m_impl->musicSubmix, 2, 44100, 0, 0, nullptr, nullptr);
        m_impl->xaudio->CreateSubmixVoice(&m_impl->sfxSubmix, 2, 44100, 0, 0, nullptr, nullptr);
        OutputDebugStringA("AudioEngine: XAudio2 ready\n");
    }

    AudioEngine::~AudioEngine()
    {
        if (!m_impl || !m_impl->ok()) return;
        StopMusic();
        for (auto& s : m_impl->activeSfx) if (s.voice) { s.voice->Stop(); s.voice->DestroyVoice(); }
        m_impl->activeSfx.clear();
        if (m_impl->sfxSubmix) m_impl->sfxSubmix->DestroyVoice();
        if (m_impl->musicSubmix) m_impl->musicSubmix->DestroyVoice();
        if (m_impl->master) m_impl->master->DestroyVoice();
        m_impl->xaudio->Release();
    }

    void AudioEngine::SetMasterVolume(float v) { if (m_impl->master) m_impl->master->SetVolume(Clamp01(v)); }
    void AudioEngine::SetMusicVolume(float v) { if (m_impl->musicSubmix) m_impl->musicSubmix->SetVolume(Clamp01(v)); }
    void AudioEngine::SetSfxVolume(float v)   { if (m_impl->sfxSubmix) m_impl->sfxSubmix->SetVolume(Clamp01(v)); }

    void AudioEngine::PlaySfx(const std::string& wavPath)
    {
        if (!m_impl->ok() || m_impl->activeSfx.size() >= kMaxSfxVoices) return;

        auto wav = std::make_shared<WavData>();
        if (!LoadWav(wavPath, *wav))
        {
            OutputDebugStringA(("AudioEngine: could not load sfx '" + wavPath + "'\n").c_str());
            return;
        }

        IXAudio2SourceVoice* voice = m_impl->CreateVoice(wav->format, m_impl->sfxSubmix);
        if (voice == nullptr) return;

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(wav->samples.size());
        buffer.pAudioData = wav->samples.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.pContext = voice;   // identifies this voice in OnBufferEnd
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start(0)))
        {
            voice->DestroyVoice();
            return;
        }
        m_impl->activeSfx.push_back({ voice, std::move(wav) });
    }

    void AudioEngine::PlayMusic(const std::string& wavPath)
    {
        if (!m_impl->ok()) return;
        StopMusic();
        if (wavPath.empty()) return;

        auto wav = std::make_shared<WavData>();
        if (!LoadWav(wavPath, *wav))
        {
            OutputDebugStringA(("AudioEngine: could not load music '" + wavPath + "'\n").c_str());
            return;
        }
        m_impl->musicVoice = m_impl->CreateVoice(wav->format, m_impl->musicSubmix);
        if (m_impl->musicVoice == nullptr) return;

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(wav->samples.size());
        buffer.pAudioData = wav->samples.data();
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        buffer.pContext = nullptr;   // never "finishes"
        if (FAILED(m_impl->musicVoice->SubmitSourceBuffer(&buffer)) || FAILED(m_impl->musicVoice->Start(0)))
        {
            m_impl->musicVoice->DestroyVoice();
            m_impl->musicVoice = nullptr;
            return;
        }
        m_impl->musicWav = std::move(wav);
    }

    void AudioEngine::StopMusic()
    {
        if (m_impl->musicVoice)
        {
            m_impl->musicVoice->Stop(0);
            m_impl->musicVoice->FlushSourceBuffers();
            m_impl->musicVoice->DestroyVoice();
            m_impl->musicVoice = nullptr;
        }
        m_impl->musicWav.reset();
    }

    void AudioEngine::Update()
    {
        if (!m_impl->ok()) return;

        std::vector<IXAudio2SourceVoice*> done;
        {
            std::scoped_lock lock(m_impl->callback.mutex);
            done.swap(m_impl->callback.finished);
        }
        for (IXAudio2SourceVoice* voice : done)
        {
            const auto it = std::find_if(m_impl->activeSfx.begin(), m_impl->activeSfx.end(),
                [voice](const Impl::ActiveSfx& s) { return s.voice == voice; });
            if (it == m_impl->activeSfx.end()) continue;
            it->voice->DestroyVoice();
            *it = std::move(m_impl->activeSfx.back());
            m_impl->activeSfx.pop_back();
        }
    }
}
