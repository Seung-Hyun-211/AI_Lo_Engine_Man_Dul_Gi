# 엔티티 식별·생존주기 (Entity Lifecycle) — 뼈대

"엔티티"라는 이름의 큰 타입을 만들지 않는다. 대신 **식별(누가 살아있나)** 과 **저장(데이터를 어떻게 두나)** 을 분리해서, 저장 방식은 시스템마다 고르게 한다. 지금은 뼈대만 — 실제 엔티티 종류(캐릭터/아이템 등)는 `docs/synopsis.md`(장르 미정)가 정해진 뒤 추가한다. `Simulation`에는 아직 연결 안 했다(쓸 데가 없는데 미리 연결하면 죽은 코드).

관련 코드: `src/core/EntityId.h`(값 타입), `src/core/EntityRegistry.h/.cpp`(생존주기). 관련 문서: `docs/command-playbook.md` #4(엔티티 모델 판단 항목), `docs/collider-design.md`(같은 "메인 스레드 전용" 규칙 선례).

---

## 1. 왜 이렇게 나눴나

기존 데모(`Particle`)처럼 동질적이고 수만 개 규모인 대상은 **AoS 구조체 배열**이 맞다 — `JobSystem::ParallelFor`로 연속 범위를 병렬 처리하기 좋고, 조합(옵션 컴포넌트)이 필요 없다. 반대로 캐릭터·아이템처럼 "이건 Health가 있고 저건 없고" 조합이 다양해지는 대상은 **컴포넌트 테이블**(EntityId → 인덱스)이 낫다.

두 방식을 억지로 통일하면 안 맞는 쪽이 손해를 본다(파티클에 컴포넌트 조회 오버헤드를 주거나, 캐릭터를 고정 필드 struct로 욱여넣거나). 그래서 공유하는 건 **`EntityId`(식별자)** 하나뿐이고, 저장은 시스템이 알아서 고른다.

## 2. `EntityId` / `EntityRegistry`

```cpp
struct EntityId { std::uint32_t index; std::uint32_t generation; };
```

인덱스 + 세대 카운터. `EntityRegistry`는 **식별 정보만** 갖는다(살아있나, 슬롯 재사용 시 세대 증가) — 컴포넌트 데이터는 전혀 안 들고 있다(SRP). 슬롯이 죽었다가 새 엔티티에게 재할당돼도 세대가 달라 옛 `EntityId`는 영원히 죽은 채로 남는다(`IsAlive`가 `false`) — 다른 엔티티를 가리키는 사고가 안 난다.

`Destroy(id)`는 `IsAlive`엔 즉시 반영되지만 슬롯 재사용은 `Flush()`까지 미룬다 — 이번 스텝에 그 id를 아직 처리 중인 다른 시스템이 "갑자기 사라짐"을 안 겪게 하기 위해서다. `Flush()`는 그 스텝의 모든 시스템이 끝난 뒤, 메인/시뮬 스레드에서 한 번만 부른다.

**스레드 규칙**: `CollisionWorld`와 동일하게 메인/시뮬 스레드 전용이다. `JobSystem` 워커 안에서 `Create`/`Destroy`/`Flush`를 부르지 않는다 — CLAUDE.md 불변 규칙 6에 이미 명시된 금지 항목이다.

## 3. 저장 전략 두 가지

### A. 단순 struct 배열 (지금 `Particle` 패턴, `EntityId` 없이도 됨)

```cpp
struct Particle { float x, y, vx, vy; };
std::vector<Particle> particles;   // 인덱스 자체가 식별자 - EntityId 불필요
```

동질적이고 개별 참조·조합이 필요 없으면 이대로 충분하다. `EntityId`를 안 쓰는 것도 정상적인 선택이다 — 모든 것에 억지로 식별자를 붙이지 않는다.

### B. 컴포넌트 테이블 (`EntityId` 필요할 때)

