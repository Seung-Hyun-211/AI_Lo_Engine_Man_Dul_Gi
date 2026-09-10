# Collider / 충돌 설계 (Collision Design)

`src/physics/`. 충돌 **탐지**만 담당한다. 침투 해소·반발·마찰 같은 **응답(response)** 은 이 모듈 밖 (게임 코드 또는 이후 `Dynamics` 모듈).

2D와 3D는 별도 모듈이다. 서로 `#include` 하지 않는다. 공통 개념만 `physics/Collision.h`에 둔다.

```text
src/physics/
  Collision.h              공통: ColliderId, CollisionLayer, Contact, ContactList
  p2d/
    Collider2D.h           Collider2D (Box | Circle) + Overlaps()
    CollisionWorld2D.h/.cpp 콜라이더 보관 + 매 스텝 겹침 쌍 산출
  p3d/                      [ENGINE_WITH_3D 일 때만 빌드]
    Collider3D.h           Collider3D (Box | Sphere) + Overlaps()
    CollisionWorld3D.h/.cpp 3D 버전. 구조 동일
```

## 자료형

### 공통 (`Collision.h`)

```cpp
using ColliderId = std::uint32_t;                 // CollisionWorld가 발급
using CollisionLayer = std::uint32_t;             // 비트마스크

struct Contact {                                  // 이번 스텝에 겹친 한 쌍
    ColliderId a, b;                              // 항상 a < b
    std::uint64_t userA, userB;                   // 각 콜라이더의 user 값 (엔티티 id 등)
};
```

레이어 규칙: `A.mask & B.layer` 와 `B.mask & A.layer` 가 **둘 다** 0이 아니어야 검사한다. 플레이어/적/발사체/트리거 분리에 쓴다.

### 2D (`p2d/Collider2D.h`)

```cpp
struct Collider2D {
    enum class Shape : std::uint8_t { Box, Circle };
    Shape shape{ Shape::Box };
    math::Vec2 center{};
    math::Vec2 halfExtents{};   // Box
    float radius{};             // Circle
    CollisionLayer layer{ 1 };
    CollisionLayer mask{ 0xFFFFFFFF };
    std::uint64_t user{};       // 소유 엔티티 식별자
};

[[nodiscard]] bool Overlaps(const Collider2D& a, const Collider2D& b);
```

`Overlaps`: Box-Box(AABB), Circle-Circle(중심거리²), Box-Circle(클램프 후 거리²). 회전 없음 (OBB는 로드맵).

### 3D (`p3d/Collider3D.h`)

같은 모양. `math::Vec3`, `Shape::{ Box, Sphere }`. `Overlaps`: AABB3-AABB3, Sphere-Sphere, AABB3-Sphere.

## CollisionWorld

```cpp
class CollisionWorld2D {
public:
    ColliderId Add(const Collider2D& collider);   // id 발급
    void Update(ColliderId id, const Collider2D& collider);
    void Remove(ColliderId id);
    void Clear();

    void Step();                                   // 겹침 쌍 재계산
    [[nodiscard]] const std::vector<Contact>& Contacts() const;
    [[nodiscard]] bool AreTouching(std::uint64_t userA, std::uint64_t userB) const;
};
```

- 콜라이더는 연속 `std::vector<Collider2D>` 에 저장 (id → index 맵). JobSystem 친화적.
- **브로드페이즈: 현재 없음 (N²)**. 수백 개까진 충분. 병목이 측정되면 유니폼 그리드(2D) / loose octree·sweep-and-prune(3D) 추가. `Step()` 내부만 바뀌고 인터페이스는 불변.
- `Step()`은 결정적: 콜라이더 추가 순서대로 검사, `Contact.a < Contact.b`, 리스트는 (a,b) 정렬.

## 프레임 통합

`Simulation`이 소유하고 고정 스텝마다 구동한다 ([time-design.md](time-design.md) 규칙 2: 고정 `dt`).

```text
Simulation::Step(fixedDt, intent):
  1. 월드 상태 전진 (플레이어 이동, 파티클 등)
  2. m_collision2d.Clear() 후 이번 스텝의 콜라이더 Add   (또는 Update)
  3. m_collision2d.Step()                                 ← 겹침 쌍 산출
  4. Contacts() 읽어 게임 반응 (플래그·이벤트). 위치 보정은 여기서 게임이 직접 (원하면)
  #if ENGINE_WITH_3D : m_collision3d 도 동일
```

병렬화(로드맵): 쌍 검사는 겹치지 않는 인덱스 구간으로 `JobSystem::ParallelFor`, 워커별 `Contact` 버퍼에 모아 `Fence` 뒤 병합 ([multithreaded 문서](multithreaded_game_engine_architecture.md) 규칙 6). 지금은 동기.

## 스레드·불변 규칙

1. `CollisionWorld`는 시뮬레이션(메인 스레드)만 만진다. 렌더 스레드·스냅샷은 접근 금지.
2. 탐지만 한다. `Step()`은 콜라이더 위치를 **바꾸지 않는다**.
3. 콜라이더는 값. 스냅샷에 넣지 않는다 (렌더는 결과 색/디버그 Quad만 받는다).
4. 회전 콜라이더·연속 충돌(CCD)·물리 응답은 범위 밖. 별도 단계.

