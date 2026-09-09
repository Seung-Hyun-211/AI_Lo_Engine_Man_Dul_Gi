# 엔티티 식별·생존주기 (Entity Lifecycle) — 뼈대

"엔티티"라는 이름의 큰 타입을 만들지 않는다. 대신 **식별(누가 살아있나)** 과 **저장(데이터를 어떻게 두나)** 을 분리해서, 저장 방식은 시스템마다 고르게 한다. 지금은 뼈대만 — 실제 엔티티 종류(캐릭터/아이템 등)는 `docs/synopsis.md`(장르 미정)가 정해진 뒤 추가한다. `Simulation`에는 아직 연결 안 했다(쓸 데가 없는데 미리 연결하면 죽은 코드).

관련 코드: `src/core/EntityId.h`(값 타입), `src/core/EntityRegistry.h/.cpp`(생존주기), `tools/entity_memory_bench.cpp`(§5 벤치마크, 엔진 빌드 밖). 관련 문서: `docs/command-playbook.md` #4(엔티티 모델 판단 항목), `docs/collider-design.md`(같은 "메인 스레드 전용" 규칙 선례).

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

### B. 컴포넌트 테이블 (`EntityId` 필요할 때) — sparse set

```cpp
template <typename T>
class DenseComponentTable
{
public:
    void Set(core::EntityId id, T value)
    {
        EnsureSparseCapacity(id.index);
        if (const std::size_t* slot = FindSlot(id)) { m_dense[*slot].value = std::move(value); return; }
        m_sparse[id.index] = m_dense.size();
        m_dense.push_back(Row{ id, std::move(value) });
    }
    [[nodiscard]] T* Find(core::EntityId id)
    {
        const std::size_t* slot = FindSlot(id);
        return slot ? &m_dense[*slot].value : nullptr;
    }
    // 매 스텝 한 번, 죽은 id의 행을 청소 - 알림을 받는 게 아니라 스스로 확인한다
    // (레지스트리를 옵저버 목록으로 무겁게 만들지 않기 위해, ISP). swap-and-pop이라
    // m_dense에 구멍이 안 남는다 - §4의 "인접 유지" 규칙이 여기서 실제로 지켜짐.
    void PruneDead(const core::EntityRegistry& registry)
    {
        for (std::size_t i = 0; i < m_dense.size(); )
        {
            if (registry.IsAlive(m_dense[i].id)) { ++i; continue; }
            m_sparse[m_dense[i].id.index] = kNone;
            m_dense[i] = std::move(m_dense.back());
            m_dense.pop_back();
            if (i < m_dense.size()) m_sparse[m_dense[i].id.index] = i;   // 옮겨온 행의 새 위치
        }
    }
    // 실제 순회(핫 패스)는 m_dense를 처음부터 끝까지 - 포인터도 해시도 없다.
    // 진짜 구현 땐 여기에 begin()/end() 나 std::span<T> 접근자를 둔다.
private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    struct Row { core::EntityId id; T value; };

    void EnsureSparseCapacity(std::uint32_t index) { if (index >= m_sparse.size()) m_sparse.resize(index + 1, kNone); }
    [[nodiscard]] const std::size_t* FindSlot(core::EntityId id) const
    {
        if (id.index >= m_sparse.size()) return nullptr;
        const std::size_t slot = m_sparse[id.index];
        if (slot == kNone || slot >= m_dense.size()) return nullptr;
        // 풀 EntityId 비교(세대 포함) - 재사용된 슬롯이 옛 데이터를 새 엔티티에게
        // 잘못 물려주는 걸 막는 안전망(PruneDead가 매 스텝 청소해도 한 번 더 확인).
        return m_dense[slot].id == id ? &m_sparse[id.index] : nullptr;
    }

    std::vector<Row> m_dense;             // 컴포넌트 데이터 - 인접 배치, 이게 핵심(§4)
    std::vector<std::size_t> m_sparse;    // id.index → m_dense 위치, 또는 kNone. 룩업 전용, 순회 안 함
};
```

