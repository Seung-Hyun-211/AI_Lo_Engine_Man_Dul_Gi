# 오디오 설계 (Audio)

**상태: 최소 구현 + 스트리밍/풀링.** XAudio2 마스터 + 2 서브믹스(music, sfx), 포맷 매칭
**풀링된** PCM WAV 원샷, **청크 스트리밍**되는 루핑 음악 1개(전용 스레드).
3D 위치 오디오·이펙트(리버브 등)·오디오 에셋 로더 통합은 아직.

관련: `docs/game-settings.md`(볼륨 설정), `docs/roadmap.md`(rec 3 / D6), `docs/loading-and-streaming.md`(장차 `AssetKind::Audio`).

---

## 1. 구조

```
IXAudio2 (자체 믹서 스레드)
 └ MasteringVoice            SetVolume = 마스터 볼륨
    ├ SubmixVoice "music"    SetVolume = 음악 볼륨.  스트리밍 소스 1개가 여기로
    └ SubmixVoice "sfx"      SetVolume = 효과음 볼륨.  풀링된 원샷 voice 들이 여기로

MusicStream 전용 스레드 (PlayMusic 이 std::thread 로 기동, StopMusic 이 join)
 └ 파일을 청크(~350ms)로 읽어 링 버퍼(3슬롯) 채움 → SubmitSourceBuffer
   data 청크 끝에 닿으면 커서를 처음으로 되감아 자체 루핑 (XAUDIO2_LOOP_INFINITE 안 씀)
```

- `src/audio/AudioEngine.{h,cpp}` — `engine::audio::AudioEngine`. **pImpl** 로 `<xaudio2.h>` 를 헤더 밖에 둔다(`Application.h` 가 include 해도 Windows 오디오 헤더가 안 샌다).
- **메인 스레드 API.** XAudio2 가 믹싱을 자기 스레드에서 하므로 렌더/잡 스레드는 안 건드린다. `PlayMusic` 이 여는 스트리밍 스레드도 자기 자신(파일 핸들 + 청크 버퍼 + 넘겨받은 voice)만 건드려 같은 원칙을 지킨다 — voice 는 스레드 살아있는 동안 다른 곳에서 안 건드림(`StopMusic` 이 join 을 `DestroyVoice` 보다 먼저).
- **XAudio2 없으면 조용히 무동작.** `XAudio2Create`/`CreateMasteringVoice` 실패 시 생성자는 성공하고 모든 호출이 no-op — 게임은 그대로 돈다(헤드리스/WARP 환경 대비).
- 링크: `xaudio2.lib` (Windows 10+ 인박스 XAudio2.9, 재배포·COM 초기화 불필요).

## 2. API

```cpp
class AudioEngine {
    AudioEngine();  ~AudioEngine();
    void SetMasterVolume(float v01);   // 0..1 clamp
    void SetMusicVolume(float v01);
    void SetSfxVolume(float v01);
    void PlaySfx(const std::string& wavPath);     // 풀링된 원샷 (포맷 일치 voice 재사용)
    void PlayMusic(const std::string& wavPath);   // 청크 스트리밍 루프, 현재 곡 교체 (빈 경로 = 정지)
    void StopMusic();
    void Update();                                // 프레임당 1회 — 끝난 원샷 voice 를 풀로 반환
};
```

- **`PlaySfx`**: WAV 로드 → **포맷이 맞는 유휴 voice 를 풀에서 재사용**, 없으면 새로 생성(상한
  `kMaxSfxVoices = 48`), 풀이 꽉 찼는데 맞는 게 없으면 유휴 voice 아무거나 그 포맷으로 재생성.
  버퍼 제출 + Start. `IXAudio2VoiceCallback::OnBufferEnd` 가 voice 를 뮤텍스 큐에 넣고, `Update()`
  가 그걸 읽어 `Stop`+`FlushSourceBuffers` 후 **풀에 반환**(destroy 안 함 — voice 생성·파괴 비용을
  없앤 게 §6 "voice 풀링"). WAV 바이트는 `shared_ptr<WavData>` 로 재생 중에만 살아있음(끝나면
  `Update()` 가 reset).
- **`PlayMusic`**: 파일을 끝까지 읽지 않는다 — `OpenWavHeader` 로 `fmt `/`data` 위치만 파싱하고
  전용 `std::thread` 가 ~350ms 청크를 3슬롯 링버퍼로 읽어 순서대로 `SubmitSourceBuffer`. 큐 깊이가
  `GetState().BuffersQueued` 로 링 크기 아래로 떨어지면(=XAudio2 가 가장 오래된 슬롯을 다 재생함)
  다음 청크로 그 슬롯을 채운다. `data` 끝에 닿으면 커서를 0 으로 되감아 자체 루핑. `StopMusic` 이
  스레드에 `stop` atomic 을 세우고 join 한 뒤 voice 를 정리한다.
- **WAV 리더**: `OpenWavHeader`(헤더만) / `LoadWav`(헤더 + 전체 데이터, `PlaySfx` 용) — RIFF/WAVE 의
  `fmt ` + `data` 청크만. PCM(및 IEEE float 태그). 다른 청크 스킵. 이해 못 하는 파일은 false → 로그
  + 무시.

