# C++ DX11 2D 게임 엔진: 멀티스레드 아키텍처

> 이 문서는 현재 `Cpp_Window_App`의 구현 계약과 이후 확장 방향을 정의한다.

## 목표와 전제

- Win32/DX11 2D 엔진에서 대량의 동종 데이터를 연속 메모리로 관리한다.
- 렌더러는 가변 게임 월드를 직접 읽지 않고, 완료된 값 기반 `RenderSnapshot`만 소비한다.
- 워커는 생성 후 재사용하는 JobSystem으로 관리한다.
- 프레임 목표는 기본 60 FPS이며, VSync 또는 소프트웨어 FPS 제한을 선택한다.

## 스레드 역할

```text
Main Thread: Win32 메시지 → Input → Simulation → Job Fence → RenderSnapshot
                                                               ↓ Submit
Render Thread:                            latest-frame mailbox → DX11 → Present
Job Workers:                              연속 데이터 청크의 독립 update
향후 IO Thread:                           파일 read/decode → 안전한 완료 큐
향후 Audio Thread:                        실시간 오디오 콜백/명령 소비
```

- **Main**은 입력, 게임 상태 전이, Job 제출과 완료 경계를 소유한다. D3D11 호출은 금지한다.
- **Render**는 device, immediate context, swap chain, back buffer, resize, draw, `Present`의 유일한 소유자다.
- **Audio**는 실시간 경로에서 잠금, 할당, 파일 I/O를 하지 않는다.
- **IO**는 CPU 디코드 결과를 완료 큐에 넣고, GPU 리소스 생성은 Render Thread의 안전 지점에서 한다.

## 현재 구현

| 항목 | 구현 위치 | 상태 |
|---|---|---|
| Main / Render 분리 | `src/main.cpp`, `src/render/Dx11Renderer.*` | 완료 |
| 최신 프레임 mailbox | `Dx11Renderer::Submit` | 완료 |
| VSync / 목표 FPS | `FrameSettings` | 완료 |
| ThreadPool, Job, Fence | `src/core/JobSystem.*` | 초기 구현 완료 |
| 연속 데이터 + chunked parallel-for | `std::vector<MovingParticle>` | 검증용 초기 구현 |
| SpriteBatch / texture atlas | - | 다음 단계 |
| IO, Audio, work stealing | - | 이후 측정 후 도입 |

## 프레임 설정

```cpp
renderer.SetFrameSettings({
    .targetFramesPerSecond = 60,
    .verticalSync = true,
});
```

- `verticalSync = true`: `Present(1, 0)`을 사용하며 디스플레이 새로 고침이 상한이다. 이 경우 `targetFramesPerSecond`는 적용하지 않는다.
- `verticalSync = false`, `targetFramesPerSecond > 0`: `Present(0, 0)` 뒤 Render Thread가 해당 FPS로 제한한다.
- `verticalSync = false`, `targetFramesPerSecond = 0`: 제한 없이 렌더링한다.

## 코어 수별 Worker 시작값

물리 코어와 OS/드라이버 상황에 따라 측정해 변경한다. 논리 코어를 모두 워커로 쓰는 것은 기본 정책이 아니다.

| 물리 코어 | Worker 시작값 |
|---:|---:|
| 4 | 1~2 |
| 8 | 4~6 |
| 12 | 7~10 |
| 16 | 10~14 |

현재 코드는 `logical processor - 2`, 최소 1·최대 14를 보수적 초기값으로 사용한다. 추후 설정 파일/옵션 메뉴로 노출한다.

## 연속 메모리와 ParallelFor

```cpp
std::vector<MovingParticle> particles;
jobs.ParallelFor(0, particles.size(), 2048, UpdateChunk).Wait();
```

각 워커는 겹치지 않는 연속 `[begin, end)` 범위만 쓴다. 청크 크기는 2,048~16,384 객체 또는 약 0.1~0.5 ms 작업 시간을 시작점으로 두고 프로파일링한다. 작업 비용 편차가 크면 청크를 줄인다.

