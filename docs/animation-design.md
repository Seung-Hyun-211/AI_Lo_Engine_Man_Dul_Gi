# 애니메이션 설계 (Animation Design) — 2D/3D 통합 지도, 설계만

이 문서 자체는 지도/설계 문서다(2D 프레임 애니메이션·`anim::core`·Live2D/Spine 연구 항목은 여전히 미구현). 목적: 애니메이션을 2D/3D로 명확히 분리하고, 바로 설계 가능한 항목과 별도 조사가 먼저 필요한 항목(Live2D/Spine류)을 구분한다. 3D 스켈레탈 쪽은 실제 구현이 들어갔다 — Unity-chan 클립 재생(CPU 스키닝 + 조건부 라운드로빈)은 `docs/model-animation-research.md` §5.2a/§5.3a 참고, 아래 §1 표에 요약.

관련 기존 문서: `docs/model-animation-research.md`(3D 스켈레탈 — 상세 설계 완료, 이 문서는 요약만 하고 중복 작성하지 않음), `docs/collider-design.md`·`docs/engine-overview.md`(2D/3D 분리 선례).

---

## 0. 범위와 원칙

- **불변 규칙 7 적용**: 애니메이션도 2D/3D를 분리한다 — `anim::a2d`(2D) / `anim::a3d`(3D). 서로 `#include` 금지.
- 현재 `src/anim/AnimationSampler.*`는 `math/Math3D.h`(`Mat4`)에 의존하는 **3D 전용** 코드다. 즉 사실상 이미 `a3d`다. 이름/디렉터리 정리(`anim/AnimationSampler.*` → `anim/a3d/AnimationSampler.*`)는 이 설계를 실제로 쓰기 시작할 때(2D 쪽이 생겨서 이름 충돌·혼동이 실제 문제가 될 때) 별도 커밋으로 — 지금 코드가 도는데 이름만 바꾸는 리팩터는 하지 않는다(YAGNI).
- 공유는 **제네릭 core**(`anim::core`)로만 한다. `core`에는 `Mat4`/`Quat`/uv rect 같은 도메인 타입을 넣지 않는다(불변 규칙 7의 "공유는 각 계층의 core로만" 원칙을 애니메이션에도 적용).
- 시간 전진은 항상 고정 스텝(`Simulation::Step`)에서만 — `docs/time-design.md` 규칙 재사용. 렌더/잡 스레드에서 애니메이터 `Tick` 금지.

```
anim/
  core/   ── 제네릭 재생 상태 + 상태 머신 템플릿 (도메인 타입 없음)
  a2d/    ── 스프라이트 프레임 애니메이션 (신규 설계, §2)
  a3d/    ── 스켈레탈 애니메이션 (기존 AnimationSampler, 상세는 model-animation-research.md)
```

---

## 1. 3D 애니메이션 (스켈레탈) — 이미 설계됨, 요약만

전체 설계·구현 상태는 `docs/model-animation-research.md` §4~7 참조. 여기선 이 문서의 지도 안에서 위치만 표시한다.

| 구성요소 | 상태 |
|---|---|
| `anim::AnimationSampler`(CPU 포즈 평가, LBS 준비) | ✅ 구현됨 |
| Unity-chan 클립 재생 (CPU 스키닝 + 조건부 라운드로빈) | ✅ 구현됨 — `model-animation-research.md` §5.2a/§5.3a. `SkinnedMeshPass3D`(§5.2 원안, GPU 스킨 새 패스)가 아니라 기존 `ModelMeshPass3D` 를 CPU 스킨으로 확장한 형태 |
| `SkinnedMeshPass3D`(GPU 스키닝 전용 새 패스, §5.2 원안) | ❌ 설계만 — 인스턴스 여럿을 각자 다른 애니메이션으로 세울 때 필요 |
| 크로스페이드/애디티브/본 마스크/루트 모션 | ❌ 설계만 |
| 텍스처 베이킹 애니메이션(본 행렬 텍스처 / VAT, 군중용) | ❌ 연구·설계만 — `model-animation-research.md` §5.5 |

