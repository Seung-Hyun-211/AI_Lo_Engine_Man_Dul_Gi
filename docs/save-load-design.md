# 세이브 / 로드 설계 (Save & Load) — 뼈대

인게임 진행 상태를 디스크에 쓰고 되읽는 구조. **상태: 설계만(미구현).**

기존 선례인 `core::Settings`(`settings.cfg`, `key=value`, `docs/game-settings.md`)와 **같은 계열의 파일 규약**을
쓰되, 저장 대상·수명·실패 정책이 달라 **별도 타입·별도 파일**로 둔다(§0).

관련: `docs/game-settings.md`(설정 저장 — 이 문서와 별개), `docs/entity-lifecycle-design.md`(`EntityId` 생존주기),
`docs/scrollable-list-and-pool.md`(`core::ObjectPool` generation 핸들 — §5의 핵심 제약), `docs/time-design.md`(고정 스텝
경계), `docs/scene-flow-design.md`(Title/InGame 전환 지점), `docs/loading-and-streaming.md`(전용 IO 스레드 선례).

---

## 0. 범위

| | 대상 |
|---|---|
| **이 문서** | 인게임 진행(플레이어 상태, 월드 상태, 경제/진행도). 슬롯 여러 개, 수동 저장 + 오토세이브 |
| **이 문서 아님** | 사용자 설정(해상도·볼륨·감도) — 이미 `core::Settings`로 구현돼 있고 기기별 설정이지 진행도가 아니다. **`Settings`에 진행 상태를 얹지 않는다** |

두 파일은 수명도 다르다: `settings.cfg`는 기기당 1개·언제나 덮어쓰기, 세이브는 슬롯당 1개·손상되면 **되돌릴 수 없는
손실**이다. 그래서 세이브에만 원자적 교체(§6)와 버전(§4)이 붙는다.

---

## 1. 무엇을 저장하고 무엇을 저장하지 않는가

세이브 크기와 마이그레이션 비용은 **저장 안 하는 것을 정하는 데서** 결정된다. 기본 규칙:
**"로드 직후 한 스텝 안에 재계산되는 것은 저장하지 않는다."**

| 저장한다 (권위 상태) | 저장하지 않는다 (파생·일시) |
|---|---|
| 플레이어 위치·방향·체력 | `RenderSnapshot` 전체(매 프레임 재생성) |
| 월드/맵 식별자 + 변경분(파괴된 것, 배치된 것) | `CollisionWorld*`의 콜라이더·`Contacts()` (로드 후 재등록·재계산) |
| 진행도(웨이브/일차, 자원, 해금) | VFX·파티클·기브·화염 조각 (일시적 연출) |
| 인벤토리·배치물 같은 플레이어가 만든 상태 | 애니메이션 재생 시간, 카메라 흔들림 등 표현 상태 |
| 게임 시각(경과 시간, 페이즈 남은 시간) | 고정 스텝 누적기(§3에서 스텝 경계에 저장하므로 항상 0) |
| RNG 시드 + 소비 횟수 *(RNG를 도입하면)* | 쿨다운처럼 재시작해도 무방한 짧은 타이머(정책에 따라 선택) |

> 지금 `Simulation`의 상태 대부분(크라우드 `SimAgent`, `GibPiece`, `FireChunk`, 파티클)은 **데모/연출 payload**라
> 저장 대상이 아니다. 실제 저장 대상은 장르가 정해진 뒤의 콘텐츠 상태다 — 그래서 이 문서는 **뼈대**만 고정하고
> 필드 목록은 §8의 절차로 늘린다.

---

## 2. 계층 — 4개 타입 (SRP 분해)

```
game::Simulation  ──Capture()──►  core::SaveData        (순수 값 구조체, 포맷·IO 모름)
game::GameState                        │
                                       ├──► core::SaveSerializer   SaveData ↔ 바이트 (포맷만)
                                       │
                                       └──► core::SaveStorage      바이트 ↔ 디스크 (원자적 교체, 슬롯 경로)
                                                  ▲
                                 game::SaveService ┘   조율 + 비동기 + 실패 보고
```