### AoS와 SoA

- 일반 게임 객체처럼 여러 필드를 함께 읽는 초기 구조는 AoS(`vector<Object>`)가 구현하기 쉽다.
- 파티클, 탄환, 군중 이동처럼 동일 필드만 대량 처리할 때는 SoA(`x[]`, `y[]`, `vx[]`, `vy[]`)가 캐시와 SIMD에 유리하다.
- 엔진 전체를 한 번에 SoA로 바꾸지 말고, 측정된 핫패스부터 전환한다.

### 캐시와 False Sharing 규칙

- 인접하지 않은 랜덤 포인터 순회보다 연속 범위 순회를 우선한다.
- 워커가 동일 cache line 또는 동일 인덱스를 쓰지 않게 청크를 분할한다.
- 객체마다 공유 atomic 카운터를 증가시키지 않는다. 워커별 출력 버퍼를 만들고 Fence 뒤에 병합한다.
- 병렬 순회 중 `vector::push_back`, 엔티티 생성/파괴, 재할당을 하지 않는다. 명령 버퍼로 모아 Main의 병합 단계에서 적용한다.

## JobSystem / Fence 계약

```text
Main: ParallelFor 제출 → Worker가 독립 range update → Fence 완료 → Snapshot 작성
```

- `JobFence::Wait()`는 프레임 단계 경계에서만 사용한다.
- Job이 참조한 메모리는 Fence 완료 전까지 살아 있어야 한다.
- 현재는 mutex/condition-variable 기반 단일 큐다. 큐 경합이 실제 병목일 때만 worker별 deque와 work stealing을 추가한다.

## 스냅샷과 동기화 규칙

1. 하나의 프레임 단계에서 가변 데이터의 쓰기 소유자는 하나다.
2. Job은 자기 청크만 쓴다. 다른 Job 결과가 필요하면 Fence 뒤에 읽는다.
3. `RenderSnapshot`에는 값 또는 자신이 소유한 버퍼만 넣는다. 가변 게임 객체 포인터는 넣지 않는다.
4. Render Thread는 최신 스냅샷 하나만 보관한다. 오래된 미렌더 프레임을 폐기해 메모리와 입력 지연을 제한한다.
5. 창 resize 요청은 Main에서 전달하지만 `ResizeBuffers`는 Render Thread만 호출한다.

## 처리량: 예시일 뿐, 벤치마크 아님

64 B 내외 객체, 연속 접근, 단순 위치 갱신을 가정한 설계용 추정치다. CPU, RAM 대역폭, SIMD, 캐시 적중률, 객체 연산에 따라 수 배 이상 달라진다.

| Worker | 예시 범위 |
|---:|---:|
| 1 | 10~30 M object/s |
| 2 | 20~60 M object/s |
| 4 | 40~110 M object/s |
| 8 | 70~180 M object/s |
| 12 | 90~220 M object/s |
| 16 | 100~240 M object/s |

스레드를 늘려도 메모리 대역폭과 캐시 병목 때문에 선형 확장은 멈춘다. 성능 판단은 반드시 릴리스 빌드와 실제 씬에서 프레임 시간, Fence 대기, worker별 편차, cache miss, GPU 시간을 측정해 수행한다.

## 로드맵

1. **현재:** Main–Render 분리, frame settings, JobSystem, 연속 데이터 ParallelFor.
2. `MovingParticle` 결과를 `RenderSnapshot`의 `SpriteDraw` 배열로 옮긴다.
3. Render Thread에 dynamic vertex buffer + texture atlas 기반 SpriteBatch를 추가한다.
4. 생성/파괴·이벤트의 워커별 명령 버퍼와 병합 단계를 추가한다.
5. 고정 timestep 물리, 애니메이션, 컬링을 독립 Job으로 옮긴다.
6. CPU/GPU profiler와 frame-time HUD를 추가해 worker 수·청크 크기를 측정한다.
7. 측정 근거가 생길 때 SoA/SIMD, work stealing, IO/Audio 전용 스레드를 추가한다.