```cpp
class HealthTable
{
public:
    void Set(core::EntityId id, int hp) { m_rows[id] = hp; }
    [[nodiscard]] int* Find(core::EntityId id)
    {
        auto it = m_rows.find(id);
        return it != m_rows.end() ? &it->second : nullptr;
    }
    // 매 스텝 한 번, 죽은 id의 행을 청소 - 알림을 받는 게 아니라 스스로 확인한다
    // (레지스트리를 옵저버 목록으로 무겁게 만들지 않기 위해, ISP).
    void PruneDead(const core::EntityRegistry& registry)
    {
        std::erase_if(m_rows, [&](const auto& kv) { return !registry.IsAlive(kv.first); });
    }
private:
    std::unordered_map<core::EntityId, int> m_rows;   // EntityId.h 가 std::hash 특수화를 이미 제공
};
```

캐릭터처럼 시스템별로 컴포넌트가 있다/없다가 갈리는 대상에 맞다. 레지스트리는 "청소해라"라고 알려주지 않는다 — 각 테이블이 자기 스케줄(예: 매 스텝 끝)에 `IsAlive`로 직접 걸러낸다. 그래서 `EntityRegistry`가 구독자 목록을 관리할 필요가 없다(SRP 유지).

같은 게임 안에서 A와 B가 공존한다 — 파티클은 그대로 배열로, 나중에 생길 캐릭터만 `EntityId`+테이블로.

## 4. 사용 방법 (How to use)

### 새 엔티티 종류를 컴포넌트 테이블로 추가하기

```cpp
// Simulation이 소유(메인/시뮬 스레드) - 구조는 위 §3B 그대로
core::EntityRegistry m_entities;
HealthTable m_health;          // 필요한 컴포넌트마다 이런 테이블 하나

core::EntityId SpawnCharacter(int hp)
{
    core::EntityId id = m_entities.Create();
    m_health.Set(id, hp);
    return id;
}

void Simulation::Step(float fixedDelta, const PlayerIntent& intent)
{
    // ... 기존 시스템들 ...
    m_health.PruneDead(m_entities);   // 각 테이블이 스스로
    m_entities.Flush();               // 맨 마지막, 이 스텝의 모든 Destroy를 한 번에 반영
}
```

### 죽었는지 확인하고 쓰기

과거 스텝에서 받아 둔 `EntityId`를 쓰기 전엔 항상 `IsAlive`를 먼저 확인한다 — 재사용된 슬롯이라도 세대가 다르면 자동으로 `false`가 나오니 별도 무효화 로직이 필요 없다.

```cpp
if (m_entities.IsAlive(heldId)) { /* 안전하게 사용 */ }
```

### 새 컴포넌트 테이블 추가하기

§3B의 `HealthTable` 모양을 그대로 복사한다 — 저장은 `unordered_map<EntityId, T>` 또는 성능이 중요하면 dense 배열 + `EntityId→index` 맵(sparse set)으로. `PruneDead`만 매 스텝 끝에서 불러주면 된다.

### 하지 말 것

- `JobSystem` 워커 안에서 `Create`/`Destroy`/`Flush` 호출 금지(§2, CLAUDE.md 불변 규칙 6).
- `EntityRegistry`에 컴포넌트 데이터를 넣지 않는다 — 식별만. 데이터는 항상 그 시스템 자신의 테이블에.
- `ParallelFor` 콜백 중간에 `Flush()`를 부르지 않는다 — 스텝의 모든 시스템이 끝난 뒤 한 번만.
- 모든 것에 `EntityId`를 억지로 붙이지 않는다 — 조합·개별 참조가 필요 없는 동질적 대상(지금 `Particle`)은 그냥 배열로 둔다(§3A).
- `Simulation`에 지금 당장 연결하지 않는다 — 실제 쓰는 엔티티 종류가 생기기 전까지는 뼈대만(YAGNI). `docs/synopsis.md` 장르가 정해지고 첫 엔티티 타입이 나올 때 §4 패턴대로 연결한다.