| 타입 | 책임 (하나) | 아닌 것 |
|---|---|---|
| `core::SaveData` | 저장 대상 상태를 담는 **값 구조체**. 복사 가능, 스레드 경계를 넘어감 | 파일 IO·포맷·게임 로직 |
| `core::SaveSerializer` | `SaveData` ↔ 바이트 변환 + **버전 태그/마이그레이션** | 디스크 접근 |
| `core::SaveStorage` | 슬롯 경로 결정, 원자적 쓰기(temp→rename), 읽기, 목록/삭제 | 내용 해석 |
| `game::SaveService` | "언제 저장할지"를 받아 캡처→직렬화→쓰기를 **비동기로** 태우고 결과를 UI에 보고 | 저장 대상 결정(그건 각 시스템) |

**DIP**: `SaveService`는 `SaveStorage`를 인터페이스로 받는다(로컬 디스크 / 나중에 클라우드·테스트 더블 교체).
`Simulation`은 `SaveData`만 알고 파일을 모른다. `main.cpp`만 구상 타입을 조립한다 — `IRenderer`와 같은 형태.

**YAGNI 경계**: v1에서 `ISaveParticipant { Capture/Restore }` 같은 **참여자 레지스트리를 만들지 않는다.**
저장 대상 시스템이 아직 `Simulation` 하나이므로 명시적 호출로 충분하다. 시스템이 **셋째로 늘어날 때** 승격한다
(`anim::core`를 실사용처 둘 전엔 만들지 않기로 한 것과 같은 판단).

---

## 3. 스레딩 계약 — `RenderSnapshot`과 같은 패턴

저장은 IO라 느리고(수~수십 ms), 프레임 루프에서 동기로 하면 눈에 띄는 멈춤이 된다. 그런데 게임 상태는
메인(시뮬) 스레드만 만진다. 해법은 이미 이 엔진이 렌더에 쓰는 방식 그대로다 — **값을 떠서 넘긴다.**

```
메인 스레드                                     IO 스레드 (SaveService 소유)
 Step(fixedDelta) ... 스텝 경계                  │
 SaveData data = Capture();      ← 여기서만      │
 service.RequestSave(slot, std::move(data)); ───►│ Serialize(data) → bytes
 (즉시 리턴, 프레임 계속)                         │ Storage::WriteAtomic(slot, bytes)
                                                 │ 결과를 원자적 상태로 게시
 service.PollResult()  ← 다음 프레임들에서 확인 ◄─┘
```

- **캡처는 반드시 스텝 경계**(`Step()` 밖, 프레임 루프)에서 한다. 스텝 중간이면 절반만 전진한 상태가 찍힌다.
- **`JobSystem` 워커 안에서 캡처 금지**(불변 규칙 6). 캡처는 메인 스레드 단일 호출.
- IO 스레드는 `SaveData` **복사본**만 본다 — 게임 객체 포인터를 넘기지 않는다(불변 규칙 3과 동일한 이유).
- **로드는 비대칭**: 디스크 읽기·역직렬화는 IO 스레드에서 해도 되지만, **`SaveData`를 월드에 적용(`Restore`)하는
  것은 메인 스레드에서**, 그리고 프레임 경계에서 한 번에. 로드 중에는 시뮬 스텝을 돌리지 않는다
  (로딩 커튼은 `loading-and-streaming.md`와 합류).
- 저장 중 두 번째 저장 요청이 오면 **무시하거나 큐 1칸**으로 덮어쓴다(렌더 스냅샷의 1슬롯 메일박스와 같은 발상).

---

## 4. 포맷과 버전

### 판단 A/B (권장: A)