`unordered_map<EntityId, T>` 한 장짜리(노드 기반, 값이 힙 여기저기 흩어짐)보다 이쪽을 기본으로 삼는다 — 이유와 실측은 §4·§5. 인스턴스가 몇 개 안 되고 매 프레임 순회하지 않는 컴포넌트라면 `unordered_map` 버전도 여전히 틀린 선택은 아니다(단순함이 이길 때도 있다) — 다만 `JobSystem::ParallelFor`로 돌릴 대상이면 항상 이 sparse set 쪽.

캐릭터처럼 시스템별로 컴포넌트가 있다/없다가 갈리는 대상에 맞다. 레지스트리는 "청소해라"라고 알려주지 않는다 — 각 테이블이 자기 스케줄(예: 매 스텝 끝)에 `IsAlive`로 직접 걸러낸다. 그래서 `EntityRegistry`가 구독자 목록을 관리할 필요가 없다(SRP 유지).

같은 게임 안에서 A와 B가 공존한다 — 파티클은 그대로 배열로, 나중에 생길 캐릭터만 `EntityId`+테이블로.

## 4. 메모리 할당 규칙 — 같은 타입은 인접 메모리에

1. **같은 타입의 데이터는 항상 하나의 연속 컨테이너(`std::vector<T>`) 안에 산다.** 엔티티 하나하나를 개별 `new`/`make_unique`로 힙에 흩뿌리지 않는다 — §3A든 §3B든 실제 데이터는 `vector`(§3B는 `m_dense`) 안에서만 산다.
2. **컴포넌트 테이블은 노드 기반 대신 sparse set(§3B)으로.** `sparse`는 룩업 전용(id→인덱스)이라 반복 경로에 안 낀다 — 실제로 순회하는 건 `dense` 하나뿐이고, 거기 담긴 `T`들이 서로 인접해 있다.
3. **제거는 항상 swap-and-pop.** 배열 중간에 구멍을 내는 제거(예: `erase(begin()+i)`로 뒤를 당김)는 O(n)이기도 하고, 그 순간까지는 인접성이 깨진다. swap-and-pop은 O(1)이고 인접성이 항상 유지된다.
4. **상한을 아는 시스템은 시작 시 `reserve()`.** `vector` 성장(재할당+복사)은 예측 가능한 시점(스텝 경계)에서만 일어나야 한다는 점에서 불변 규칙 6(워커 안 `vector` 재할당 금지)과 같은 이유다 — `reserve`로 아예 없애버리면 제일 안전하다.
5. **`JobSystem::ParallelFor`로 병렬 순회할 대상은 반드시 위 1~4번을 지킨 dense 배열이어야 한다.** `ParallelFor`는 겹치지 않는 연속 `[begin,end)` 범위 분할을 전제로 하므로(불변 규칙 6), sparse map이나 포인터 목록은 애초에 그렇게 못 쪼갠다 — dense 배열만 이 계약을 만족한다.

## 5. 검증: 실제로 오버헤드가 줄어드나 — 벤치마크

주장만 하지 않고 쟀다. `tools/entity_memory_bench.cpp`(엔진 빌드에는 안 들어감, `tools/fbx_probe.cpp`와 같은 위상의 확인용 실행 파일) — Position+Velocity 24바이트 struct(`Particle`과 비슷한 크기) N개에 "물리 스텝"(속도만큼 위치 이동)을 40회 반복, 7회 측정해 중앙값을 낸다. 세 레이아웃을 같은 랜덤 시드로 만들어 **완전히 같은 계산**을 하게 하고, 끝에 체크섬을 찍어 셋 다 일치하는지 확인한다(다르면 레이아웃 문제가 아니라 버그).

- **A. dense**: `std::vector<Transform>` 하나.
- **B. scattered (fresh new)**: 엔티티마다 개별 `make_unique`, 할당 순서 그대로 순회 — "특별히 나쁘게 안 만든" 기본 케이스.
- **C. scattered (fragmented)**: B에 다른 할당/해제를 섞어 힙을 어지르고 포인터 순서를 섞음 — 여러 종류의 엔티티가 오래 생성/파괴되며 돌아간 실제 상황에 더 가깝다.

