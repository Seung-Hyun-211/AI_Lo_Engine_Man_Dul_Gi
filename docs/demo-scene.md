# 데모 씬 (Demo Scene) — unity_chan 캐릭터 컨트롤러 + 팔로우 카메라

뼈대가 살아있음을 보이는 최소 콘텐츠. 실제 게임 로직은 아니지만, 이제 조작 가능한
3인칭 캐릭터다: WASD 이동, Space 점프, Shift 달리기, 마우스로 카메라 궤도 회전,
그리고 wait/walk/run/jump 애니메이션 전환.

관련 문서: [command-playbook.md](command-playbook.md) 4b(카메라)·3e(스키닝), [model-animation-research.md](model-animation-research.md) §5.2a/§5.3, [animation-design.md](animation-design.md) §1, [time-design.md](time-design.md).

## 흐름 (누가 무엇을 하나)

```text
Win32Window (platform/)
  · RegisterRawInputDevices(mouse)  → WM_INPUT  → IWindowEventSink::OnMouseDelta(dx,dy)   [잠금 중일 때만]
  · SetPointerLocked(bool)          → 커서 숨김 + 화면중앙 재배치 + ClipCursor(클라이언트)
                                       포커스 잃으면 자동 해제, 되찾으면 재적용
Application (game/)
  · EnterInGame / CloseSettings   → m_window.SetPointerLocked(true)
    EnterLobby / OpenSettings → m_window.SetPointerLocked(false)
  · OnMouseDelta                  → m_input.OnMouseDelta   (InputState 가 프레임 단위 누적, BeginFrame 에서 리셋)
  · BuildPlayerIntent()           → WASD(+화살표) / Shift / Space(에지) / MouseDelta 를 PlayerIntent 값으로
  · Run 루프 (InGame && !overlay) → m_simulation.UpdateCameraLook(intent.look)   ← 프레임당 1회 (고정스텝 밖)
                                    for step: m_simulation.Step(dt, intent)      ← 고정 timestep
Simulation (game/)
  · UpdateCameraLook  : m_cameraYaw += dx*sens;  m_cameraPitch = clamp(m_cameraPitch - dy*sens, ...)
  · StepActors        : 액터마다 로컬 timeScale 로 dt 스케일(>1 은 서브스텝) → StepOneActor 호출 (time-design.md)
  · StepOneActor      : [플레이어 = m_actors[0]] 카메라 yaw 로 WASD 축을 회전 → 이동 방향, 그 방향으로 회전(kCharTurnRate)
                        Space+접지 → 수직속도 = kCharJumpSpeed, 중력 적분, y=0 에서 착지
                        Locomotion(Jump/Wait/Run/Walk) 결정 → CharacterAnimationState::Update
                        (m_actors[1..] 은 입력 없는 캔드 배회 — 로컬 배속 데모용)
  · getter           : CharacterPosition / CharacterFacingYaw / CameraYaw / CameraPitch / HeroAnimClip{Index,Time}  (전부 m_actors[0] 래퍼)
CharacterAnimationState (game/)
  · Update(dt, Locomotion) : 상태가 바뀌면 해당 클립으로 스냅 + clipTime=0, 아니면 clipTime += dt
                             Locomotion→클립 매핑은 render/r3d/CharacterAnimationClips.h 의 kUnityChan{Wait,Walk,Run,Jump}Clip
SnapshotBuilder (game/)
  · BuildCamera  : 씬 1/3 = 3인칭 오빗, eye = focus - forward*orbitDistance(씬1=3.6, 씬3=5.5).
                   **씬 2는 1인칭**(총구 이펙트 플레이테스트 후 전환, particle-system-research.md
                   §12) — eye = focus 그대로(뒤로 안 뺌), forward 방향을 바로 봄.
                   focus = CharacterPosition + (0,1.3,0), forward = f(camYaw,camPitch) 공통.
  · BuildScene3D : 씬 1/3만 플레이어 모델을 그림(model.world = RotationY(...) * Translation(...)).
                   **씬 2는 1인칭이라 자기 모델을 안 그림** — 총구 파티클을 가리던 문제 해결.
```