- **A — 텍스트 `key=value` (권장)**: `Settings`의 파서 계열을 그대로 확장. 서드파티 0, `git diff`로 보이고,
  손으로 고쳐 디버깅할 수 있다. 반복 레코드는 접두사로 표현:
  ```
  version=1
  save.name=Slot A
  save.playtimeSeconds=3721.5
  player.x=120.5
  player.y=64.0
  player.health=87
  progress.wave=12
  progress.supply=340
  placed.count=2
  placed.0.kind=WireFence
  placed.0.x=96
  placed.0.y=48
  placed.1.kind=Mine
  ```
- **B — 바이너리**(매직 + 버전 + 고정 레이아웃): 빠르고 작지만 손으로 못 읽고 도구가 필요하다.

지금 규모(필드 수십~수백)에선 A의 단점이 드러나지 않는다. 그리고 §2가 `SaveSerializer`를 분리해 두었으므로
**나중에 B로 바꾸는 비용은 그 한 타입 교체**다 — 미리 B로 갈 이유가 없다.

### 손상·버전 정책 (`Settings`보다 엄격)

| 상황 | 처리 |
|---|---|
| 파일 없음 | "세이브 없음" — Title의 CONTINUE 비활성 |
| 필드 하나가 깨짐 | 그 필드만 기본값 + **경고 카운트**. `Settings`와 같은 관용 정책 |
| 필수 필드(`version`, 플레이어 위치) 없음 | **로드 실패**로 처리. 조용히 반쯤 복원된 월드를 만들지 않는다 |
| `version` > 빌드가 아는 버전 | **로드 거부**(구버전 빌드로 신버전 세이브를 열지 않음). 파일은 건드리지 않는다 |
| `version` < 현재 | `Migrate_v{n}_to_v{n+1}()` 사슬을 순서대로 적용. 각 단계는 작고 순수한 함수 |

**절대 원칙**: 실패한 로드가 **기존 파일을 덮어쓰지 않는다.** 실패는 읽기 단계에서 끝나고, 저장은 §6의
원자적 교체로만 일어난다.

---

## 5. 핸들은 저장하지 않는다 — 논리 레코드 + 로드 시 리맵

`core::ObjectPool<T>::Handle`과 `core::EntityId`는 **슬롯 인덱스 + generation**이다. generation은 그 슬롯이
재사용된 횟수라 **프로세스 수명에만 의미가 있다.** 이걸 그대로 저장하면, 로드 시 풀이 새로 만들어지면서
generation이 리셋돼 **되살아난 핸들이 엉뚱한 슬롯을 가리키거나 전부 죽은 것으로 읽힌다.**

```cpp
// 하지 말 것 - 슬롯/세대는 프로세스 로컬
out << "agent." << i << ".handleIndex=" << h.index << '\n';
out << "agent." << i << ".handleGeneration=" << h.generation << '\n';
```

**대신**: 저장은 **논리 레코드의 배열**로 하고, 로드에서 풀을 다시 채우며 **옛 인덱스 → 새 핸들** 리맵 표를 만든다.
레코드끼리의 참조는 그 표를 통해 복원한다.

```cpp
// 저장: 순서가 곧 논리 id
for (std::size_t n = 0; n < records.size(); ++n) { /* placed.n.* 기록 */ }

// 로드: 새로 Acquire 하면서 표를 만든다
std::vector<Pool::Handle> remap(records.size());
for (std::size_t n = 0; n < records.size(); ++n)
{
    const Pool::Handle h = pool.Acquire();
    pool.Slots()[h.index] = FromRecord(records[n]);
    remap[n] = h;                       // 옛 논리 id n -> 새 핸들
}
// 레코드가 서로를 가리켰다면 여기서 remap[old] 로 치환
```

같은 이유로 **콜라이더 id(`ColliderId`)도 저장하지 않는다** — 로드 후 `CollisionWorld*`에 재등록하며 새로 받는다
(`CollisionWorld2D::Clear()`가 `m_nextId`를 되감는 것도 이 결정을 강제한다. `docs/2d-engine-roadmap.md` §4-J).

---

## 6. 파일 배치와 원자적 저장