이 세션 컨테이너(Linux, Xeon, g++ 13 `-O2`)에서 실측:

| 레이아웃 | N=500,000 | N=2,000,000 |
|---|---|---|
| A: dense `vector<Transform>` | 24.15 ms (1.00x) | 105.42 ms (1.00x) |
| B: scattered, fresh `new` | 31.27 ms (1.29x) | 128.55 ms (1.22x) |
| C: scattered, fragmented | 137.90 ms (5.71x) | 1098.08 ms (10.42x) |

**결론**:
- "특별히 나쁘지 않게" 개별 힙 할당만 해도 20~30% 손해다 — 포인터 역참조 자체가 이미 비용이다.
- 실제로 오래 돌아간 프로그램과 비슷한 흩어짐(C)에서는 **5.7배~10.4배** — 그리고 이 배율이 N이 늘수록 더 나빠진다(500k→2M, 4배 늘 때 5.7x→10.4x). 캐시/TLB가 감당 못 하는 접근 패턴이라 엔티티 수가 늘수록 격차가 벌어지는 것 — 이 엔진이 실제로 다루는 규모(파티클 2만 개, 앞으로 더 늘 수 있는 군중 등)에서 무시할 수 없는 차이다.
- 재현: `g++ -O2 -std=c++20 tools/entity_memory_bench.cpp -o bench && ./bench <N>` (또는 `tools/build_entity_memory_bench.bat`). 절대 수치는 하드웨어·컴파일러마다 다르지만(이 결과는 이 세션 컨테이너 기준), "인접 배치가 이기고 격차가 N에 따라 벌어진다"는 정성적 결론은 캐시 계층 구조가 있는 모든 하드웨어에서 재현되는, 잘 알려진 현상이다.
- 측정 안 한 것(다음에 필요하면): 삽입/삭제 자체의 비용(이번엔 순회만 쟀다), 멀티스레드 순회(여러 코어가 동시에 흩어진 데이터를 읽으면 캐시 경합이 더 나빠질 가능성이 높음 — `JobSystem::ParallelFor`가 실제 이 데이터를 돌릴 때는 오히려 더 유리할 수 있다는 뜻).

## 6. 사용 방법 (How to use)

### 새 엔티티 종류를 컴포넌트 테이블로 추가하기

```cpp
// Simulation이 소유(메인/시뮬 스레드) - 구조는 위 §3B DenseComponentTable 그대로
core::EntityRegistry m_entities;
DenseComponentTable<int> m_health;   // 필요한 컴포넌트마다 이런 테이블 하나

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

§3B의 `DenseComponentTable` 모양을 그대로 복사(또는 인스턴스화)한다 — 매 프레임 순회하지 않는 소수 인스턴스면 `unordered_map<EntityId, T>`로 단순하게 가도 된다. `PruneDead`만 매 스텝 끝에서 불러주면 된다.

### 하지 말 것

- `JobSystem` 워커 안에서 `Create`/`Destroy`/`Flush` 호출 금지(§2, CLAUDE.md 불변 규칙 6).
- `EntityRegistry`에 컴포넌트 데이터를 넣지 않는다 — 식별만. 데이터는 항상 그 시스템 자신의 테이블에.
- `ParallelFor` 콜백 중간에 `Flush()`를 부르지 않는다 — 스텝의 모든 시스템이 끝난 뒤 한 번만.
- 모든 것에 `EntityId`를 억지로 붙이지 않는다 — 조합·개별 참조가 필요 없는 동질적 대상(지금 `Particle`)은 그냥 배열로 둔다(§3A).
- 매 프레임 순회할 컴포넌트를 `unordered_map`/개별 `new`로 흩어두지 않는다 — §4의 인접 배치 규칙, §5가 그 대가를 실측한 것.
- `Simulation`에 지금 당장 연결하지 않는다 — 실제 쓰는 엔티티 종류가 생기기 전까지는 뼈대만(YAGNI). `docs/synopsis.md` 장르가 정해지고 첫 엔티티 타입이 나올 때 §6 패턴대로 연결한다.