스레드 경계는 여전히 값 기반 `RenderSnapshot` 하나뿐 — 카메라도 캐릭터도 `Mat4`/`Vec3`/`int`/`float` 로만 넘어간다.

## 사용 방법 (How to use)

### 조작 튜닝

- **속도·중력·회전·감도**: `Simulation` 의 `static constexpr` 상수(`kCharWalkSpeed`, `kCharRunSpeed`,
  `kCharJumpSpeed`, `kCharGravity`, `kCharTurnRate`, `kMouseSensitivity`, `kCamPitchMin/Max`, `kCharHalfRange`).
  숫자만 바꾸면 된다.
- **카메라 거리/높이**: `SnapshotBuilder.cpp` 의 `BuildCamera` — `focus` 의 `+ (0, 1.3, 0)` 와 `eye = focus - forward * 3.6f`.
- **마우스 상하 반전**: `Simulation::UpdateCameraLook` 의 `m_cameraPitch - mouseDelta.y*...` 를 `+` 로.
- **캐릭터가 뒤로 달리면**: `SnapshotBuilder.cpp` 의 `kModelYawOffset` 를 `0.0f` ↔ `kPi` 로 뒤집는다
  (unity_chan 메시가 로컬 +Z 를 보는지 -Z 를 보는지의 문제).

### 다른 클립 쓰기 / 상태 추가

1. `render/r3d/CharacterAnimationClips.h` 의 `kUnityChanClips` 에 파일이 있는지 확인
   (없으면 `assets/models/unitychan/animation/` 에 FBX 추가 + 배열에 항목).
2. 같은 헤더의 `kUnityChan{Wait,Walk,Run,Jump}Clip` 인덱스를 원하는 클립으로.
3. 상태를 늘리려면: `game/CharacterAnimationState.h` 의 `enum class Locomotion` 에 값 추가 →
   `CharacterAnimationState::ClipFor` 에 매핑 한 줄 → `Simulation::StepOneActor` 의 `loco` 결정 분기에 조건.
   호출부(`SnapshotBuilder`, `ModelMeshPass3D`)는 `ClipIndex()/ClipTime()` 만 읽으므로 안 건드린다.

### 하지 말 것

- `Step()` **밖**에서 캐릭터 물리·애니메이션 시간을 전진시키지 말 것. `UpdateCameraLook` 만 예외다
  (조준값이라 프레임당 1회여야 하고, 물리가 아니다 — 고정 timestep 규칙은 [time-design.md](time-design.md)).
- `PlayerIntent` 에 `InputState` 나 Win32 타입을 넣지 말 것. 이미 해석된 값(축 `[-1,1]`, `bool`, 픽셀 델타)만.
- `SnapshotBuilder` 에서 `simulation.PlayerX` 같은 게임 개념을 렌더러로 하드코딩하지 말 것 —
  `ModelDraw`/`MeshDraw`/`CameraView` 값으로만 방출 (OCP, [engine-overview.md](engine-overview.md) DIP 절).
- 마우스 잠금은 `Win32Window::SetPointerLocked` 로만. 패스나 UI 에서 `ClipCursor`/`ShowCursor` 직접 호출 금지.

## 데모 씬 2 — 언덕 위 조망 + 시뮬레이션 군중