```
saves/
  slot0.sav        슬롯 본체
  slot0.sav.tmp    쓰는 중에만 존재 (성공 시 rename 으로 사라짐)
  slot0.meta       (선택) 목록 화면용 요약 - 이름·플레이타임·저장 시각
```

- **쓰기 순서**: `slot0.sav.tmp`에 전량 기록 → flush/close → **`slot0.sav`로 rename(덮어쓰기)**.
  rename은 (같은 볼륨에서) 원자적이라, 도중에 전원이 나가도 **이전 세이브가 온전히 남는다.** 바로 덮어쓰면
  "쓰다 만 세이브"가 원본을 대체해 복구 불가가 된다 — 세이브와 `settings.cfg`의 가장 큰 차이.
- `.meta`를 따로 두면 슬롯 목록 UI가 본체 전체를 파싱하지 않는다. 본체에 같은 값이 중복되지만,
  목록은 자주·본체는 드물게 읽히므로 그 편이 싸다. (v1에선 생략하고 본체 앞부분만 읽어도 된다.)
- 경로는 작업 디렉터리 기준(`settings.cfg`와 같은 규칙). `%APPDATA%` 이관은 배포 시점 결정.

---

## 7. 씬 흐름 연동 (`scene-flow-design.md`)

| 지점 | 동작 |
|---|---|
| Title 진입 | `Storage::List()`로 슬롯 존재 확인 → **CONTINUE** 버튼 활성/비활성 |
| CONTINUE / LOAD | 읽기·역직렬화(IO) → `EnterInGame()` → 메인 스레드에서 `Restore` → 첫 스텝 |
| ESC 메뉴 SAVE | 스텝 경계에서 `Capture` → `RequestSave` (프레임 안 멈춤) |
| 오토세이브 | 게임 고유의 **안전한 경계**에서만(예: 웨이브/일차 종료). 전투 한복판에 걸지 않는다 |
| 종료(QUIT) | 오토세이브 정책이 있다면 저장 **완료를 기다린 뒤** 창을 닫는다(IO 스레드 join) |

---

## 8. 사용 방법 (How to use)

> 아래는 **구현 시점의 API 초안**이다(현재 아무것도 구현돼 있지 않다).

### 저장 필드 추가하기 (가장 흔한 작업)

1. `core::SaveData`에 필드를 추가한다(값 타입만 — 포인터·핸들 금지, §5).
2. `SaveSerializer`의 쓰기/읽기 양쪽에 한 줄씩 추가한다. 읽기는 **없으면 기본값**으로 둔다 → 구버전 세이브가
   그대로 열린다(대부분의 필드 추가는 `version`을 올릴 필요조차 없다).
3. 그 값을 소유한 시스템의 `Capture`/`Restore`에 대입 한 줄씩.
4. **필드의 의미가 바뀌었을 때만** `version`을 올리고 `Migrate_vN_to_vN+1()`을 추가한다.

```cpp
// 1) SaveData
struct SaveData { /* ... */ int wave{ 1 }; int supply{ 0 }; };

// 2) Serializer (읽기 쪽 - 없으면 기본값 유지가 핵심)
else if (key == "progress.wave")   data.wave   = ParseIntOr(value, data.wave);
else if (key == "progress.supply") data.supply = ParseIntOr(value, data.supply);
```

### 저장·불러오기 호출

```cpp
// game::Application - 프레임 루프, 스텝 경계
void Application::SaveToSlot(int slot)
{
    core::SaveData data = m_simulation.Capture();   // 메인 스레드, Step() 밖
    data.playtimeSeconds = m_playtime;
    m_saveService.RequestSave(slot, std::move(data));   // 즉시 리턴
}

void Application::Update()
{
    if (const auto result = m_saveService.PollResult())   // 다음 프레임들에서 확인
        m_hud.ShowToast(result->ok ? "SAVED" : "SAVE FAILED");
}

// 로드: 적용은 반드시 메인 스레드에서
void Application::LoadFromSlot(int slot)
{
    if (auto data = m_saveService.LoadBlocking(slot))   // v1은 동기 + 로딩 커튼으로 충분
    {
        EnterInGame();
        m_simulation.Restore(*data);
    }
}
```

