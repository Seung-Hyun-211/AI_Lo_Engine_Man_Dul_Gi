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
    EnterTitle  / OpenSettings    → m_window.SetPointerLocked(false)
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
  · BuildCamera  : focus = CharacterPosition + (0,1.3,0);  forward = f(camYaw,camPitch);  eye = focus - forward*3.6
  · BuildScene3D : model.world = RotationY(CharacterFacingYaw + kModelYawOffset) * Translation(CharacterPosition)
                   model.animClipIndex/Time = Simulation 의 값 (렌더 스레드가 §5.2a 로 CPU 스킨)
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
- **크로스페이드 없음**: 상태 전환이 즉시 스냅이다. 블렌딩은 설계만.
- **캐릭터 콜라이더 없음**: 박스/바닥과 물리 상호작용 안 함. 이동 범위는 `kCharHalfRange` 로 클램프만.
  (충돌 모듈은 탐지 전용 — [collider-design.md](collider-design.md).)
- 잠금 중에는 커서가 안 보여 `SETTINGS` HUD 버튼을 클릭할 수 없다. `Esc` 로 설정 열기.