`Simulation::DemoScene`(런타임 열거형, **아래 "사용 방법" 참고 — 로비 메뉴에는 더 이상 버튼이 없고,
`Application::EnterInGame(DemoScene)` 을 코드로 불러야 진입한다, 리빌드는 불필요**) 로 고른다. `CharacterDemo`(1) = 위에서 설명한 로컬 배속 액터 3인 +
작은 슬랩. `DefenseCombat`(2) = **같은 플레이어**가 완만한 언덕(10m 높이, 20도 경사로 아래
평지까지 이어짐 — 원래는 수직 절벽/메사였으나 사격 시야 확보를 위해 경사로로 교체) 위에 서서
앞쪽 넓은 평지를 내려다보고, 그 아래에서 다수의 경량 개체(`SimAgent`)가 언덕을 올라오며
배회한다. 대규모 디펜스 장르([synopsis.md](synopsis.md))의 "다수 오브젝트" 파이프라인 씨앗 —
[defense-combat-design.md](defense-combat-design.md) §0~§7의 무기 5종/무한 웨이브/패배·재시작이
전부 이 씬 위에서 동작. `ShadowShowcase`(3) = 크라우드 없는 그림자/조명 쇼케이스 — 정적 씬이라
이 문서보다 [shadows.md](shadows.md) "씬 3" 이 계약. `EffectsTest`(4) = 파티클 이펙트만
독립적으로 미리보는 빈 사격장 — 아래 "데모 씬 EffectsTest" 참고.

```text
Simulation (game/)
  · SpawnActors        : ActiveScene()==DefenseCombat → 플레이어 1명만. pos.y = kHillHeight(10.0),
                         groundY = kHillHeight (그 높이에 착지), halfRange = kPlateauHalf(5.0,
                         원점 대칭 클램프) — 플레이어는 언덕 꼭대기 평지 안에서만 움직이고
                         경사로 자체는 걸어 내려가지 않는다(카메라 시야만 바뀜). 평지 끝
                         (kPlateauHalf+1.0 = kHillTopZ)부터 경사로가 시작, kHillSlopeDeg(20도)로
                         kHillHeight(10m) 만큼 내려가 평지(높이 0)와 만난다. m_cameraPitch = -0.5.
  · SpawnSimAgents     : m_agents(core::ObjectPool<SimAgent>).Init(kActiveCrowd.capacity) →
                         kActiveCrowd.count 번 Acquire + SeedAgent(결정적, RNG 없음).
                         핸들은 m_agentHandles 에 보관(churn 용). 수·모델은 game/CrowdConfig.h.
  · StepSimAgents(dt)  : ParallelFor(청크 32) 가 m_agents.ActiveIndices() 를 겹치지 않는
                         [begin,end) 로 — Slots()[active[k]] 만 쓰기(슬롯 인덱스 유일 → 충돌
                         없음). heading 드리프트 + 전진 + bob + 필드 경계 반사 + animTime +=
                         dt*(speed/1.4)(워크사이클 desync, SeedAgent 가 오프셋). 그 뒤 메인에서
                         churn: 12스텝마다 1마리 Release→Acquire→SeedAgent (풀 상시 검증, 게임
                         메커닉 아님). ActiveIndices() 가 비면(씬 1) 즉시 반환.
  · UpdateCrowdQueries(): (씬 2만) Application 이 스텝 루프 뒤 **프레임당 1회** 호출
                        (Step() 안이 아님 — O(crowd) 라 서브스텝마다 돌리면 느린 프레임이
                        스파이럴). m_collision3d.Clear() + 크라우드마다 Sphere Collider3D
                        (user=슬롯) Add → (1) 플레이어 시선 레이(head + 카메라 forward,
                        maxDist=kLookRayRange, mask=kLayerCrowd3D) RaycastClosest →
                        LookRayResult, (2) Step()(균일 그리드 브로드페이즈) → Contacts()
                        로 m_agentTouch[slot] 채움. 시뮬 상태 안 건드림(렌더 전용).
  · Actor.groundY/halfRange : 액터별 바닥 높이·이동 반경. 씬 1 은 기본값(0 / 7.5)이라 동작 불변.
SnapshotBuilder (game/)
  · BuildCamera   : ActiveScene()==DefenseCombat 면 1인칭(위 §데모씬2 참고), 아니면 3인칭 오빗.
  · BuildLighting : 씬 2 는 셰도우 ortho 를 넓히고(22→64) 중심을 +Z 로 밀어 군중을 덮는다.
  · BuildScene3D  : ActiveScene()==DefenseCombat → BuildCliffScene (넓은 평지 Plane + 플레이어 평지 Cube +
                    경사 램프 Plane(RotationX) + 기둥 마커 + **크라우드 = 인스턴스드**: 메시/높이/피벗은 kActiveCrowd
                    (Cube→MeshId::Cube 중심피벗, Model→MeshId::CrowdModel 발피벗).
                    m_agents.ActiveIndices() 순회, 프러스텀·최대거리 컬 + 거리 LOD 2단계
                    (d2 <= kAgentShadowDist² → 근 lod0, 아니면 원 lod2)로 lodBucket 나눠 배치
                    0~2개. inst.scale = kActiveCrowd.height. 색(flat tint): LookRay().agentSlot
                    = 노랑, AgentTouching()[slot] = 빨강, 그 외 속도 램프. 디버그 시선 레이 +
                    hit 마커). 씬 1(kBoxes)은 else.
  · MeshPass3D    : Initialize→LoadCrowdMesh() — kCrowdModelFbx(Zombie1.FBX) 를
                    position+normal+uv 로 로드 + per-vertex 본 데이터 보관, Z-up 감지·회전 +
                    발원점·단위높이 정규화 → MeshId::CrowdModel. + kCrowdDiffuseTex(Zombie.tga)
                    → _UNORM_SRGB SRV(없으면 white). + **VAT 베이크**: kCrowdClipFbx(Zombie@Z_Run)
                    클립을 kVatFps(24)로, 프레임마다 AnimationSampler+LBS → normalise(zUpRotate 는
                    안 걸음 — 스킨 행렬이 이미 포함) + 루트 모션 XZ 스트립 →
                    R32G32B32A32_FLOAT [verts×frames] 텍스처(t2). 경로 비면 스킵, 메시 실패 시
                    큐브 폴백. instanceBatches 를 배치당 DrawIndexedInstanced 1콜 —
                    mesh_instanced.hlsl VS 가 CrowdModel 배치엔 animTime 으로 VAT 행 Load(큐브는
                    바인드포즈), PS 는 diffuse@t0. 셰도우 패스는 lod>=2 스킵 + 바인드포즈(VAT 미적용).
```

