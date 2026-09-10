# Time 설계 (Time System Design)

`src/core/Time.h`. 엔진의 모든 시간 진행은 이 두 타입을 거친다. 게임 코드는 `std::chrono`를 직접 쓰지 않는다.

## 목표

- 프레임률과 무관하게 시뮬레이션이 **결정적**이다 (같은 입력 → 같은 결과).
- 브레이크포인트·스톨·탭 전환 후에도 시뮬레이션이 **점프하지 않는다**.
- 시간 진행 지점이 한 곳뿐이라 나중에 pause·slow-motion·replay를 한 군데에서 건다.

## 구성 (현재 구현)

### `FrameClock` — 벽시계 델타 소스

```cpp
core::FrameClock clock;
const float dt = clock.Tick();   // 이전 Tick 이후 경과 초, kMaxDelta(0.1s)로 clamp
```

- 메인 루프에서 프레임당 정확히 한 번 `Tick()`.
- `steady_clock` 기반 (시스템 시계 변경 영향 없음).
- **clamp가 핵심**: 디버거로 멈췄다 재개해도 `dt`는 최대 0.1s. 아래 `FixedTimestep` 누적기가 폭주하지 않는다.

### `FixedTimestep` — 고정 스텝 누적기

```cpp
core::FixedTimestep timestep{ 1.0f / 60.0f };   // 기본 60Hz
const int steps = timestep.Advance(dt);          // 이번 프레임 실행할 고정 스텝 수
for (int i = 0; i < steps; ++i)
    simulation.Step(timestep.Step(), intent);
```

- 가변 `dt`를 누적해 고정 크기 스텝으로 쪼갠다. 시뮬레이션·물리·충돌은 **항상 `Step()` 크기(1/60s)** 로만 전진한다.
- 프레임당 최대 `kMaxStepsPerFrame`(5) 스텝. 그 이상 밀리면 누적분을 버린다 ("spiral of death" 방지 — 느린 프레임을 따라잡으려다 더 느려지는 것).
- 60Hz VSync면 프레임당 보통 1스텝. 144Hz면 2~3프레임마다 1스텝 (플레이어가 이산적으로 움직여 살짝 stutter — `Alpha()` 보간은 로드맵).

## 프레임에서의 위치

```text
Application::Run 루프:
  InputState::BeginFrame
  Win32Window::PumpMessages
  dt = FrameClock::Tick()                     ← 시간 측정은 여기 한 번
  intent = BuildPlayerIntent()
  steps = FixedTimestep::Advance(dt)          ← 가변→고정 변환
  repeat steps: Simulation::Step(fixedDt)     ← 시뮬은 고정 dt만 본다
  SnapshotBuilder::Build → IRenderer::Submit
```

렌더 스레드는 자기 시간(FPS 캡)을 `FrameSettings`로 따로 관리한다. 시뮬 시간과 무관하다.

## 불변 규칙

1. `FrameClock::Tick()`은 프레임당 한 번, 메인 스레드에서만.
2. 시뮬레이션·물리·충돌 코드는 전달받은 **고정 `dt`만** 쓴다. `Tick()`·`chrono`·`GetTickCount` 직접 호출 금지.
3. `FixedTimestep`의 스텝 크기는 런타임 중 바꾸지 않는다 (결정성 깨짐). 바꾸려면 누적기를 리셋한다.
4. 애니메이션·파티클처럼 "보이기만 하고 되감기 불필요"한 것도 일관성을 위해 고정 스텝에서 돈다.

## 시간 배율 (Time Scale) — 구현

두 겹이다. 둘 다 스텝 **크기**(`1/60`s)는 안 건드린다 — 결정성 유지(불변 규칙 3). 스텝 **수**만 달라진다.

### 전역 배율 — `Application` 소유

```cpp
const int steps = m_timestep.Advance(delta * m_globalTimeScale);   // Application::Run
```

- `Application::SetGlobalTimeScale(float)` / `GlobalTimeScale()`. `1` = 정상, `0` = 일시정지, `0.5` = 슬로모, `2` = 배속.
- `scale == 0` → `Advance` 가 0 → `Step` 이 루프에서 안 불림(비용 0). 이때도 `ignoreGlobalPause` 액터를 위해 `Application` 이 프레임당 1회 `m_simulation.Step(m_timestep.Step(), intent, /*globalPaused=*/true)` 를 부른다.
- Settings 오버레이 pause(스텝 블록 게이팅)와 **별개**로 겹친다.
- 데모: `Application::OnKey` 의 PageUp/PageDown 이 `{0, 0.25, 0.5, 1, 2}` 순환. **디버그 배선** — 실게임은 게임플레이가 `SetGlobalTimeScale` 을 부른다(불릿타임 발동, 일시정지 메뉴 등).

### 개체별 로컬 배율 — `Simulation::Actor`

