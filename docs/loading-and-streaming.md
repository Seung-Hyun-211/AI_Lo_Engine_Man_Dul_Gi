# 로딩 · 스트리밍 설계 (Loading & Streaming)

씬 전환 / 큰 위치 이동에서 **화면을 가리는 로딩 커튼**(진행바 + 로테이션 팁)과,
그 뒤에서 도는 **비동기 에셋 로드 파이프라인**, 그리고 한 맵 안 인접 구역을
플레이 중 미리 당겨오는 **백그라운드 프리스트림**의 구조.

**상태: 설계만 (구현 전).** 이 문서가 계약이고, 코드는 아직 없다. §9에 지을 것 목록.

관련: `docs/scene-flow-design.md`(GameState 패턴), `docs/multithreaded_game_engine_architecture.md`(스레드 경계·1슬롯 메일박스), `docs/time-design.md`(고정 스텝), `docs/model-animation-research.md`·`docs/demo-scene.md`(현재 에셋 로드가 어디서 일어나는지), `docs/ui-architecture.md`(위젯).

---

## 0. 지금 뭐가 문제인가

- **에셋 로드가 전부 `ModelMeshPass3D::Initialize` 안에서 동기로 일어난다** — 렌더 스레드에서, `Start()` 전에 한 번. 메인 FBX + 애니 클립 FBX 26개 파싱 + TGA 디코드 + GPU 버퍼 생성이 한 줄로 이어진다. 그래서 시작이 느리고(검은 창), **시작 후에 뭔가를 더 로드할 방법이 없다**.
- 씬은 `Application` 생성자에서 `Simulation` 이 한 번 만들어지고 끝이다. 런타임 씬 교체 개념이 없다.
- 따라서 "다른 위치로 넘어가기 / 씬 전환" 을 하려면 **로드 시점을 프레임 루프 밖으로 빼고, 진행 상황을 화면에 표시하고, 다 되면 월드를 갈아끼우는** 3개 레이어가 필요하다.

## 1. 두 개의 레이어

| 레이어 | 책임 | 스레드 |
|---|---|---|
| **전환 상태기 (보이는 쪽)** | `GameState::Loading` 진입/탈출, 커튼 UI, 진행바·팁 갱신, "다 되면" 월드 스왑 | 메인 |
| **비동기 로드 파이프라인 (기능 쪽)** | 매니페스트 diff → CPU 디코드(파싱) → 렌더 스레드 시간분할 GPU 업로드 → 레지스트리 등록 | 전용 IO 스레드 1~2개 + 렌더 스레드 업로드 펌프 |

핵심 불변식(엔진 규칙 그대로):

- **D3D11 은 렌더 스레드에서만.** IO 스레드는 CPU 버퍼까지만 만든다(`import::Model`, 디코드된 RGBA, `import::AnimationClip`). GPU 오브젝트는 렌더 스레드의 업로드 펌프에서만 생성.
- **메인 루프를 절대 블록하지 않는다.** `loader.Wait()` 금지(종료 때만). 매 프레임 `Poll()` 만. 렌더 스레드는 로딩 중에도 계속 Present → 커튼이 60fps 로 애니메이션.
- **월드 교체는 프레임 경계에서.** `Simulation::Step` 도중이 아니라 스텝 사이 메인 스레드에서. 결정성 유지(`time-design.md`).
- 파싱 같은 **긴 작업에 `JobSystem::ParallelFor` 를 쓰지 않는다** — 짧은 잡·연속 범위·워커 내 할당 금지라는 그 계약을 깬다. 전용 `core::AssetLoader` 스레드.

---

## 2. 전환 상태기 (GameState::Loading)

`docs/scene-flow-design.md` 가 열어둔 확장점 그대로 — `GameState` 에 값 추가 + `Application` 에 전환 함수.

```cpp
enum class GameState { Title, InGame, Loading };

enum class TransitionKind { Curtain, Streamed };
struct TransitionRequest { SceneId target; SpawnPoint spawn; TransitionKind kind; };
```

### 2.1 커튼(discrete) 흐름

```cpp
void Application::BeginTransition(const TransitionRequest& req)
{
    m_state = GameState::Loading;
    m_pendingSpawn = req.spawn;
    // 오버레이로 얹는다: 직전 프레임의 씬이 커튼 뒤에 남아 페이드 대상이 됨.
    m_ui.SetOverlay(BuildLoadingScreen(TipSetFor(req.target), SplashIdFor(req.target)));
    m_sceneLoader.Request(req.target, req.kind == TransitionKind::Curtain
                                          ? LoadPriority::Curtain : LoadPriority::Low);
}
```