`SimAgent` 는 동질적이라 `EntityId` 없이 `core::ObjectPool<SimAgent>`(AoS, 슬롯 고정 +
`ActiveIndices()`) 에 산다 ([entity-lifecycle-design.md](entity-lifecycle-design.md) §3A,
[scrollable-list-and-pool.md](scrollable-list-and-pool.md) §1.1). 스레드 경계는 여전히 값뿐 —
군중은 `render::MeshInstance`(28B POD: pos/yaw/scale/color/animTime) 배열 + `InstanceBatch` 로만
스냅샷에 실린다.

### 사용 방법 (How to use)

- **씬 전환은 런타임** (리빌드 불필요) — 앱은 곧장 CIRCULAR 로 부팅(타이틀 화면 없음) — ESC → 설정 → **"LOBBY"** 로 로비 메뉴로
  돌아갈 수 있다(`LobbyScreen.h/.cpp` `BuildLobbyScreen`). **로비는 2026-09 사용자 결정으로 버튼이 GAME(=`Circular`
  정상 실행) · TEST SCENE(=`Simulation::EnterCircularTestScene()`, 체력 99999 더미 몹 1마리만 있는 무기 테스트용 씬,
  [circular-design.md](circular-design.md) §부록) 딱 두 개다** — 이 문서의 3D 씬들(DEFENSE COMBAT/CHARACTER DEMO/
  SHADOW SHOWCASE/EFFECTS TEST)은 어떤 메뉴에서도 더 이상 고를 수 없다(코드는 그대로 있음 — `Simulation::EnterScene`도
  여전히 받고, `Application::EnterInGame(DemoScene)`으로 프로그램적으로는 진입 가능 — 그냥 UI 버튼이 없을 뿐).
  **`Circular`(5)는 2D 뱀서라이크 씬 — 이 문서의 3D 씬들과 별개, [circular-design.md](circular-design.md)가 계약.**
  `Simulation::kDemoScene`(예전 `static constexpr int`)는 이제 `enum class
  DemoScene {CharacterDemo=1, DefenseCombat=2, ShadowShowcase=3, EffectsTest=4, Circular=5}` 런타임
  멤버(`m_demoScene`,
  `ActiveScene()`로 읽음) — `Simulation::EnterScene(DemoScene)`이 액터 목록을 지우고 새로
  짓는다(크라우드/기브/오드넌스 풀도 씬이 뭐든 매번 정리 — DefenseCombat 이 아닌 씬에 남은
  크라우드가 계속 `ParallelFor`로 스텝되는 낭비를 막으려고). `SpawnActors`/`SnapshotBuilder`의
  `BuildCamera`/`BuildLighting`/`BuildScene3D` 전부 `if constexpr (kDemoScene==N)` 대신
  `if (simulation.ActiveScene() == DemoScene::N)` 런타임 분기로 바뀜 — 씬마다 다른 코드를
  컴파일해 없애는 이점은 사라졌지만(전부 항상 컴파일됨), 게임 도중 씬을 자유롭게 오갈 수
  있다. `m_demoScene`의 멤버 초기값(`DefenseCombat`)은 오직 `Simulation` 생성 직후(즉 앱을
  막 띄운 순간)에만 의미가 있고, "LOBBY"에서 뭘 누르든 `EnterScene`(또는 `EnterCircularTestScene`)이 그
  값을 덮어써서 실제로 뭐가 뜨는지는 항상 사용자가 고른 씬이다.
