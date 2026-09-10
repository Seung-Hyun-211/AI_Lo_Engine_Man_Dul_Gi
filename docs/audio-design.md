# 오디오 설계 (Audio)

**상태: 최소 구현.** XAudio2 마스터 + 2 서브믹스(music, sfx), PCM WAV 원샷, 루핑 음악 1개.
스트리밍(청크 읽기)·3D 위치 오디오·이펙트(리버브 등)·오디오 에셋 로더 통합은 아직.

관련: `docs/game-settings.md`(볼륨 설정), `docs/roadmap.md`(rec 3 / D6), `docs/loading-and-streaming.md`(장차 `AssetKind::Audio`).

---

## 1. 구조

```
IXAudio2 (자체 믹서 스레드)
 └ MasteringVoice            SetVolume = 마스터 볼륨
    ├ SubmixVoice "music"    SetVolume = 음악 볼륨.  루핑 소스 1개가 여기로
    └ SubmixVoice "sfx"      SetVolume = 효과음 볼륨.  원샷 소스들이 여기로
```

- `src/audio/AudioEngine.{h,cpp}` — `engine::audio::AudioEngine`. **pImpl** 로 `<xaudio2.h>` 를 헤더 밖에 둔다(`Application.h` 가 include 해도 Windows 오디오 헤더가 안 샌다).
- **메인 스레드 API.** XAudio2 가 믹싱을 자기 스레드에서 하므로 렌더/잡 스레드는 안 건드린다.
- **XAudio2 없으면 조용히 무동작.** `XAudio2Create`/`CreateMasteringVoice` 실패 시 생성자는 성공하고 모든 호출이 no-op — 게임은 그대로 돈다(헤드리스/WARP 환경 대비).
- 링크: `xaudio2.lib` (Windows 10+ 인박스 XAudio2.9, 재배포·COM 초기화 불필요).

## 2. API

```cpp
class AudioEngine {
    AudioEngine();  ~AudioEngine();
    void SetMasterVolume(float v01);   // 0..1 clamp
    void SetMusicVolume(float v01);
    void SetSfxVolume(float v01);
    void PlaySfx(const std::string& wavPath);     // fire-and-forget 원샷
    void PlayMusic(const std::string& wavPath);   // 루핑, 현재 곡 교체 (빈 경로 = 정지)
    void StopMusic();
    void Update();                                // 프레임당 1회 — 끝난 원샷 voice 회수
};
```

- **`PlaySfx`**: WAV 로드 → 소스 voice 생성(→ sfx 서브믹스) → 버퍼 제출 + Start. `IXAudio2VoiceCallback::OnBufferEnd` 가 voice 를 뮤텍스 큐에 넣고, `Update()` 가 그걸 읽어 `DestroyVoice`. WAV 바이트는 `shared_ptr<WavData>` 로 voice 수명 동안 살아있음(버퍼가 그 메모리를 가리킴). 동시 원샷 상한 `kMaxSfxVoices = 48`.
- **`PlayMusic`**: 전용 소스 voice + `LoopCount = XAUDIO2_LOOP_INFINITE`. v1 은 전곡을 메모리에 올린다(진짜 청크 스트리밍은 후속).
- **WAV 리더**: `LoadWav` — RIFF/WAVE 의 `fmt ` + `data` 청크만. PCM(및 IEEE float 태그). 다른 청크 스킵. 이해 못 하는 파일은 false → 로그 + 무시.

## 3. 설정 연결

`core::Settings::{masterVolume, musicVolume, sfxVolume}` (0..1) → `Application::ApplyVolumes()` 가 `m_audio.Set*Volume` 로 밀어넣는다. 호출 시점:
- 시작 시 1회(`Application` 생성자).
- `SettingsScreen` 의 볼륨 슬라이더 `onChanged` → `SettingsScreenActions::onVolumeChanged` → `ApplyVolumes()` (실시간).

`docs/game-settings.md` §1 의 "저장만 됨" 이 볼륨 3개에 대해서는 해소됨(마우스 감도/반전은 여전히 대상 없음).

## 4. 데모 훅

플레이어 점프(Space) 시 `Application` 이 `m_audio.PlaySfx("assets/audio/blip.wav")` — `blip.wav` 는 생성된 880 Hz 짧은 사인(44100 Hz, 16-bit mono). 실제 게임 이벤트 배선이 생기면 교체.

## 5. 사용 방법 (How to use)

### 효과음 재생

게임플레이 코드(`Simulation` 등)는 오디오를 직접 못 부른다(레이어 분리) — **`Application` 이 조율**한다. 이벤트를 `Application` 이 감지하거나 `Simulation` 이 값으로 알려주고(`PlayerIntent` 처럼), `Application::Run` 에서 `m_audio.PlaySfx(path)`.

### 배경음악

`Application` 의 씬 전환 함수(`EnterInGame` 등)에서 `m_audio.PlayMusic("assets/audio/<track>.wav")`, 타이틀 복귀 시 `StopMusic()` 또는 다른 곡.

### 새 볼륨 버스

`AudioEngine::Impl` 에 서브믹스 추가 + `Set<Name>Volume` + `Settings` 필드 + `SettingsScreen` 슬라이더 행 + `ApplyVolumes()` 한 줄.

### 하지 말 것

- 렌더/잡 스레드에서 `AudioEngine` 호출 금지 — 메인 스레드만.
- `Simulation`/`ui`/`render` 에서 `audio/` 를 include 하지 않는다 — `Application` 만.
- WAV 바이트를 voice 보다 먼저 해제하지 않는다(`PlaySfx` 의 `shared_ptr` 패턴 유지).
- `Update()` 를 프레임당 여러 번 부르거나 빠뜨리지 않는다(원샷 voice 누수).
- 압축 오디오(OGG/MP3) 를 지금 넣지 않는다 — v1 은 PCM WAV. 필요하면 vendor + `LoadWav` 옆에 디코더.

## 6. 지을 것

- 진짜 스트리밍(음악을 청크로 읽어 이중 버퍼) — 긴 트랙 메모리 절감.
- `loading-and-streaming` 의 `AssetKind::Audio` 로 로드 경로 통합(현재는 `PlaySfx` 가 즉석 로드).
- OGG Vorbis(stb_vorbis vendor) — 배포 용량.
- 3D 위치 오디오(`X3DAudio`) — 디펜스 게임에서 적/타워 위치음.
- voice 풀링(원샷마다 Create/Destroy 대신 재사용).
- 오디오 에셋 매니페스트 + `game-settings.md` 에 출력 장치 선택.