`Run()` 스텝 게이팅은 이미 이 상태를 "정지" 로 취급한다(`m_state == InGame && !HasOverlay()` — Loading 은 InGame 이 아니고 오버레이도 있음). 매 프레임 로더를 폴링:

```cpp
if (m_state == GameState::Loading)
{
    const LoadStatus s = m_sceneLoader.Poll();
    m_loadingScreen->SetProgress(s.shownProgress);   // §5 이징된 값
    m_loadingScreen->TickTips(delta);                // 팁 로테이션

    if (s.phase == LoadPhase::Failed)
        m_loadingScreen->ShowError(s.error);         // 재시도 / 타이틀로 버튼
    else if (s.phase == LoadPhase::Ready && m_loadingScreen->MinTimeElapsed())
    {
        m_simulation.AdoptWorld(m_sceneLoader.TakeWorld(), m_pendingSpawn);  // 프레임 경계 스왑
        m_ui.ClearOverlay();
        m_state = GameState::InGame;
        m_ui.SetScreen(BuildInGameHud([this] { OpenSettings(); }));
    }
}
```

- **월드 스왑**: `Simulation::AdoptWorld(std::unique_ptr<World>, SpawnPoint)` — 기존 `m_world` 파괴(그 안 엔티티/콜라이더/컴포넌트 전부), 새 월드 설치, 플레이어를 `spawn` 에 배치. `SnapshotBuilder` 는 다음 프레임부터 새 월드를 읽는다(렌더러 변경 없음).
- **에셋 해제**: 스왑 시 이전 매니페스트에만 있던 에셋은 레지스트리에서 decref. 실제 메모리 해제는 LRU 로 지연(§6·§4.4).

### 2.2 스트리밍(silent) 흐름 — 한 맵 안 인접 구역

커튼 없이. 트리거 볼륨이 미리 로드를 시작하고, 경계를 넘을 때 스왑한다.

```cpp
// 구역 경계 근처 트리거 볼륨의 onEnter:
trigger->onEnter = [this](Entity&) { m_sceneLoader.Prefetch(SceneId::EastWing, LoadPriority::Low); };

// 경계 자체를 넘는 트리거의 onEnter:
boundary->onEnter = [this](Entity&) {
    if (m_sceneLoader.Ready(SceneId::EastWing))
        BeginStreamedSwap(SceneId::EastWing);   // 커튼 없이, 200~300ms 크로스페이드
    else
        BeginTransition({ SceneId::EastWing, spawn, TransitionKind::Curtain });  // 폴백: 커튼
};
```

- `Prefetch` 는 `Request` 와 같은 매니페스트 diff 를 돌리되 **낮은 우선순위** 로 큐에 넣는다 — 커튼 작업이 생기면 뒤로 밀린다.
- **더블 버퍼 월드**: `Simulation` 이 `m_world` 외에 `std::unique_ptr<World> m_pendingWorld` 를 든다. 프리스트림이 끝나면 `pendingWorld` 를 미리 조립해 둔다(현재 월드는 계속 스텝). 경계에서 `AdoptPending()` 이 스텝 경계에 스왑.
- **아직 준비 안 됨** → `Prefetch` 를 Curtain 우선순위로 승격 + 커튼 표시. 매끄러운 폴백.
- 크로스페이드: 스왑 프레임부터 풀스크린 알파 페이드 쿼드를 UI 오버레이로 200~300ms(`QuadPass2D`, 이미 straight-alpha 블렌딩 됨).

```text
        Title ──START──► InGame ──────────────────────────────┐
          ▲                │  ▲                                │
          │  타이틀로       │  │ Streamed swap (준비됨: 커튼 없음)  │
          │               │  └───────── 인접 구역 ◄────────────┘
          │       BeginTransition(Curtain)
          │               ▼
          └────────────  Loading  ── Ready & minTime ──► InGame(target)
                          │  Failed
                          ▼
                    에러 패널(재시도 / 타이틀로)
```

---

## 3. 비동기 로드 파이프라인

두 조각: 재사용 프리미티브 `core::AssetLoader`(스레드 + 레지스트리) + 게임 오케스트레이터 `game::SceneLoader`(씬 매니페스트를 안다).

### 3.1 core::AssetLoader