- **군중 설정 = `game/CrowdConfig.h` 의 `kActiveCrowd`** (한 줄). 프리셋: `kCrowdBoxes`(600 큐브,
  원래 데모) / `kCrowdZombies`(500/500, `Zombie1.FBX` + VAT + **`CrowdShading::Toon` 기본**,
  실제 플레이 시 체감 끊김 신고를 받고 16384→500 으로 낮춤 — 아래 "풀 churn 버그" 참고).
  `CrowdConfig{count, capacity, mesh, shading, height, colliderRadius}` — `Simulation`(스폰·풀·
  콜라이더)·`SnapshotBuilder`(메시·셰이더·높이·피벗)가 전부 이걸 읽는다. `capacity >= count` 는
  `static_assert` 로 강제. `shading`(`Smooth`=`mesh_instanced.hlsl` / `Toon`=`mesh_instanced_toon.hlsl`,
  엔진 셀 룩)이 배치 셰이더를 고른다(§9.7). 모델 파일은 `MeshPass3D.cpp` 의 `kCrowdModelFbx`.
  상세 [instanced-rendering.md](instanced-rendering.md) §9.5·§9.7.
  렌더는 배치 1~2개 = `DrawIndexedInstanced`, 상한 `MeshPass3D::kMaxInstances`(32768, 여유 폭 —
  count 를 500 으로 낮춘 지금은 훨씬 더 여유).
- **풀 churn 버그(발견·수정됨)**: `SpawnSimAgents()`가 매 웨이브 전환마다(60초 Combat→Prep,
  또 60초 Prep→Combat) `m_agents.Init(capacity)`를 다시 불러 풀 전체를 파괴·재구성하고 있었다 —
  실기기(RTX 5070 Ti)에서 "평균 90fps인데 걸어다니기만 해도 일정 간격으로 뚝뚝 끊긴다"는 신고로
  발견. `EndCombatPhase()`가 이미 모든 핸들을 `Release`해 풀을 완전히 비워두므로 재-`Init`은
  불필요한 반복 작업이었다. `if (m_agents.Capacity() == 0)`로 감싸 최초 1회만 `Init`하게 고침
  ([Simulation.cpp:148](../src/game/Simulation.cpp:148)). 웨이브 3회 순환 동안 스텝 루프
  실행시간이 2ms를 넘는 프레임이 하나도 없는 것으로 확인.