### 새 시스템을 저장 대상에 넣기

`SaveData`에 그 시스템의 슬라이스 구조체를 하나 추가하고, 시스템에 `Capture`/`Restore` 한 쌍을 둔다.
`SaveService`나 직렬화기에 그 시스템의 **게임 개념이 스며들지 않게** 한다(직렬화기는 필드만 안다).

### 하지 말 것

- `ObjectPool::Handle`·`EntityId`·`ColliderId`를 **그대로 저장하지 않는다** — 논리 레코드 + 리맵(§5).
- `Step()` **중간에** 캡처하지 않는다. 스텝 경계에서만(§3).
- `JobSystem` 워커나 렌더 스레드에서 캡처·복원하지 않는다(불변 규칙 3·6).
- 원본 파일에 **바로 쓰지 않는다** — temp + rename(§6).
- 로드 실패를 조용히 삼키고 **반쯤 복원된 월드로 진행하지 않는다**(§4).
- 진행 상태를 `core::Settings`/`settings.cfg`에 얹지 않는다(§0).
- 파생·연출 상태(VFX, 콜라이더, 스냅샷)를 저장하지 않는다(§1).
- 오토세이브를 매 프레임·매 스텝에 걸지 않는다 — 안전한 경계에서만(§7).

---

## 9. 구현 순서 (착수 시)

1. `core/SaveData.h` — 값 구조체 + `version` 상수. 필드는 최소(플레이어 위치·진행도 1~2개)로 시작.
2. `core/SaveStorage.{h,cpp}` — 슬롯 경로, `WriteAtomic`(temp→rename), `Read`, `List`, `Delete`.
   **원자적 교체를 여기서 먼저 맞춘다**(나중에 얹기 어려운 유일한 부분).
3. `core/SaveSerializer.{h,cpp}` — `key=value` 쓰기/읽기 + 버전 검사 + 필드별 관용 파싱(`Settings` 재사용 가능한
   파싱 헬퍼는 여기로 추출).
4. `game/Simulation`에 `Capture()`/`Restore()` 한 쌍 (값만 주고받음).
5. `game/SaveService.{h,cpp}` — 전용 IO 스레드 + 요청 1슬롯 + 결과 폴링(오디오 `MusicStream`의 스레드 수명 관리
   패턴을 그대로 따른다: 소멸자에서 stop+join).
6. UI 연결 — Title CONTINUE 활성화, ESC 메뉴 SAVE/LOAD, 토스트.
7. `docs/command-playbook.md`에 "명령 → 처리" 행 갱신, `docs/roadmap.md` P2 항목을 이 문서 링크로 축약.

1~4까지가 "뼈대"이고, 5는 저장이 눈에 띄게 끊길 때(수 ms 이상) 넣어도 늦지 않다 — v1을 동기로 두고
로딩 커튼으로 가려도 된다.

---

## 10. 판단 필요

1. **포맷 A(텍스트) / B(바이너리)** — §4. 권장 A.
2. **슬롯 개수** — 단일 슬롯 + 오토세이브 1개 vs 수동 N슬롯. UI 분량이 여기서 갈린다.
3. **오토세이브 경계** — 웨이브/일차 종료만 vs 주기적(N분). 주기적이면 "안전한 순간" 판정이 필요.
4. **저장 경로** — 작업 디렉터리(현재 `settings.cfg` 규칙) vs `%APPDATA%`. 배포 형태에 달렸다.
5. **RNG 도입 시 시드 저장 여부** — 지금 게임 코드에 RNG가 없으므로 미룰 수 있지만, 도입하는 순간
   "시드 + 소비 횟수"를 저장하지 않으면 로드 후 월드가 갈라진다.