```cpp
enum class AssetKind { Model, Texture, AnimationClip, /* Audio, ... */ };
enum class AssetState { Queued, CpuReady, GpuReady, Failed };
enum class LoadPriority { Curtain, Low };

using AssetId = std::uint64_t;   // = FNV-1a(정규화 경로).  나중에 hash(bytes) 로 내용 dedupe 가능

struct LoadItem { AssetId id; AssetKind kind; std::string path; float weight; LoadPriority prio; };

class AssetLoader   // core, 재사용
{
public:
    void Enqueue(std::span<const LoadItem>);          // 메인 스레드
    void Cancel(std::span<const AssetId>);            // stop 요청

    // 렌더 스레드가 프레임마다 호출: CpuReady 항목을 최대 budget µs 만큼 GPU 로 올린다.
    void PumpUploads(ID3D11Device*, ID3D11DeviceContext*, int budgetMicros);

    struct Progress { float weightDone, weightTotal; int labelIndex; bool anyFailed; };
    [[nodiscard]] Progress Poll() const;              // atomic 스냅샷, 락 없음
    [[nodiscard]] AssetRegistry& Registry();

private:
    std::vector<std::jthread> m_io;   // clamp(hw_concurrency/4, 1, 2) 개
    // 우선순위 큐 (Curtain 먼저) + 결과 큐 (CpuAsset, 성장 가능 — 1슬롯 아님)
};
```

- **IO 워커 루프**: 큐에서 pop → `kind` 별 CPU 디코드
  - `Model`  → `import::LoadModelFromFile` → `import::Model`
  - `Texture`→ `import::LoadTga` → `DecodedImage{ w, h, std::vector<std::uint8_t> rgba }`
  - `AnimationClip` → `import::LoadAnimationClipsFromFile` → `import::AnimationClip`
  - 결과를 `CpuAsset{ id, kind, payload(값 소유), bytes }` 로 **결과 큐**에 push, 레지스트리 항목을 `CpuReady` 로.
  - 항목마다 `stop_token` 체크 → 취소면 버림.
- **렌더 스레드 업로드 펌프**(`PumpUploads`): 결과 큐에서 예산 안에서 뽑아 `CreateBuffer`/`CreateTexture2D`/`CreateShaderResourceView` → 레지스트리 항목을 `GpuReady` + GPU 핸들 저장. **시간분할** 이라 텍스처가 우르르 몰려도 Present 주기가 안 튐(§4.3).
- 스레드 경계로 넘어가는 `CpuAsset` 은 값 페이로드(자기 버퍼 소유). `RenderSnapshot` 메일박스와 같은 정신인데 **1슬롯이 아니라 성장 큐** — 업로드는 하나도 버리면 안 되니까.

### 3.2 core::AssetRegistry

```cpp
struct AssetEntry
{
    int refCount{ 0 };
    AssetState state{ AssetState::Queued };
    std::size_t bytes{ 0 };
    std::uint64_t lastUsedFrame{ 0 };
    // CpuReady: 디코드 결과 보관(업로드 후 비움 가능)
    // GpuReady: GPU 핸들 (ID3D11Buffer* / SRV*, RAII 래퍼)
};

class AssetRegistry
{
public:
    void AddRef(AssetId);  void Release(AssetId);            // 메인
    [[nodiscard]] AssetState State(AssetId) const;
    [[nodiscard]] const GpuModel*   Model(AssetId) const;    // GpuReady 아니면 nullptr
    [[nodiscard]] const GpuTexture* Texture(AssetId) const;
    void EvictColdUntilUnder(std::size_t byteBudget, std::uint64_t frame);   // LRU
};
```

- 뮤텍스 1개면 충분하다 — 접근이 짧고(포인터/카운터), 경합이 적다. 로더 스레드가 `CpuReady`, 렌더 스레드가 `GpuReady`, 메인이 ref 증감·조회.
- `refCount == 0` 이라고 바로 free 하지 않는다. "warm" 으로 남겨두고 `EvictColdUntilUnder` 가 바이트 예산 초과 시 `lastUsedFrame` 오래된 것부터 해제(§6).

### 3.3 game::SceneLoader

```cpp
struct SceneManifest { SceneId id; std::vector<AssetId> assets; };   // 지금은 코드 테이블, 나중에 파일

class SceneLoader
{
public:
    void Request(SceneId, LoadPriority);     // 현재→다음 매니페스트 diff, Enqueue
    void Prefetch(SceneId, LoadPriority);    // Request 와 같되 결과를 pendingWorld 쪽으로
    [[nodiscard]] bool Ready(SceneId) const; // 그 씬 에셋 전부 GpuReady?
    [[nodiscard]] LoadStatus Poll() const;   // { phase, shownProgress(이징), error }
    [[nodiscard]] std::unique_ptr<World> TakeWorld();   // Ready 후 조립된 월드 인계
};
```