- **폭발 넉백 데모(씬 2 전용)**: `Application` 이 InGame 진입 5초 뒤 자동 1회, 이후 `F` 키로
  재발파 — `Simulation::TriggerExplosion(center, radius, power)` 가 반경 내 `SimAgent` 에게
  바깥+위 임펄스(거리 선형 감쇠)를 줘 포물선으로 튕겨나가게 한다(`SimAgent::vel/airborne`,
  `StepSimAgents` 가 낙하 적분). 좌표·반경·세기는 `Application.cpp` 익명 네임스페이스의
  `kBlastCenter/Radius/Power/kAutoBlastDelay`. `ActiveScene() != DefenseCombat` 면 `TriggerExplosion` 자체가
  no-op. 실제 게임에서는 타이머/키 대신 게임플레이 이벤트(투사체 충돌, 타워 AoE)가 불러야 한다.
- **FPS 표시**: `Application` 이 `1/delta` EMA(`m_fpsSmoothed`) → `SnapshotBuilder::Build(fps)` →
  `ui::DrawRect`+`DrawText` 우상단(모든 화면 위). 끄려면 `Build` 호출에서 `fps` 를 0 으로.
- **좀비 메시**: `Zombie1.FBX`(정적 bind pose, T포즈). **Z-up** 으로 들어와서(ufbx axis target
  무시) `MeshPass3D::LoadCrowdMesh` 가 감지해 `(x,y,z)→(x,z,-y)` 회전(det +1, 이 릭은 정면이
  -Y 라 +Z 를 봄) 후 발 원점·단위 높이 정규화. 문워크면 `(-x,z,y)` 로. 텍스처·애니메이션은
  [instanced-rendering.md](instanced-rendering.md) §9.6.
- **풀 churn 속도**: `kAgentChurnIntervalSteps`(현재 12스텝마다 1마리 재활용 — 데모용 검증 churn).
  키우면 재활용이 덜 눈에 띈다. 웨이브 스폰/디스폰이 생기면 이 churn 은 제거.
- **필드·언덕 치수**: `kHillHeight`(언덕 높이), `kHillSlopeDeg`(경사각), `kPlateauHalf`(플레이어
  이동 반경), `kFieldHalf`(평지 반경), 크라우드 z 범위 `kFieldAgentZLo/Hi`(Simulation.cpp 익명).
  경사로 시작/끝 z(`kHillTopZ`/`kHillBottomZ`)와 크라우드 개체의 실시간 높이(`HillHeightAtZ`)는
  `Simulation.cpp` 익명 네임스페이스에서 파생 — `SnapshotBuilder.cpp` 의 `BuildCliffScene` 이
  같은 공식을 램프 메시 배치에 재사용(주석 참고). 숫자만 바꾸면 언덕·평지·크라우드 높이가
  전부 따라온다.
- **크라우드 컬·LOD**: `BuildCliffScene` 의 `kAgentCullDist`(100, 이 거리 밖 스킵),
  `kAgentShadowDist`(34, 이 거리 밖은 그림자 안 캐스트). 프러스텀 스피어 반경 = `height*0.6`.
- **군중 거동**: `Simulation::StepSimAgents` 의 heading 드리프트 계수·`speed` 범위·bob 진폭,
  초기 배치는 `SeedAgent`. 실제 게임 AI(추적·경로)로 바꿀 때 이 함수만 교체하면 렌더/스냅샷은
  안 건드린다.
- **시선 레이 / 개체 충돌**: `kActiveCrowd.colliderRadius`(크라우드 스피어 반경, 중심 = `height*0.5`),
  `Simulation::kLookRayRange`(80). `UpdateCrowdQueries` 가 `m_collision3d` 를 **프레임당 1회**
  (스텝 루프 밖) rebuild → 시선 레이 `RaycastClosest` + `Step()`(균일 그리드 브로드페이즈, `collider-design.md` "브로드페이즈").
  타워 타겟팅·투사체·분리력을 붙일 때 이 `CollisionWorld3D` 에 `RaycastClosest/Any/All` /
  `Contacts()` 로 질의. 셀 크기·캡은 `CollisionWorld3D.cpp` 익명(`kMinCellSize` 등).