이 문서가 추가하는 것은 하나: 상태 머신을 만들 때 **3D 전용으로 새로 짜지 말고** §3의 제네릭 `anim::core::AnimatorController<TClip>`을 3D 클립 타입으로 인스턴스화해서 쓴다(2D와 구조를 맞춰 나중에 로직 두 벌을 유지하지 않도록).

---

## 2. 2D 애니메이션 (스프라이트 프레임) — 신규 설계

### 전제 조건

현재 2D 렌더는 단색 `Quad`만 있고(`render/r2d/Sprite2D.h`), 텍스처 `SpriteDraw`(atlas id + uv rect)는 아직 없다(커맨드 플레이북 #3, 로드맵 2/3단계). **이 설계는 그 위에 얹힌다** — `SpriteDraw`가 없는 상태에서 프레임 애니메이션은 그릴 대상이 없다. 순서는 §5 참조.

### 데이터

```cpp
namespace engine::anim::a2d
{
    struct Frame
    {
        render::UvRect uvRect;       // SpriteDraw 쪽 타입 재사용 (atlas 내 좌표)
        float durationSeconds{};
    };

    struct SpriteAnimationClip
    {
        render::AtlasId atlas;
        std::vector<Frame> frames;
        bool loop{ true };
    };
}
```

`SpriteAnimationClip`은 값 타입. 애셋 로드 시 한 번 만들어 게임 코드가 보관(3D의 `import::AnimationClip`과 대칭 — FBX bake 결과를 들고 있는 것과 같은 자리).

### 재생 상태

```cpp
namespace engine::anim::a2d
{
    class SpriteAnimator
    {
    public:
        void Play(const SpriteAnimationClip& clip, float speed = 1.0f);
        void Tick(float fixedDeltaSeconds);   // Simulation::Step 안에서만
        [[nodiscard]] const Frame& CurrentFrame() const;

    private:
        const SpriteAnimationClip* m_clip{};
        float m_elapsed{};
        float m_speed{ 1.0f };
    };
}
```

- 프레임 인덱스는 `m_elapsed`를 누적 duration과 비교해 찾는다(3D처럼 보간하지 않음 — 프레임 애니메이션은 스텝 함수, lerp 없음. 이게 2D와 3D 샘플링의 본질적 차이이자 `core`를 얇게 두는 이유).
- `SnapshotBuilder`가 `animator.CurrentFrame().uvRect`를 그 프레임의 `SpriteDraw.uvRect`로 값 복사.

### 상태 머신

3D와 동일하게 §3의 `anim::core::AnimatorController<SpriteAnimationClip>`을 그대로 쓴다(idle/walk/attack 전이 + 크로스페이드는 2D에선 대개 불필요하지만, 클립 전환에 1~2프레임 유예를 주고 싶으면 같은 인터페이스로 표현 가능).

### 2D 스켈레탈(본 기반 2D 변형)은 여기 포함 안 함

"2D 애니메이션"이라고 해서 Live2D/Spine 같은 메시 변형·2D 본 애니메이션까지 이 절에 넣지 않는다. 그건 조사가 먼저 필요하다 — §4.

---

## 3. 공유 코어: `anim::core`

2D 프레임 애니메이션과 3D 스켈레탈은 "재생 상태 + 클립 전환 규칙"이라는 모양은 같고 "포즈를 어떻게 계산하는가"만 다르다. 그 공통부만 제네릭으로 뺀다.

```cpp
namespace engine::anim::core
{
    struct PlaybackState
    {
        float time{};
        float speed{ 1.0f };
        bool loop{ true };
    };

    // TClip: a2d::SpriteAnimationClip 또는 3D 클립 핸들. core는 내용을 모른다.
    template <typename TClip>
    class AnimatorController
    {
    public:
        using ConditionFn = std::function<bool(const void* params)>;

        void AddState(std::string name, TClip clip);
        void AddTransition(std::string from, std::string to, ConditionFn cond, float crossfadeSeconds);
        void Tick(float fixedDeltaSeconds, const void* params);
        [[nodiscard]] const std::string& CurrentState() const;
        [[nodiscard]] float CrossfadeWeight() const;   // 0=from, 1=to
    };
}
```

- `AnimatorController`는 **어떤 포즈를 어떻게 블렌딩할지 모른다** — 그건 `a2d`/`a3d` 각자가 `CurrentState()`+`CrossfadeWeight()`를 받아 자기 도메인 방식으로 평가한다(2D는 대개 크로스페이드 없이 즉시 전환, 3D는 `AnimationSampler`에 블렌딩 추가).
- `params`를 `void*`로 둔 건 core가 게임 파라미터 구조체를 몰라야 하기 때문(ISP/DIP) — 실제 구현 시엔 `void*` 대신 얇은 타입 소거 인터페이스나 `std::any`로 다듬을 여지 있음. 지금은 설계 스케치.
- **이 클래스에 `Mat4`, `Quat`, uv rect가 들어가면 설계 실패** — 그 순간 `core`가 `a3d`나 `a2d`에 의존하게 된다.

---

## 4. 연구가 필요한 항목 (Research needed) — 설계 전에 별도 조사 문서부터

아래는 지금 설계를 확정하지 않는다. `model-animation-research.md`가 스켈레탈 3D 구현 전에 라이브러리 비교부터 했던 것처럼, 각각 별도 조사 문서(`docs/2d-mesh-animation-research.md` 가칭)가 먼저 필요하다. 이 절은 "무엇을 조사해야 하는가"만 정리한다.

### 4.1 Live2D 스타일 (메시 디포메이션 기반 2D)

- **무엇**: 레이어드 일러스트를 메시로 나눠 변형 파라미터(눈 깜빡임, 입 모양 등)로 표정·움직임을 만드는 방식. Live2D Cubism이 사실상 업계 표준.
- **조사할 것**:
  - Cubism SDK 라이선스(비상용 무료, 상용은 매출 구간별 계약) — 이 엔진의 "서드파티는 `src/vendor/`에 소스 vendor" 정책(`ufbx`처럼 MIT/PD)과 충돌 가능성이 큼. Cubism SDK는 소스 공개형이 아니라 바이너리/제한적 소스 배포.
  - 파일 포맷(`.moc3`)이 비공개 바이너리 — 리버스 엔지니어링 없이 직접 파서를 구현하는 건 사실상 불가능에 가까움.
  - 결론은 조사 전 예단 금지하되, 가설: **자체 구현보다 공식 SDK 채택 여부(라이선스 수용 가능한지)가 먼저 결정돼야** 나머지 설계(SDK를 vendor에 어떻게 격리할지 등)가 의미 있음.

### 4.2 Spine 스타일 (2D 스켈레탈 + 메시 attachment)

- **무엇**: 2D 본 계층 + 메시 attachment(FFD, free-form deformation) + IK. 스켈레톤을 텍스처 조각에 입혀 애니메이션.
- **조사할 것**:
  - `spine-cpp` 런타임 라이선스(런타임 코드 자체는 공개돼 있으나 "Spine Editor로 만든 데이터를 재생하려면 Spine 라이선스 필요" 조건이 있음 — 버전별/용도별 재확인 필요) 및 상용 배포 조건.
  - 데이터 포맷(`.skel` 바이너리 / `.json`) 파서를 직접 짤지 `spine-cpp`를 vendor 할지.
  - `spine-cpp`가 `ufbx` 수준(단일/소수 파일, 외부 의존 0)으로 vendor 가능한 규모인지 — 아니라면 이 엔진의 "단일 vcxproj 유지" 정책과의 절충안 필요.
  - 2D 전용 스키닝이 현재 3D LBS(`docs/model-animation-research.md` §2)와 알고리즘은 같음(가중 평균 변환) — 하지만 불변 규칙 7 때문에 코드 재사용은 안 되고, 필요하면 그 수식만 문서로 공유하고 구현은 `a2d`/`a3d` 각자 둔다.

### 4.3 대안 — DragonBones / 자체 2D 스켈레탈

- **무엇**: 오픈소스 2D 스켈레탈 포맷(DragonBones, BSD 계열)이나, 이 엔진이 직접 최소 포맷을 정의하는 방안.
- **조사할 것**:
  - DragonBones 공식 C++ 런타임의 존재 여부·유지보수 활성도(현재 저조하다고 알려져 있음 — 직접 확인 필요).
  - 자체 포맷일 경우 **툴체인 부재**가 실질적 병목: 아티스트가 리깅할 에디터가 없으면 포맷을 설계해도 콘텐츠를 못 만든다. 조사 우선순위는 "엔진이 뭘 지원할 수 있는가"보다 "아티스트가 실제로 어떤 툴을 쓰는가"가 먼저.

### 4.4 조사 후 사용자가 정할 것 (Claude가 예단하지 않음)

- Live2D/Spine 같은 상용 SDK 라이선스 비용·조건을 감수할지, 아니면 §2의 스프라이트 프레임 애니메이션 + (필요하면) 자체 2D 스켈레탈로 자체 해결할지.
- 아트 파이프라인(실제로 Live2D/Spine 툴을 쓰는 외주·팀원이 있는지)이 조사 우선순위를 정한다 — 툴이 없으면 포맷 지원 자체가 무의미하다.

---

## 5. 로드맵 순서 제안

1. 텍스처 `SpriteDraw`(커맨드 플레이북 #3) — 이거 없으면 §2 전체가 그릴 대상이 없음. 선행 필수.
2. §2 스프라이트 프레임 애니메이션 — SpriteDraw 이후 바로 설계대로 구현 가능, 추가 조사 불필요.
3. §1 3D 스키닝(`SkinnedMeshPass3D`) — 설계는 이미 끝났음(`model-animation-research.md` §5), 구현만 남음.
4. §3 `anim::core::AnimatorController` — 2D·3D 중 하나가 실제로 상태 전환(idle/walk 등)이 필요해지는 시점에 도입. 둘 다 쓰기 전엔 만들지 않는다(추상을 먼저 만들지 않는다 — YAGNI).
5. §4 Live2D/Spine 등 — 조사 문서부터, 아트 파이프라인 결정 이후. 지금 착수 안 함.

---

## 6. 사용 방법 (How to use)

이 문서는 설계 문서라 아래는 **구현 시점의 API 초안**이다(현재는 아무것도 구현 안 됨 — CurrentState/existing symbol 아님).

### 2D 프레임 애니메이션 (구현 후)

```cpp
#include "anim/a2d/SpriteAnimator.h"

engine::anim::a2d::SpriteAnimationClip walkClip = LoadWalkClip(); // atlas + frame(uvRect, duration) 배열, 1회 로드
engine::anim::a2d::SpriteAnimator animator;
animator.Play(walkClip);

// Simulation::Step(fixedDelta) 안에서만
animator.Tick(fixedDelta);

// SnapshotBuilder 안에서
spriteDraw.uvRect = animator.CurrentFrame().uvRect;
```

### 3D 스켈레탈 애니메이션

`docs/model-animation-research.md` §7 그대로 사용 — 이 문서에서 중복 작성하지 않는다.

### `anim::core` 상태 머신 (구현 후, 제네릭)

```cpp
engine::anim::core::AnimatorController<ClipHandle> controller;
controller.AddState("Idle", idleClip);
controller.AddState("Walk", walkClip);
controller.AddTransition("Idle", "Walk",
    [](const void* p) { return static_cast<const LocomotionParams*>(p)->speed > 0.1f; },
    /*crossfadeSeconds=*/0.15f);

controller.Tick(fixedDelta, &locomotionParams);
// controller.CurrentState() + CrossfadeWeight() 를 a2d/a3d 각자 방식으로 평가
```

### 하지 말 것

- `anim::core`에 `Mat4`/`Quat`/uv rect 같은 도메인 타입을 넣지 않는다 — 제네릭 파라미터로만 받는다.
- `anim::a2d`와 `anim::a3d`를 서로 `#include` 하지 않는다(불변 규칙 7).
- 텍스처 `SpriteDraw` 없이 §2를 먼저 구현하지 않는다(그릴 대상 없음).
- §4(Live2D/Spine/DragonBones)를 조사 문서·라이선스 확인 없이 바로 vendor 하거나 구현하지 않는다.
- 렌더/잡 스레드에서 `SpriteAnimator::Tick`/`AnimationSampler::Evaluate`/`AnimatorController::Tick` 호출(메인/시뮬 스레드 전용, 결과는 값으로만 스냅샷에).
- 2D·3D가 쓸 것 같다고 지금 당장 `anim::core`부터 만들지 않는다 — 실사용처가 하나라도 생긴 뒤에(§5 순서).
