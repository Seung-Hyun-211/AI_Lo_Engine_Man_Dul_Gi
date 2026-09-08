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

## 확장 로드맵 (미구현)

| 항목 | 스케치 |
|---|---|
| `Alpha()` 보간 | `FixedTimestep`가 `accumulator / step` (0..1) 노출. `SnapshotBuilder`가 이전/현재 상태를 lerp해 고프레임에서 부드럽게. |
| `TimeScale` / pause | `FixedTimestep::Advance(dt * scale)`. `scale = 0`이면 일시정지. slow-mo·bullet-time·디버그 스텝. |
| 시스템별 클럭 | UI 애니메이션은 pause에 영향 안 받아야 함 → unscaled 클럭 별도. `struct TimeContext { float scaled, unscaled; }`. |
| Replay / 결정성 검증 | 고정 dt + 시드 고정 입력 로그 → 재생. 프레임 해시 비교로 desync 탐지. |
| 프로파일 타이머 | `ScopedTimer`가 구간 시간 수집 → frame-time HUD (로드맵 6). |

## 사용 방법 (How to use)

- **새 시뮬/물리 시스템을 만들 때**: `void Step(float fixedDelta, ...)` 시그니처로 만들고 `Application::Run`의 `for (steps)` 루프에서 호출한다. 내부에서 시간을 재지 않는다.
- **"N초마다" 하는 로직**: 시스템 안에 `float m_accum{}; m_accum += fixedDelta; if (m_accum >= period) { ...; m_accum -= period; }`.
- **경과 시간이 필요하면**: 시스템이 `m_elapsed += fixedDelta`를 누적하고 getter로 노출한다 (예: `Simulation::ElapsedTime()`가 3D 데모 카메라·큐브 회전에 쓰임).
- **스텝 레이트 변경**: `Application` 멤버 `core::FixedTimestep m_timestep{ 1.0f/120.0f };` 처럼 생성자 인자로. 기본은 60Hz.
- **하지 말 것**: 워커 잡·렌더 패스 안에서 `FrameClock` 접근, `Step()` 밖에서 `chrono` 호출, 프레임마다 스텝 크기 바꾸기.