### 하지 말 것 (씬 2 추가분)

- `SimAgent` 스텝을 `Step()` 밖에서 돌리지 말 것 — 고정 timestep 규칙은 씬 1 과 동일.
- `StepSimAgents` 의 잡 람다에서 `m_agents.Acquire()`/`Release()`·다른 잡의 슬롯 접근 금지
  (불변 규칙 6). 스폰/디스폰·churn 은 `ParallelFor().Wait()` **뒤** 메인에서만. 잡은
  `Slots()[active[k]]`(자기 `[begin,end)`) 만 쓴다.
- 군중을 스킨드 모델(`ModelDraw`)이나 개체마다 `MeshDraw` 로 그리지 말 것 — 전자는 CPU 스킨
  1개 상한, 후자는 draw call 폭증. `MeshInstance` + `InstanceBatch` 로
  ([instanced-rendering.md](instanced-rendering.md) §9). 애니메이션 군중은 VAT(§5·horde).
- `LookRayResult` / `AgentTouching()` 를 시뮬 입력으로 쓰지 말 것 — 렌더 전용(카메라 기반이라
  프레임당 1회 성격). 게임플레이가 레이/겹침 결과를 필요로 하면 `Step()`/`RaycastClosest` 를
  `Simulation::Step` 안에서 직접 질의해 시뮬 상태에 반영.
- `Contacts()` 만 필요하면서 `RaycastClosest` 는 필요 없을 때도 `Step()` 은 불러야 한다(레이캐스트
  질의는 `Step()` 없이 돌지만 `Contacts()` 는 `Step()` 이 채운다).

## 데모 씬 EffectsTest — VFX 검증 사격장

좀비/무기/웨이브 루프 없이 파티클 이펙트 3종(머즐 플래시, 폭발, 기브 피 스프레이 —
[particle-system-research.md](particle-system-research.md))만 즉시 미리보기하는 빈 씬. **로비 메뉴에 버튼이
없다**(2026-09 부터 — 위 "사용 방법") — `Application::EnterInGame(DemoScene::EffectsTest)` 를 코드로 불러야 진입한다.

```text
Simulation::SpawnActors (EffectsTest 분기)
  플레이어 1명, pos=(0, 1.6, 0), facingYaw=0 — 그 외 액터 없음.
SnapshotBuilder
  · BuildCamera : DefenseCombat 과 함께 1인칭 취급(원점 그대로, 뒤로 안 뺌).
  · BuildScene3D: 플레이어 모델 자기 자신은 안 그림(1인칭) + BuildEffectsTestScene —
                  20m 오프셋 평지 Plane + 5/10/20/40m 마다 노란 기둥 + 바닥 타깃 플레이트.
  · HUD 텍스트  : "1: MUZZLE FLASH  2: EXPLOSION  3: GIB BLOOD SPRAY" 상단 고정 표시.
Application (프레임 루프, InGame && !overlay)
  · KeyPressed('1'/'2'/'3') → Simulation::PreviewVfxEffect(MuzzleFlash/Explosion/GibBurst)
    (DefenseCombat 의 박격포/지뢰/철조망 키와 물리적으로 같은 키 — 서로 다른 씬이라 충돌 없음,
    양쪽 다 자기 씬이 아니면 내부에서 조용히 no-op).
Simulation::PreviewVfxEffect
  · m_lookRay.origin/dir 를 따라 스폰 — UpdateCrowdQueries()가 EffectsTest에서도 돌아
    (크라우드 콜라이더는 0개라 레이는 항상 빗나가지만 origin/dir 자체는 정상 계산됨) 마우스로
    보는 방향에 맞춰 미리보기가 나온다. MuzzleFlash 는 눈 바로 앞(kMuzzleForwardOffset),
    Explosion/GibBurst 는 kEffectsPreviewDistance(8m) 앞.
```