## 현재 구현 상태

- [x] 2D: Box/Circle, `Overlaps`, `CollisionWorld2D` (N²), 데모에서 플레이어-장애물 겹침 감지
- [x] 3D: Box/Sphere, `Overlaps`, `CollisionWorld3D` (N²), 데모에서 위성 큐브-중심 큐브 겹침 감지
- [x] **3D 레이캐스트** — `Ray3D` + `RayHit3D` + `RaycastCollider`(Ray-Box slab / Ray-Sphere) + `CollisionWorld3D::{RaycastClosest, RaycastAny, RaycastAll}` (선형 스캔). §"레이캐스트" 참고
- [ ] 브로드페이즈(레이캐스트도 이걸로 가속), 병렬 쌍 검사
- [ ] 2D 레이캐스트(`Ray2D` 대칭), 셰이프/스윕 캐스트, 트리거 enter/exit 이벤트 (현재는 매 스텝 "지금 겹침" 리스트뿐)
- [ ] 물리 응답 (별도 모듈)

## 레이캐스트

`p1` 에서 `v1` 방향으로 쏘아 부딪히는 콜라이더를 찾는다. **탐지만·메인 스레드만** (`Step()` 과 같은 규칙). 현재 선형 스캔(전 콜라이더 순회) — 브로드페이즈가 붙으면 레이 AABB 로 후보만 거른다.

```cpp
struct Ray3D {
    math::Vec3     origin;                 // p1
    math::Vec3     dir { 0,0,1 };          // v1, 단위 벡터(호출부 책임)
    float          maxDistance = FLT_MAX;  // 이 거리까지만  →  "일정 거리 안" 질의
    CollisionLayer mask = kAllLayers;      // 이 마스크에 layer 가 든 콜라이더만
    ColliderId     ignoreId = kInvalidCollider;   // 자기 자신 제외
};
struct RayHit3D { ColliderId id; std::uint64_t user; float distance; math::Vec3 point, normal; };
```

| 질의 | 반환 | 용도 |
|---|---|---|
| `RaycastClosest(ray)` | `std::optional<RayHit3D>` — **가장 가까운 1개** | 타워 타겟팅, 지면 검사, 카메라 벽 |
| `RaycastAny(ray)` | `bool` — 맞았나만 (가장 빠름) | 시야(LOS) 판정 |
| `RaycastAll(ray, out)` | `out` 에 **전부**, `distance` 오름차순 | 관통 투사체, 광역 스캔 |

"p1 에서 v1 방향, 일정 거리 안의 물체" = `maxDistance` 를 유한값으로 준 `RaycastAll`(또는 `RaycastClosest`). 별도 API 아님.

- 교차: **Ray-Box** = slab 법(`(min-o)/d`, `(max-o)/d` 의 `tmin/tmax`, `d` 성분 0 은 슬랩 안/밖만 검사). **Ray-Sphere** = 2차방정식. `t<0`(뒤) 또는 `t>maxDistance` 는 miss. 레이가 콜라이더 안에서 출발하면 `t=0`, `normal` 은 `-dir`.
- 필터: `LayersInteract` 대신 `ray.mask & collider.layer`(레이는 layer 가 없으니 단방향).
- **아직 `Simulation` 에 미연결** — `core::EntityRegistry` 처럼 뼈대만. 디펜스 게임의 타워 타겟팅/지면 검사에서 처음 쓰인다(`docs/roadmap.md` D1).

## 사용 방법 (How to use)

**콜라이더 붙이기 (2D)**

```cpp
// Simulation 멤버
physics::CollisionWorld2D m_collision2d;

// Step() 안에서 매 스텝
m_collision2d.Clear();
Collider2D player{};
player.shape = Collider2D::Shape::Box;
player.center = m_player + math::Vec2{ kPlayerSize * 0.5f, kPlayerSize * 0.5f };
player.halfExtents = { kPlayerSize * 0.5f, kPlayerSize * 0.5f };
player.user = kPlayerEntity;
m_collision2d.Add(player);
// ... 장애물들 Add ...
m_collision2d.Step();
for (const physics::Contact& c : m_collision2d.Contacts()) { /* 반응 */ }
```

**3D**: `#if ENGINE_WITH_3D` 안에서 `physics::CollisionWorld3D` + `Collider3D` 동일 패턴. `Shape::Sphere`는 `radius`, `Shape::Box`는 `halfExtents`.

**레이어로 거르기**: `collider.layer = LayerPlayer; collider.mask = LayerEnemy | LayerWorld;` — 플레이어는 적·월드만 검사, 다른 플레이어끼리는 무시.

**"닿았나?" 한 번만 물어보기**: `world.AreTouching(entityA, entityB)` (그 스텝 `Contacts()` 선형 검색).

**하지 말 것**: 렌더/잡 스레드에서 `CollisionWorld` 접근, `Step()` 후 콜라이더가 밀려났다고 가정(응답 없음), 스냅샷에 콜라이더 싣기, 프레임마다 `Add`한 id를 저장해두고 다음 프레임에 재사용(데모는 매 스텝 `Clear`+`Add`; 영속 id가 필요하면 `Update` 패턴으로).
