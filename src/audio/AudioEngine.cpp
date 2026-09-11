#include "audio/AudioEngine.h"

#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
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

        // Parsed RIFF/WAVE header: format + where the `data` chunk's bytes live in
        // the file. Shared by the full-load path (PlaySfx) and the streaming path
        // (PlayMusic) so both agree on the same minimal PCM/IEEE-float parser.
        struct WavHeader
        {
            WAVEFORMATEX format{};
            std::uint64_t dataOffset{};
            std::uint64_t dataSize{};
        };

        std::uint32_t Read32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
        std::uint16_t Read16(const std::uint8_t* p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; }

        // Opens `path` (trying the same relative-prefix ladder as every other
        // asset loader here - CLI builds run from a couple of directories below
        // the project root) and walks RIFF chunks far enough to capture `fmt `
        // and where `data` starts/ends. Leaves `file` open and seekable; does
        // NOT read the data bytes themselves. Returns false on anything it does
        // not understand (only `fmt ` + `data`, PCM or IEEE-float tag).
        bool OpenWavHeader(std::ifstream& file, const std::string& path, WavHeader& out)
        {
            for (const std::string& prefix : { std::string{}, std::string{ "../../" }, std::string{ "../../../" } })
            {
                file.open(prefix + path, std::ios::binary);
                if (file.is_open()) break;
            }
            if (!file.is_open()) return false;

            std::uint8_t riff[12];
            file.read(reinterpret_cast<char*>(riff), 12);
            if (file.gcount() != 12 || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
                return false;

            bool haveFmt = false, haveData = false;
            std::uint64_t pos = 12;
            while (!haveFmt || !haveData)
            {
                std::uint8_t chunkHeader[8];
                file.seekg(static_cast<std::streamoff>(pos));
                file.read(reinterpret_cast<char*>(chunkHeader), 8);
                if (file.gcount() != 8) break;
                const std::uint32_t size = Read32(chunkHeader + 4);
                const std::uint64_t body = pos + 8;

                if (std::memcmp(chunkHeader, "fmt ", 4) == 0 && size >= 16)
                {
                    std::uint8_t f[16];
                    file.read(reinterpret_cast<char*>(f), 16);
                    if (file.gcount() != 16) break;
                    out.format.wFormatTag = Read16(f + 0);
                    out.format.nChannels = Read16(f + 2);
                    out.format.nSamplesPerSec = Read32(f + 4);
                    out.format.nAvgBytesPerSec = Read32(f + 8);
                    out.format.nBlockAlign = Read16(f + 12);
                    out.format.wBitsPerSample = Read16(f + 14);
                    out.format.cbSize = 0;
                    haveFmt = true;
                }
                else if (std::memcmp(chunkHeader, "data", 4) == 0)
                {
                    out.dataOffset = body;
                    out.dataSize = size;
                    haveData = true;
                }
                pos = body + size + (size & 1u);   // chunks are word-aligned
            }
            return haveFmt && haveData && out.dataSize > 0;
        }

        // Full load (PlaySfx): short one-shots stay entirely in memory, same as
        // before streaming existed.
        bool LoadWav(const std::string& path, WavData& out)
        {
            std::ifstream file;
            WavHeader header;
            if (!OpenWavHeader(file, path, header)) return false;

            out.format = header.format;
            out.samples.resize(static_cast<std::size_t>(header.dataSize));
            file.seekg(static_cast<std::streamoff>(header.dataOffset));
            file.read(reinterpret_cast<char*>(out.samples.data()), static_cast<std::streamsize>(header.dataSize));
            return static_cast<std::uint64_t>(file.gcount()) == header.dataSize;
        }

        bool SameFormat(const WAVEFORMATEX& a, const WAVEFORMATEX& b)
        {
            return a.wFormatTag == b.wFormatTag && a.nChannels == b.nChannels
                && a.nSamplesPerSec == b.nSamplesPerSec && a.wBitsPerSample == b.wBitsPerSample;
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

        // Background music streaming (docs/audio-design.md §6 "진짜 스트리밍").
        // Owns the file handle and a small ring of chunk buffers; a dedicated
        // std::thread refills them from disk so the main/render/job threads never
        // block on file IO for music. This thread only ever touches its own
        // `file`/`chunks`/`cursor` and the XAudio2 source voice it was handed -
        // IXAudio2SourceVoice methods are documented thread-safe, and the voice
        // is not touched by anything else while this thread is alive (StopMusic
        // joins it before DestroyVoice).
        struct MusicStream
        {
            static constexpr int kBufferCount = 3;   // ring depth; ~1s of slack at the chunk size below

            std::ifstream file;
            WavHeader header;
            std::uint64_t cursor{};                   // read position within the data chunk; wraps to loop
            std::vector<std::uint8_t> chunks[kBufferCount];
            std::size_t chunkBytes{};
            std::thread thread;
            std::atomic<bool> stop{ false };

            // Fills `dst` with up to chunkBytes, wrapping back to the start of the
            // data chunk (this is how the track loops - no XAUDIO2_LOOP_INFINITE,
            // we are our own loop). Returns 0 only on a real read failure.
            std::size_t ReadNextChunk(std::vector<std::uint8_t>& dst)
            {
                dst.resize(chunkBytes);
                std::size_t filled = 0;
                while (filled < chunkBytes)
                {
                    if (cursor >= header.dataSize) cursor = 0;
                    file.seekg(static_cast<std::streamoff>(header.dataOffset + cursor));
                    const std::size_t remain = static_cast<std::size_t>(header.dataSize - cursor);
                    const std::size_t take = std::min(chunkBytes - filled, remain);
                    file.read(reinterpret_cast<char*>(dst.data() + filled), static_cast<std::streamsize>(take));
                    const std::size_t got = static_cast<std::size_t>(file.gcount());
                    if (got == 0) break;   // read error - stop instead of spinning
                    cursor += got;
                    filled += got;
                }
                dst.resize(filled);
                return filled;
            }
        };

        // Runs on `stream->thread`. Primes every ring slot, then polls the
        // voice's queue depth and refills the oldest slot whenever XAudio2 has
        // consumed one - slots are refilled in the same round-robin order they
        // were submitted, so a slot is only ever rewritten after XAudio2 has
        // moved past it (FIFO per voice).
        void StreamMusicThread(IXAudio2SourceVoice* voice, MusicStream* stream)
        {
            int nextSlot = 0;
            for (int i = 0; i < MusicStream::kBufferCount; ++i)
            {
                if (stream->ReadNextChunk(stream->chunks[i]) == 0) return;
                XAUDIO2_BUFFER buffer{};
                buffer.AudioBytes = static_cast<UINT32>(stream->chunks[i].size());
                buffer.pAudioData = stream->chunks[i].data();
                voice->SubmitSourceBuffer(&buffer);
            }

            while (!stream->stop.load(std::memory_order_relaxed))
            {
                XAUDIO2_VOICE_STATE state{};
                voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                if (state.BuffersQueued < static_cast<UINT32>(MusicStream::kBufferCount))
                {
                    if (stream->ReadNextChunk(stream->chunks[nextSlot]) == 0) break;
                    XAUDIO2_BUFFER buffer{};
                    buffer.AudioBytes = static_cast<UINT32>(stream->chunks[nextSlot].size());
                    buffer.pAudioData = stream->chunks[nextSlot].data();
                    voice->SubmitSourceBuffer(&buffer);
                    nextSlot = (nextSlot + 1) % MusicStream::kBufferCount;
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
            }
        }
    }

    struct AudioEngine::Impl
    {
        IXAudio2* xaudio{};
        IXAudio2MasteringVoice* master{};
        IXAudio2SubmixVoice* musicSubmix{};
        IXAudio2SubmixVoice* sfxSubmix{};
        VoiceCallback callback;

        // One-shot voice pool (docs/audio-design.md §6 "voice 풀링"): voices are
        // created lazily up to kMaxSfxVoices and then reused by matching format
        // instead of Create/DestroyVoice per call - a mass-defense scene fires
        // many overlapping, same-format SFX per second.
        struct SfxVoice { IXAudio2SourceVoice* voice{}; WAVEFORMATEX fmt{}; bool busy{ false }; std::shared_ptr<WavData> wav; };
        std::vector<SfxVoice> sfxVoices;

        IXAudio2SourceVoice* musicVoice{};
        std::unique_ptr<MusicStream> musicStream;

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
        m_impl->sfxVoices.reserve(kMaxSfxVoices);
        OutputDebugStringA("AudioEngine: XAudio2 ready\n");
    }

    AudioEngine::~AudioEngine()
    {
        if (!m_impl || !m_impl->ok()) return;
        StopMusic();
        for (auto& s : m_impl->sfxVoices) if (s.voice) s.voice->DestroyVoice();
        m_impl->sfxVoices.clear();
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
        if (!m_impl->ok()) return;

        auto wav = std::make_shared<WavData>();
        if (!LoadWav(wavPath, *wav))
        {
            OutputDebugStringA(("AudioEngine: could not load sfx '" + wavPath + "'\n").c_str());
            return;
        }

        Impl::SfxVoice* slot = nullptr;
        for (auto& s : m_impl->sfxVoices)
            if (!s.busy && SameFormat(s.fmt, wav->format)) { slot = &s; break; }

        if (slot == nullptr && m_impl->sfxVoices.size() < kMaxSfxVoices)
        {
            IXAudio2SourceVoice* voice = m_impl->CreateVoice(wav->format, m_impl->sfxSubmix);
            if (voice == nullptr) return;
            m_impl->sfxVoices.push_back({ voice, wav->format, false, nullptr });
            slot = &m_impl->sfxVoices.back();
        }
        else if (slot == nullptr)
        {
            // Pool is at capacity: steal the first idle voice (any format) and
            // recreate it for this format. If every voice is busy, drop the
            // sound - same behaviour as the old hard cap.
            for (auto& s : m_impl->sfxVoices)
                if (!s.busy) { slot = &s; break; }
            if (slot == nullptr) return;
            slot->voice->DestroyVoice();
            slot->voice = m_impl->CreateVoice(wav->format, m_impl->sfxSubmix);
            if (slot->voice == nullptr)
            {
                m_impl->sfxVoices.erase(m_impl->sfxVoices.begin()
                    + (slot - m_impl->sfxVoices.data()));
                return;
            }
            slot->fmt = wav->format;
        }

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(wav->samples.size());
        buffer.pAudioData = wav->samples.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.pContext = slot->voice;   // identifies this voice in OnBufferEnd
        if (FAILED(slot->voice->SubmitSourceBuffer(&buffer)) || FAILED(slot->voice->Start(0))) return;
        slot->busy = true;
        slot->wav = std::move(wav);
    }

    void AudioEngine::PlayMusic(const std::string& wavPath)
    {
        if (!m_impl->ok()) return;
        StopMusic();
        if (wavPath.empty()) return;

        auto stream = std::make_unique<MusicStream>();
        if (!OpenWavHeader(stream->file, wavPath, stream->header))
        {
            OutputDebugStringA(("AudioEngine: could not load music '" + wavPath + "'\n").c_str());
            return;
        }

        // ~350ms per chunk, rounded down to a whole sample frame so a chunk
        // boundary never splits one.
        const auto& fmt = stream->header.format;
        std::size_t chunkBytes = static_cast<std::size_t>(fmt.nAvgBytesPerSec) * 350 / 1000;
        const std::size_t align = std::max<std::size_t>(fmt.nBlockAlign, 1);
        chunkBytes -= chunkBytes % align;
        stream->chunkBytes = chunkBytes > 0 ? chunkBytes : align;

        m_impl->musicVoice = m_impl->CreateVoice(fmt, m_impl->musicSubmix);
        if (m_impl->musicVoice == nullptr) return;

        IXAudio2SourceVoice* voice = m_impl->musicVoice;
        MusicStream* raw = stream.get();
        stream->thread = std::thread([voice, raw] { StreamMusicThread(voice, raw); });

        if (FAILED(voice->Start(0)))
        {
            raw->stop.store(true, std::memory_order_relaxed);
            stream->thread.join();
            voice->DestroyVoice();
            m_impl->musicVoice = nullptr;
            return;
        }
        m_impl->musicStream = std::move(stream);
    }

    void AudioEngine::StopMusic()
    {
        // Join the streaming thread first - it calls methods on musicVoice, so
        // it must be gone before DestroyVoice runs.
        if (m_impl->musicStream)
        {
            m_impl->musicStream->stop.store(true, std::memory_order_relaxed);
            if (m_impl->musicStream->thread.joinable()) m_impl->musicStream->thread.join();
        }
        if (m_impl->musicVoice)
        {
            m_impl->musicVoice->Stop(0);
            m_impl->musicVoice->FlushSourceBuffers();
            m_impl->musicVoice->DestroyVoice();
            m_impl->musicVoice = nullptr;
        }
        m_impl->musicStream.reset();
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
            const auto it = std::find_if(m_impl->sfxVoices.begin(), m_impl->sfxVoices.end(),
                [voice](const Impl::SfxVoice& s) { return s.voice == voice; });
            if (it == m_impl->sfxVoices.end()) continue;
            // Return the voice to the pool instead of destroying it.
            it->voice->Stop(0);
            it->voice->FlushSourceBuffers();
            it->busy = false;
            it->wav.reset();   // sample bytes only need to live while queued/playing
        }
    }
}