- **씬을 나가도 안전**: `IsMatchLost()`가 `ActiveScene()==DefenseCombat`도 같이 확인하도록
  고쳐뒀다 — 안 그러면 DefenseCombat 에서 패배(목표 HP 0)한 직후 EffectsTest 로 넘어와도
  `m_objectiveHealth` 가 여전히 0이라 매 프레임 로비 메뉴로 튕기는 버그가 났다.
- **사용 방법**: 마우스로 조준하고 1/2/3 눌러서 원하는 이펙트를 원하는 거리 마커 근처에서
  관찰. 크기·색·수명 튜닝은 `game/vfx/ParticleEffects.h`.

## 알려진 한계

- **루트 모션**: walk/run/jump 클립의 루트 본 트랙에 이동이 구워져 있어, 캐릭터가 제자리에서
  살짝 미끄러졌다가 루프 시작에 돌아온다. 실제 이동은 `Simulation` 이 별도로 하므로 시각적
  겹침이 있다. 루트 모션 분리(첫 프레임 기준 상대화 또는 루트 트랙 무시)는 미구현 —
  [animation-design.md](animation-design.md) §5.
- **CPU 스키닝 단위**: 스켈레톤 bind/inverseBind·클립 키의 이동 성분은 이제 메시 정점과
  같은 `ImportOptions::scale`(0.01) 로 맞춰진다(`ModelImporter` `BuildSkeleton`/`FillInverseBind`/
  `BakeClip` + `LoadAnimationClipsFromFile(scale)`). 이걸 안 맞추면 애니메이션 포즈가 메시에서
  수십 배로 튕겨나간다 — 새 캐릭터 붙일 때 `LoadAnimationClipsFromFile` 의 `scale` 인자를
  모델 로드 scale 과 반드시 같게.
- **강체 부착 서브메시**: Unity-chan 의 얼굴·눈·입(`MTH_DEF`/`EYE_DEF`/…)은 스킨 웨이트가
  없고 head 본에 노드로 매달려 있다. `ModelImporter` 가 `ModelMesh::attachBone`(가장 가까운
  조상 본)을 기록하고 `ModelMeshPass3D` 가 그 서브메시를 웨이트 없이 그 본 하나로 스킨한다
  (`SubMesh::rigidBone`). 부착 본은 다른 스킨 메시가 써서 `inverseBind` 가 채워져 있어야 정확하다
  (Unity-chan head 는 `skin` 메시가 씀). 이 서브메시들의 크리즈 라인은 바인드 포즈에 남는다(기존 한계).
- **클립 선두 프레임 트림**: Unity-chan 클립 take 는 `time_begin` 이 실제 모션보다 한 프레임
  앞이라 프레임 0 이 바인드(T)포즈다 — 루프마다·상태 전환마다 1프레임 T포즈가 튄다.
  `BakeClip` 이 "모든 본의 첫 키가 바인드 포즈와 일치할 때만" 선두 키를 버린다(`Skeleton` 인자로 비교).
- ~~크로스페이드 없음~~: 상태 전환에 0.15s 크로스페이드가 들어갔다(`CharacterAnimationState`
  + `AnimationSampler::EvaluateBlended`, 로컬 TRS lerp). 점프는 파라메트릭 구동. [roadmap.md](roadmap.md) §2.1.
- **캐릭터 콜라이더 없음**: 박스/바닥과 물리 상호작용 안 함. 이동 범위는 `Actor.halfRange`
  (씬 1 = `kCharHalfRange`, 씬 2 = `kPlateauHalf`)로 원점 대칭 클램프만, 바닥은 `Actor.groundY`.
  (충돌 모듈은 탐지 전용 — [collider-design.md](collider-design.md).)
- 잠금 중에는 커서가 안 보여 `SETTINGS` HUD 버튼을 클릭할 수 없다. `Esc` 로 설정 열기.