## 3. 설정 연결

`core::Settings::{masterVolume, musicVolume, sfxVolume}` (0..1) → `Application::ApplyVolumes()` 가 `m_audio.Set*Volume` 로 밀어넣는다. 호출 시점:
- 시작 시 1회(`Application` 생성자).
- `SettingsScreen` 의 볼륨 슬라이더 `onChanged` → `SettingsScreenActions::onVolumeChanged` → `ApplyVolumes()` (실시간).

`docs/game-settings.md` §1 의 "저장만 됨" 이 볼륨 3개에 대해서는 해소됨(마우스 감도/반전은 여전히 대상 없음).

## 4. 데모 훅

- 플레이어 점프(Space) 시 `Application` 이 `m_audio.PlaySfx("assets/audio/blip.wav")` — `blip.wav` 는
  생성된 880 Hz 짧은 사인(44100 Hz, 16-bit mono). 실제 게임 이벤트 배선이 생기면 교체.
- `EnterInGame()` 이 `m_audio.PlayMusic("assets/audio/blip.wav")`(플레이스홀더 — 같은 파일이 짧아서
  스트리밍의 루프-되감기 경로를 몇 초 안에 여러 번 실행해 보는 스트레스 테스트도 겸한다),
  `EnterTitle()` 이 `StopMusic()`. 실제 트랙이 생기면 경로만 교체.

## 5. 사용 방법 (How to use)

### 효과음 재생

게임플레이 코드(`Simulation` 등)는 오디오를 직접 못 부른다(레이어 분리) — **`Application` 이 조율**한다. 이벤트를 `Application` 이 감지하거나 `Simulation` 이 값으로 알려주고(`PlayerIntent` 처럼), `Application::Run` 에서 `m_audio.PlaySfx(path)`.

### 배경음악

`Application` 의 씬 전환 함수(`EnterInGame` 등)에서 `m_audio.PlayMusic("assets/audio/<track>.wav")`, 타이틀 복귀 시 `StopMusic()` 또는 다른 곡.

### 새 볼륨 버스

`AudioEngine::Impl` 에 서브믹스 추가 + `Set<Name>Volume` + `Settings` 필드 + `SettingsScreen` 슬라이더 행 + `ApplyVolumes()` 한 줄.

### 하지 말 것

- 렌더/잡 스레드에서 `AudioEngine` 호출 금지 — 메인 스레드만(스트리밍 스레드는 내부 구현 디테일 —
  `Application`/게임 코드가 그 존재를 몰라야 한다. `PlayMusic`/`StopMusic` 만 호출).
- `Simulation`/`ui`/`render` 에서 `audio/` 를 include 하지 않는다 — `Application` 만.
- WAV 바이트를 voice 보다 먼저 해제하지 않는다(`PlaySfx` 의 `shared_ptr` 패턴 유지).
- `Update()` 를 프레임당 여러 번 부르거나 빠뜨리지 않는다(원샷 voice 가 풀로 안 돌아옴).
- `MusicStream` 을 손으로 만들거나 그 스레드를 join 하지 않고 voice/`ifstream` 을 파괴하지 않는다 —
  `StopMusic()`(소멸자도 이걸 부름) 만이 정지 순서(스레드 stop+join → voice 정리)를 보장한다.
- 압축 오디오(OGG/MP3) 를 지금 넣지 않는다 — v1 은 PCM WAV. 필요하면 vendor + `OpenWavHeader`/`LoadWav`
  옆에 디코더(청크 단위로 디코드해야 스트리밍과 맞물림).

## 6. 지을 것

- ~~진짜 스트리밍(음악을 청크로 읽어 이중 버퍼)~~ — **됨.** `MusicStream` 전용 스레드, 3슬롯 링버퍼,
  `data` 청크만 파일에 남아있고 전곡을 메모리에 안 올림. §1/§2 참고.
- ~~voice 풀링(원샷마다 Create/Destroy 대신 재사용)~~ — **됨.** `AudioEngine::Impl::sfxVoices` 가 포맷별
  유휴 voice 를 재사용, 상한까지 찬 뒤엔 유휴 voice 재포맷. §2 참고.
- `loading-and-streaming` 의 `AssetKind::Audio` 로 로드 경로 통합(현재는 `PlaySfx`/`PlayMusic` 이
  즉석 로드).
- OGG Vorbis(stb_vorbis vendor) — 배포 용량. 스트리밍 경로(`MusicStream::ReadNextChunk`)와 맞물리게
  청크 단위 디코드로.
- 3D 위치 오디오(`X3DAudio`) — 디펜스 게임에서 적/타워 위치음. `PlaySfxAt(path, worldPos)` +
  `SetListener(pos, forward, up)` 형태, `math::Vec3` 를 쓰므로 `ENGINE_WITH_3D` 로 감싼 별도
  API(규칙 7 — 2D/3D 모듈 분리). `docs/horde-design.md` 규모(수백~수천 소스)라면 동시 재생 상한 +
  거리 기반 컬링이 voice 풀보다 먼저 필요.
- 오디오 에셋 매니페스트 + `game-settings.md` 에 출력 장치 선택.