```cpp
struct Actor { ...; float timeScale{ 1.0f }; bool ignoreGlobalPause{ false }; ...; };
```

`Simulation::StepActors` 가 액터마다:

```cpp
const float scaled = fixedDelta * actor.timeScale;
const int sub = actor.timeScale <= 1.0f ? 1
              : min((int)ceil(actor.timeScale), kMaxActorSubSteps);   // kMaxActorSubSteps = 8
const float subDt = scaled / sub;
for (i in [0, sub)) StepOneActor(actor, subDt, ...);
```

- `timeScale > 1` (가속)은 **서브스텝** 으로 나눠 적분 — `fixedDelta*배율` 한 번이면 이동/중력이 크게 튀어 터널링·발산. `subDt` 는 항상 `fixedDelta` 이하.
- `timeScale <= 1` (슬로모)은 서브스텝 1개, `subDt = fixedDelta * timeScale`.
- 유효 배율 = `globalTimeScale × actor.timeScale` (전역이 이미 `Advance` 에서 스텝 수로 반영되므로 `StepActors` 는 `actor.timeScale` 만 곱함). 전역 `0` + `ignoreGlobalPause` 액터면 `globalPaused` 경로로 `actor.timeScale` 만 적용돼 계속 움직임(타임스톱 시전자 패턴).
- 데모: `Simulation::SpawnActors` 가 액터 3개 — `[0]` 플레이어(1×), `[1]` 3× + `ignoreGlobalPause`, `[2]` 0.35×. `SnapshotBuilder` 가 `[1..]` 를 큐브로 그림(색: 따뜻=빠름, 차가움=느림).

### 아직 안 한 것

- **unscaled 클럭 분리**: UI/로딩 화면 애니메이션이 pause·배속에 안 물리게 `struct TimeContext { float scaled, unscaled; }` 로 나누는 것. 현재 `UpdateCameraLook` 만 사실상 unscaled(프레임당 1회, 스텝 밖). `m_elapsed`(조명 스윕)는 의도적으로 scaled.
- 액터 간 상호작용(빠른 액터 ↔ 보통 액터 충돌 순서), 로컬 배율의 부모/자식 합성.

## 확장 로드맵 (미구현)

| 항목 | 스케치 |
|---|---|
| `Alpha()` 보간 | `FixedTimestep`가 `accumulator / step` (0..1) 노출. `SnapshotBuilder`가 이전/현재 상태를 lerp해 고프레임에서 부드럽게. |
| 시스템별 클럭 | 위 "unscaled 클럭 분리". `struct TimeContext { float scaled, unscaled; }`. |
| Replay / 결정성 검증 | 고정 dt + 시드 고정 입력 로그 → 재생. 프레임 해시 비교로 desync 탐지. 로컬 배율·서브스텝 수가 게임플레이로 결정되면(벽시계 X) 결정성 유지. |
| 프로파일 타이머 | `ScopedTimer`가 구간 시간 수집 → frame-time HUD (로드맵 6). |

## 사용 방법 (How to use)

- **새 시뮬/물리 시스템을 만들 때**: `void Step(float fixedDelta, ...)` 시그니처로 만들고 `Application::Run`의 `for (steps)` 루프에서 호출한다. 내부에서 시간을 재지 않는다.
- **"N초마다" 하는 로직**: 시스템 안에 `float m_accum{}; m_accum += fixedDelta; if (m_accum >= period) { ...; m_accum -= period; }`.
- **경과 시간이 필요하면**: 시스템이 `m_elapsed += fixedDelta`를 누적하고 getter로 노출한다 (예: `Simulation::ElapsedTime()`가 3D 데모 카메라·큐브 회전에 쓰임).
- **스텝 레이트 변경**: `Application` 멤버 `core::FixedTimestep m_timestep{ 1.0f/120.0f };` 처럼 생성자 인자로. 기본은 60Hz.
- **전역 배속/슬로모/일시정지 걸기**: `application.SetGlobalTimeScale(0.3f)` 같은 식. `0` = 일시정지. 프레임마다 바꿔도 안전(누적기가 알아서 흡수). 스텝 크기는 안 변하므로 불변 규칙 3 위반 아님.
- **특정 개체만 가속/감속**: 그 개체를 `Simulation::Actor` 로 만들고 `timeScale` 설정(`>1` 은 자동 서브스텝). 전역 일시정지 중에도 움직여야 하면 `ignoreGlobalPause = true`. `SpawnActors` 가 조립 예시.
- **하지 말 것**: 워커 잡·렌더 패스 안에서 `FrameClock`/타임스케일 접근, `Step()` 밖에서 `chrono` 호출, 프레임마다 스텝 크기 바꾸기, `timeScale > 1` 을 서브스텝 없이 `dt` 한 번에 곱하기(터널링), `kMaxActorSubSteps` 캡 무시(느린 프레임에서 서브스텝 폭주).