- **매니페스트 diff** 가 성능 핵심:
  - `toLoad  = next.assets \ resident`     → `AssetLoader::Enqueue`
  - `toKeep  = next.assets ∩ resident`     → `Registry::AddRef` 만(재파싱 0)
  - `toDrop  = current.assets \ next.assets` → `Registry::Release`
  - 구역 A→B 가 `unitychan.fbx` 를 공유하면 **아무것도 다시 안 읽는다**.
- 모든 에셋이 `GpuReady` 가 되면 `BuildWorld(SceneId, Registry&)` 로 엔티티를 조립(에셋은 `AssetId` 로 참조) → `TakeWorld()` 가 그걸 넘긴다.

---

## 4. 기능적 / 성능적 고려사항

1. **로더 스레드에서 D3D11 금지** (규칙 1). CPU 디코드까지만. GPU 생성은 렌더 스레드 `PumpUploads` 만.
2. **긴 파싱에 `JobSystem::ParallelFor` 금지** — 짧은 잡·연속 `[begin,end)`·워커 내 할당/엔티티 생성 금지라는 계약(규칙 6)을 깬다. 프레임 단계 fence 도 막힌다. 전용 IO 스레드.
3. **GPU 업로드 시간분할** — 프레임당 업로드량을 µs 예산 또는 항목 수로 상한. 텍스처 다발이 커튼 걷히는 순간 프레임타임을 튀게 하지 않도록.
4. **업로드 큐는 1슬롯 메일박스가 아니다** — `RenderSnapshot` 과 달리 업로드는 전부 적용돼야 한다. 성장 가능한 MPSC/SPSC 큐.
5. **프레임 경계 스왑** — 월드 파괴/설치는 메인 스레드에서 스텝 사이에. `Simulation::Step` 도중 `m_world` 를 건드리지 않는다. 결정성(`time-design.md`).
6. **메인 루프 논블록** — `loader.Wait()` 은 종료 시에만. 프레임 루프에선 `Poll()`. 렌더 스레드가 계속 Present 해야 커튼이 애니메이션된다.
7. **레퍼런스 카운트 + 매니페스트 diff** — 씬 간 공유 에셋은 한 번만 로드, 델타만 이동.
8. **메모리 예산 + LRU** — 레지스트리가 상주 바이트 추적. 전환 시 ref-0 에셋은 "warm" 으로 두다가 소프트 상한 초과하면 오래된 것부터 evict. 긴 플레이 세션·잦은 전환에서 증가 방지.
9. **취소** — 항목마다 `stop_token`. 전환을 되무르면 큐 작업 취소 + 인플라이트 CPU 결과 폐기(이미 GPU 올라간 건 refcount 0 → LRU).
10. **실패 격리** — 나쁜 에셋은 `Entry` 를 `Failed` 로 표시하고 커튼에 보고(재시도 / 타이틀로). 크래시 없음, 반쯤 로드된 월드 설치 없음.
11. **최소 표시 시간** — 로드가 즉시 끝나도 커튼 ≥ N ms(예: 500ms). 진행바는 이징(§5)이라 순간이동 안 함.
12. **스레드 개수** — IO 스레드 `clamp(hw_concurrency/4, 1, 2)`. `JobSystem` 풀·렌더 스레드를 굶기면 안 됨. OS 우선순위 낮추기는 선택.
13. **파이프라인 워밍업** — 새 머티리얼/셰이더 순열의 첫 드로우가 드라이버 컴파일로 히칭할 수 있다. 커튼 동안 1px 오프스크린 워밍 드로우를 날려 커튼 걷힌 첫 프레임을 매끄럽게(고급, 나중).
14. **시작도 같은 경로로** — `ModelMeshPass3D::Initialize` 의 동기 FBX/TGA/클립 로드를 부팅 매니페스트 `SceneLoader::Request(boot)` 로 이관하고 프레임 0부터 커튼. 다초 검은 창 → 창은 즉시 뜨고 로딩바(§9 마이그레이션).
15. **스트리밍 스왑의 결정성** — 나중에 리플레이/넷코드가 붙으면 스왑 프레임을 로깅해야 함. 지금 싱글이면 무관, 메모만.
16. **내용 기반 dedupe** — `AssetId` 는 지금 hash(정규화 경로). 복사본까지 합치려면 hash(bytes) 로.
17. **스냅샷 호환** — `pendingWorld` 조립 중에도 `SnapshotBuilder` 는 `m_world` 만 읽는다. pending 은 adopt 전까지 스냅샷에 아무것도 안 낸다. 렌더러 변경 불필요.

---

## 5. 진행바 "느낌" 모델

- **밑단(진짜)**: 가중치. 항목 `weight` ≈ 예상 비용(FBX 파싱 ~50, TGA 디코드 ~5, GPU 업로드 ~2 — 또는 바이트). `real = weightDone / weightTotal`.
- **표시값**: `shown = max(shown, lerp(shown, min(real, 0.97), k·dt))`
  - **단조** (뒤로 안 감), **이징** (순간이동 안 함), `Ready` 전까지 **97% 상한** — 마지막 히치 동안 100% 에 멈춰 있지 않게.
  - `Ready` 되면 1.0 으로 애니메이션 + `minDisplay`(커튼 총 ≥ 500ms) 유지 후 해제.
- **불확정 폴백**: `weightTotal` 을 모를 때(스트리밍 발견 중)는 채움 대신 바버폴(흐르는 줄무늬).

## 6. 화면 요소 — 커튼에 뭘 넣나

`BuildLoadingScreen(tips, splashId, destinationName)` — 순수 `ui::Widget` 트리. 진행바 위젯은 로더와 무관하게 교체 가능.

- **진행바** — `ui::Slider` 를 비대화형으로 재사용하거나 전용 `ui::ProgressBar`(§9). §5 의 `shown` 값만 받음.
- **로테이션 팁("심심한 텍스트")** — `std::span<const char* const> tips` + 타이머, ~3.5s 마다 비반복 랜덤 교체. 팁 세트를 목적지 `SceneId` 로 키(그 지역 로어 힌트) 또는 전역 조작 팁.
- **목적지 이름 + 스플래시 아트** — 씬별 스플래시 `AssetId` 를 **매니페스트 맨 앞**에 둬서 제일 먼저 떠서 일찍 보여줌.
- **아이들 애니메이션 / 마스코트** — 렌더 스레드가 계속 도니까 스피너/캐릭터 루프가 매끄럽게 돈다.
- **`Ready` 후 "아무 키나" 게이트** (선택) — 팁을 읽을 시간을 주고 싶을 때.
- **컨트롤러 힌트 줄**, **진행 연동 비네트** — 소소한 아이디어.
- **로딩 음악/앰비언스** — 오디오 서브시스템(미존재) 훅만.

---

## 7. 스레드·데이터 흐름 요약

```text
 메인 스레드                IO 스레드(1~2)              렌더 스레드
 ─────────                 ────────────               ──────────
 BeginTransition
   SceneLoader.Request
     매니페스트 diff
     AssetLoader.Enqueue ──► [우선순위 큐]
                              pop → CPU 디코드
                              (import::Model /
                               DecodedImage / Clip)
                             push ──► [결과 큐(성장)] ──► PumpUploads(budget µs)
                                                          Create{Buffer,Texture2D,SRV}
                                                          Registry: GpuReady
 매 프레임:
   status = SceneLoader.Poll()  (atomic 스냅샷)
   loadingScreen.SetProgress(shown)   ← §5 이징
   if Ready && minTime:
     Simulation.AdoptWorld(TakeWorld(), spawn)   ← 프레임 경계
     UI.ClearOverlay(); state = InGame
 (렌더 스레드는 내내 최신 스냅샷 Present → 커튼 60fps)
```

---

## 8. 사용 방법 (How to use)

### 새 씬 추가하기

1. `SceneId` 에 값 추가. `game/SceneManifests.*`(신설)에 그 씬의 `SceneManifest{ id, { assetIdA, assetIdB, ... } }` 를 적는다. 스플래시 아트를 리스트 맨 앞에.
2. `game/SceneWorlds.*` 에 `std::unique_ptr<World> BuildWorld_<Scene>(const AssetRegistry&)` — 엔티티를 조립하되 메시/모델은 `AssetId` 로 참조(`registry.Model(id)` 는 `GpuReady` 아니면 nullptr 이므로 조립 시점엔 이미 준비돼 있다).
3. 진입 트리거(버튼/트리거 볼륨 콜백)에서 `m_sceneLoader.Request(SceneId::X, LoadPriority::Curtain); BeginTransition({ SceneId::X, spawn, TransitionKind::Curtain });`.

### 인게임 프리스트림 걸기 (커튼 없는 인접 구역)

```cpp
// 경계 근처 트리거 볼륨
approach->onEnter = [this](Entity&) { m_sceneLoader.Prefetch(SceneId::EastWing, LoadPriority::Low); };
// 경계 트리거
boundary->onEnter = [this](Entity&) {
    if (m_sceneLoader.Ready(SceneId::EastWing)) BeginStreamedSwap(SceneId::EastWing);
    else BeginTransition({ SceneId::EastWing, spawnEast, TransitionKind::Curtain });
};
```

### 새 에셋 종류 추가하기

`AssetKind` 에 값 + `AssetLoader` IO 워커의 디코드 스위치 한 갈래(엔진 타입 반환) + `PumpUploads` 의 업로드 스위치 한 갈래(`Create*`). 그 외 코드는 안 건드림.

### 로딩 화면 커스터마이즈

`BuildLoadingScreen(...)` 만 고친다 — 순수 위젯 트리. 진행바 위젯 교체는 로더/상태기와 무관. 팁 세트는 `TipSetFor(SceneId)` 테이블.

### 진행바 느낌 조정

`SceneLoader::Poll()` 안의 이징 상수(`k`, 상한 `0.97`, `minDisplay`)만. 항목 `weight` 추정치는 `SceneManifests` 옆 테이블.

### 하지 말 것

- 로더 스레드에서 D3D11 호출 금지 — CPU 디코드만. GPU 는 렌더 스레드 `PumpUploads`.
- 파싱을 `JobSystem::ParallelFor` 로 돌리지 않는다 — 그 계약(규칙 6)을 깬다.
- 프레임 루프에서 `loader.Wait()` / `join` 금지 — 폴링만. 렌더 스레드가 Present 를 멈추면 커튼이 얼어붙는다.
- `Simulation::Step` 도중 `m_world` / `m_pendingWorld` 를 스왑하지 않는다 — 스텝 경계에서만(`AdoptWorld`/`AdoptPending`).
- 업로드 결과 큐 항목을 드롭하지 않는다(1슬롯 메일박스 아님).
- `phase != Ready` 인데 진행바에 실제 100% 를 보여주지 않는다(§5 상한).
- `GameState` 를 전환 함수 밖에서 직접 대입하지 않는다(`scene-flow-design.md` 불변식).

---

## 9. 지을 것 (이번 설계에 미포함)

- `core/AssetLoader.{h,cpp}` — IO 스레드 풀 + 우선순위 큐 + 결과 큐 + `PumpUploads`.
- `core/AssetRegistry.{h,cpp}` — ref-count 항목 맵 + 상태 + LRU evict. (`core::EntityRegistry` 와 이름만 비슷, 별개.)
- `game/SceneLoader.{h,cpp}` — 매니페스트 diff, `Request`/`Prefetch`/`Ready`/`Poll`/`TakeWorld`.
- `game/SceneManifests.{h,cpp}`, `game/SceneWorlds.{h,cpp}` — `SceneId`, 매니페스트 테이블, `BuildWorld_*`.
- `game/LoadingScreen.{h,cpp}` — `BuildLoadingScreen(...)`, 팁 로테이션, `ShowError`, `MinTimeElapsed`.
- `ui::ProgressBar` (선택) — `ui::Slider` 재사용으로 갈음 가능.
- `render::IRenderer::PumpUploads(...)` 훅 — 렌더 루프가 패스 사이에서 업로드 예산 소진.
- `game::Application` — `GameState::Loading`, `BeginTransition`/`BeginStreamedSwap`, `Run()` 폴링 분기, `m_sceneLoader`/`m_loadingScreen` 멤버.
- `game::Simulation` — `AdoptWorld(unique_ptr<World>, SpawnPoint)`, `m_pendingWorld` + `AdoptPending()`.
- **마이그레이션**: `ModelMeshPass3D` 가 `Initialize` 에서 로드하지 말고 `AssetRegistry` 에서 `GpuReady` 리소스를 `AssetId` 로 당겨오게. 부팅 매니페스트 + 프레임 0 커튼. (가장 큰 리팩터 — 마지막.)
- **테스트 훅**: `SceneLoader` 에 가짜 타이머 진행률 소스(실제 IO 없이 커튼/진행바/스왑 흐름만 검증).
