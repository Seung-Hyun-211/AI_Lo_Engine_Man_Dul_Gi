# 서큘러 밸런싱 환경 (CSV) — 몹 수 · 경험치 · 레벨

**상태: 구현 완료.** 숫자를 코드가 아니라 CSV 여덟 개(몹/레벨/스폰 곡선 + **M1: 플레이어 이동·능력치·캐릭터** + **M5: 무기·장신구**)로 뺐고, 게임 안에서 F5로 다시 읽고, 창 없이 결과를
뽑는 시뮬레이터가 있다. 게임 설계(기초 설계 기반: 무기·XP·레벨업·스폰 패턴 등)는 [circular-design.md](circular-design.md), 앞으로 늘릴 표는 이 문서 맨 아래 "확장 계획".

## 한눈에

```text
assets/data/circular/
  balance.csv       key,value,note            몹 HP·속도·크기·XP, 스폰 거리, 레벨표 밖 성장률
  levels.csv        level,xp_to_next,note     레벨별 필요 XP (1,2,3… 빈틈 없이)
  spawn_curve.csv   time_sec,spawns_per_sec,max_alive   런 시간별 초당 스폰 수 · 살아있는 몹 상한
  player.csv        key,value                 걷기/달리기/대쉬/스태미너 수치 (설계 §2.2) + 그림·히트박스 크기
  stats.csv         stat_id,base_value,min,max,from_vit,from_int,from_cor,from_agi   능력치 정의·기초 4스텟 계수 (설계 §2.6)
  characters.csv    id,name,main_stat,start_vit..start_agi,start_weapon   플레이어블 캐릭터 (설계 §2.3)
  weapons.csv       id,name,effect,cooldown,damage,range,...,path,...   무기 10종(기초 5 + 구형 2 + 움직임 3) (열 정의 아래)
  accessories.csv   name,stat,multiplicative,amount,max_level   장신구 6종 (설계 §3.3)

game/CircularBalance.{h,cpp}   CSV → CircularBalance (몹/레벨/스폰 곡선 평가기)
core/CsvFile.{h,cpp}           범용 CSV 리더 (BOM·CRLF·따옴표·# 주석)
tools/balance_sim.cpp          창 없는 시뮬레이터 (실제 Simulation 을 그대로 돌림)
tools/run_balance_sim.bat      빌드 + 실행 한 방
```

컴파일 타임 기본값은 `game/CircularConfig.h`(`kActiveMob`, `kProgression`)에 남아 있다 — CSV 가 없거나 깨져도
게임은 그 값으로 돈다. **`kActiveMob.capacity`(4096)만은 CSV 로 못 바꾼다**(`MobField` 슬롯 수라 시작 때
정해짐) — `max_alive` 가 그 위로 올라가면 잘라내고 경고한다.

## CSV 형식 규칙

- UTF-8(BOM 있어도 됨, Excel "CSV UTF-8"), LF/CRLF 둘 다. 소수점은 `.`.
- **"px" = 월드 단위**(1920×1080 화면에서의 1px). 화면에는 항상 월드 1920×1080 만큼이 보이고 창 크기에 맞춰 확대·축소되므로, 수치는 해상도와 무관하다([engine-conventions.md](engine-conventions.md) 2D).
- 빈 줄과 `#` 로 시작하는 줄은 무시. 첫 유효 줄이 헤더(대소문자 무시). 열 순서는 헤더 이름으로 찾으니 자유.
- `note` 같은 남는 열은 읽지 않는다 — 메모용. 쉼표가 든 셀은 `"따옴표"`로 감싼다.
- **잘못된 행은 그 행만 건너뛰고** 나머지는 적용한다. 파일이 없으면 그 파일 전체가 기본값.
  문제는 줄 번호와 함께 보고된다(아래 "문제 확인").

### balance.csv — key,value

| key | 뜻 | 제약 |
|---|---|---|
| `mob_health` | 새로 스폰되는 몹 HP | > 0 |
| `mob_speed` | 몹 속도 px/s (플레이어는 300) | ≥ 0 |
| `mob_radius` | 몹 충돌/표시 반경 px | > 0 |
| `mob_xp` | 킬당 XP (플레이어 XP 획득 배율 곱하기 전) | ≥ 0 |
| `spawn_radius` | 플레이어로부터 스폰 거리 px | > 0 |
| `xp_growth_after_table` | `levels.csv` 마지막 행 뒤로 레벨마다 곱해지는 배수 | ≥ 1 |

모르는 key 는 경고 후 무시.

### levels.csv — level,xp_to_next

`level` 은 1,2,3… 순서대로 빈틈 없이(어긋난 행은 오류로 건너뜀). `xp_to_next` = 그 레벨에서 다음으로 가는 데
필요한 XP, > 0. **표의 마지막 행 뒤로는 `xp_growth_after_table` 배씩 계속 늘어나므로** 손으로 튜닝할 구간만
적으면 된다. 기본 표는 옛 공식 `30 × 1.35^(n-1)` 을 20레벨까지 정수로 옮긴 것.

### spawn_curve.csv — time_sec,spawns_per_sec,max_alive

`time_sec` = 런 시작 후 초(엄격히 증가해야 함). 행 사이는 **선형 보간**, 마지막 행 뒤로는 마지막 값 유지.
`spawns_per_sec` = 초당 스폰 수(고정 스텝 1/60 초보다 커도 됨 — 스텝당 여러 마리). `max_alive` = 살아있는 몹이
이 수에 닿으면 스폰을 멈춘다(빈 만큼 몰아서 뱉지 않음). 기본은 `0,100,4096` 한 줄 = 초당 100마리 평탄.

> **범위**: 지금 이 곡선(과 `levels.csv`)은 **런 전체에 하나**다. 스테이지가 90초 단위로 생기면(M6, [circular-design.md](circular-design.md) §4.3, [circular-design.md](circular-design.md) §6.4) 스폰 곡선은 스테이지별 시계(0~90초)로 재구성되고 `stages.csv` 의 `timeline_id` 가 고른다. 레벨/XP 는 런 전체에 이어진다 [살].

램프 예(파일 안에 주석으로 들어 있음): `0,10,300` → `60,40,1000` → `180,100,2500` → `300,150,4096`.

## 사용 방법 (How to use)

### A. 게임 안에서 조율 (눈으로 보기)

1. 게임 실행 → 곧장 Circular (타이틀 없음. 씬에 들어올 때마다 CSV 를 새로 읽는다 — ESC → 설정 → LOBBY → GAME 으로 다시 들어오면 재로드).
2. 다른 창에서 CSV 편집·저장 → 게임에서 **F5** = 다시 읽기(런 유지: 새 스폰은 새 HP/크기, 속도·XP표·스폰 곡선은
   즉시), **F6** = 다시 읽고 런 처음부터.
3. 화면 왼쪽 아래 네 줄이 현재 값을 보여준다: ① `T 42  RATE 100  CAP 4096  XP 12 OF 41`(런 시계, 지금 적용 중인
   스폰 곡선 값, 현재 레벨 XP) ② 캐릭터 이름 + 기초 4스텟 ③ 파생 능력치(이동/공속/크기/피해/투사체/대쉬 소모)
   ④ `BALANCE OK` 또는 `BALANCE n ERR m WARN - SEE OUTPUT`. 왼쪽 위에는 스태미너 바.
4. 오류·경고 상세는 Visual Studio **Output 창**(`[balance] ...` 줄)에 나온다. F5 로 디버깅 중이 아니면 안 보이니,
   그땐 B 의 시뮬레이터로 확인.

### B. 시뮬레이터로 조율 (빠르게, 표로)

```bash
tools\run_balance_sim.bat --seconds 300 --interval 10
```

실제 `game::Simulation` 을 GPU/창 없이 실시간보다 훨씬 빨리 돌려 콘솔 표 + CSV 두 개를 낸다:

- `build/tools/balance_timeline.csv` — `interval` 초마다 한 행: 살아있는 몹 수, 누적 킬, 초당 킬, 레벨, XP,
  다음 레벨 필요 XP, 그 시각 스폰 곡선 값, 덱.
- `build/tools/balance_levels.csv` — **각 레벨에 도달한 시각**(`seconds_since_prev` 로 "레벨업 간격"이 바로 보임).
  XP 표를 잡을 때 이 열을 원하는 리듬(예: 처음엔 10~20초, 후반 40초+)에 맞춘다.
- 옵션: `--seconds N`(기본 600) `--interval S`(기본 10) `--out PREFIX`(기본 `build/tools/balance`).
  다른 값으로 여러 번 돌려 `--out build\tools\try1` 식으로 이름을 달리하면 Excel 에서 겹쳐 비교하기 좋다.
- CSV 에 오류가 있으면 실행 첫 줄들에 `[balance] ERROR …` 가 나오고 종료 코드 1.
- **한계**: 대역 플레이어는 가만히 서 있고 죽지 않으며(플레이어 HP 가 아직 없음), 레벨업마다 "첫 번째 무기(카드)
  옵션(없으면 첫 옵션)"을 고른다. 돌진 패턴은 돌아가지만 피해가 없다. 곡선의 **모양**을 잡는 도구지 실제
  난이도 판정 도구가 아니다 — 최종 감각은 A 로 직접 플레이.

### player.csv — key,value (M1)

| key | 기본 | 뜻 |
|---|---|---|
| `walk_speed` | 300 | 걷기 px/s (× `move_speed` 스탯) |
| `sprite_width` / `sprite_height` | 64 / 128 | 플레이어 그림 크기(월드 단위). 그림은 히트박스 위에 선다(아래 변 일치, 가로 가운데) |
| `hitbox_width` / `hitbox_height` | 48 / 48 | 플레이어 콜라이더 = 게임상 몸(이동 경계·충돌·공격 원점·몹이 쫓는 점) |
| `run_mul` / `run_cost_per_sec` | 1.6 / 15 | 달리기 배율 / 초당 스태미너 소모 |
| `run_resume_stamina` | 10 | 바닥나서 잠긴 달리기가 다시 풀리는 스태미너 |
| `dash_speed_mul` / `dash_duration` | 3.5 / 0.18 | 대쉬 속도 배율 / 지속(=무적) 시간 s |
| `dash_cost` / `dash_cooldown` | 30 / 0.5 | 대쉬 소모 / 대쉬 시작 간 최소 간격 s |
| `dash_chain_window` / `dash_chain_penalty` | 2.0 / 0.5 | 이 창(s) 안의 연속 대쉬마다 소모 +50% |
| `stamina_regen_per_sec` / `stamina_regen_delay` | 30 / 0.8 | 초당 회복 / 마지막 소모 후 회복 시작까지 s |

최대 스태미너는 여기 없고 `stats.csv` 의 `stamina_max`(민첩 계수 포함). **"달리기 ≪ 대쉬, 연타하면 손해" [확정]** 을 깨지 않게 조정할 것 — 기준: 스태미너당 이동 효율이 달리기(12.0) > 대쉬(4.5) > 연속 3번째(2.25) 순서([circular-design.md](circular-design.md) §2.2 표).

### stats.csv — 능력치 정의 (M1)

`stat_id` 만 필수, 나머지 열은 비우면 코드 기본값. `기초 = (base_value + 가산) × (1 + 배율)`,
`파생 = (base_value + Σ from_x × 기초 스텟 x + 가산) × (1 + 배율)`, 결과는 `min..max` 로 잘림. 가산·배율은 캐릭터
시작값(`characters.csv`)과 레벨업 스탯 카드에서 온다. `min > max` 행은 오류로 건너뜀, 모르는 `stat_id` 는 경고.
현재 소비되는 능력치: `attack_speed, weapon_damage, attack_size, extra_projectiles, move_speed, xp_gain, stamina_max,
stamina_regen, dash_cost_mul, dash_chain_penalty_mul, dash_cooldown_mul`. 나머지(`luck, max_hp, crit_*` …)는 값만 있고 시스템 대기.
새 능력치 = `Stats.h` 의 `StatId` + `kStatDefs` 한 줄씩 + 이 CSV 한 행.

### characters.csv — 플레이어블 캐릭터 (M1)

`id,name,main_stat,start_vit,start_int,start_cor,start_agi,start_weapon` 전부 필수. `main_stat` = `vit|int|cor|agi`,
`start_*` ≥ 0(주력에 가장 높게), `start_weapon` = `weapons.csv` 의 무기 `id`, `name` 은 ASCII(HUD
폰트에 한글 없음). 중복 id/모르는 무기/잘못된 값은 그 행만 오류로 건너뛰고, 유효 행이 하나도 없으면 내장 4종. 게임에서는 **F7**
로 캐릭터 선택 모달을 열어 확인한다(고르면 새 런). 능력치 결과는 화면 왼쪽 아래 두 번째·세 번째 줄에 나온다.

### weapons.csv — 무기 (M5 + 전투 구조 W6·W13)

**열 정의는 여기 한 곳**(구조 설명은 [circular-combat.md](circular-combat.md) §2). 필수: `id,name,effect,cooldown,damage,range,
base_targets,max_level,damage_per_level,range_per_level,cooldown_scale,levels_per_extra_target`. 나머지는 선택 — 빈 칸 = 기본값.

| 열 | 값 | 기본 | 뜻 |
|---|---|---|---|
| `id` | 소문자·숫자·밑줄 | (필수) | 식별자. `characters.csv` 의 `start_weapon` 과 이미지 이름(`wpn_<id>_icon`, `prj_<id>_NN`)이 이걸로 찾는다 |
| `name` | ASCII | (필수) | 레벨업·HUD 표시 이름(폰트 A-Z 0-9 : - . %) |
| `effect` | 아래 7개 | (필수) | 무엇을 어떤 도형으로 — `game/Card.h` `kEffectSpecs` 한 표가 `{형태, 도형, 명중 처리}` 로 푼다. 새 효과는 그 표에 먼저 |
| `cooldown` `damage` `range` | 초 / 피해 / px | (필수) | 레벨 1 값. `range` = 즉시 장판의 크기, BOLT 사거리, `straight` 경로의 이동 거리 |
| `base_targets` `levels_per_extra_target` | 정수 | (필수) | BOLT 타깃 수 / 관통 수(`piercingshot`, 사거리로 끝나는 경로만) |
| `max_level` `damage_per_level` `range_per_level` `cooldown_scale` | | (필수) | 레벨 곡선 |
| `cone_half_angle_deg` / `line_half_width` | 도 / px | 0 | `arcswing` 부채꼴 반각 / `lineswing` 반폭 |
| `projectile_speed` | px/s | 0 | `straight` 경로 속도 |
| `explode_radius` / `damage_max` | px / 피해 | 0 | `explodingbolt` 폭발 반경 / `randomdamageshot` 롤 상한 |
| `overflow_stat` / `overflow_value` | StatId / 값 | 없음 | 최대 레벨 후 오버플로우([circular-design.md](circular-design.md) §3.4). 비우면 제안 안 함 |
| `color` | `RRGGBB` | FFFFFF | 스프라이트 전 자리표시 색(외곽선·투사체) |
| `hit_radius` | px | 6 | 움직이는 공격 자신의 반경(투사체 크기, 움직이는 장판의 도형 크기). 그림 = 이 판정 × `visual_scale` |
| `visual_scale` | 배율 | 1 | 그림 배율. 1 이 아니면 "일부러 다르게" |
| `sprite` / `fx_hit` | 이미지 접두사 | 빈 칸 | 이동 중 프레임 / 명중 이펙트 (M7 — 지금은 도형) |
| `path` | `none` `straight` `polar` | 투사체 `straight`, 그 외 `none` | 시간에 따른 위치. `none` = 시전 지점에서 즉시 1회 |
| `anchor` | `cast` `player` | `cast` | `polar` 의 중심: 발사 순간 위치 / 매 스텝 플레이어 |
| `count` | ≥ 1 | 1 | `polar` 한 번에 만드는 수(360°/count 간격, `extra_projectiles` 가 더해짐) |
| `lifetime` | 초 | 0 | > 0 이면 시간으로 끝남(`polar` 는 필수). 끝나면 사라지고 다음 쿨다운에 다시 생김 |
| `start_radius` / `radial_speed` / `angular_speed_deg` | px / px·s⁻¹ / °·s⁻¹ | 0 | `polar` 매개변수. `radial_speed` 0 = 궤도, > 0 = 바깥으로 퍼지는 소용돌이. 반경에 `attack_size` 곱함 |
| `tick_interval` | 초 | 0 | 움직이는 **장판**(효과 형태 Area + 경로)의 판정 간격 — 필수 |
| `rehit_interval` | 초 | 0 | 시간으로 끝나는 **투사체**가 같은 몹을 다시 때리기까지(0 = 다시 안 때림) |

`effect` = `radialpulse`(원 장판) · `arcswing`(부채꼴) · `lineswing`(캡슐) · `nearestbolt`(가까운 N체) · `explodingbolt`(닿으면 폭발)
· `piercingshot`(관통) · `randomdamageshot`(랜덤 피해). 움직임은 `effect` 가 아니라 `path` 로 정한다 — 예: 원 장판 + `straight` =
럴커식 전진 가시(SPIKE), 관통 + `polar` = 궤도 칼날(BLADE)·소용돌이(VORTEX).

중복 id·모르는 effect/path/anchor·필수 열 파싱 실패, **실행할 수 없는 조합**(`nearestbolt` + 경로, `polar` 인데 `lifetime` 0,
움직이는 장판인데 `tick_interval` 0, `straight` 인데 속도·수명 둘 다 0)은 그 행만 줄 번호와 함께 오류로 건너뛴다. 유효 행이
하나도 없으면 내장 7종(`Card.h` `kCardDefs` — PULSE/BOLT + 기초 설계 5종, CSV 와 같은 숫자로 유지). BLADE/VORTEX/SPIKE 는
레벨업 풀 전용(D5) — 폴백에는 없다.

### accessories.csv — 장신구 (M5)

`name,stat,multiplicative,amount,max_level` 전부 필수. `stat` = 아무 `StatId`(= `stats.csv` 의 `stat_id`).
`multiplicative` = `0|1|true|false`. 레벨 N 을 소지하면 `amount × N` 이 그 능력치에 더해지거나(가산) 곱해진다(배율).
장신구는 효과 실행 코드가 없다 — 순수 스탯 보너스. 유효 행이 하나도 없으면 내장 6종(AMULET/CHARM/RING/BLOODSTONE/
GAUNTLET/BELT).

### 튜닝 레시피

| 하고 싶은 것 | 어디를 |
|---|---|
| 초반 레벨업이 너무 잦다/뜸하다 | `levels.csv` 앞 몇 행 (`balance_levels.csv` 의 `seconds_since_prev` 로 확인) |
| 후반 레벨업이 너무 빠르다 | `levels.csv` 뒷부분 또는 `xp_growth_after_table` |
| 시간에 따라 몹이 늘어나게 | `spawn_curve.csv` 에 행 추가(램프) |
| 화면이 몹으로 터진다 | `spawn_curve.csv` `max_alive` 낮추기 (또는 `spawns_per_sec`) |
| 몹이 너무 단단하다/무르다 | `balance.csv` `mob_health` (무기 데미지는 `weapons.csv`) |
| 킬 보상 자체를 키우기 | `balance.csv` `mob_xp` |
| 달리기/대쉬 감각, 스태미너 | `player.csv` (최대치는 `stats.csv` `stamina_max`) |
| 캐릭터 성격(주력 스텟 강조) | `characters.csv` `start_*`, 스텟 → 효과 계수는 `stats.csv` `from_*` |
| 무기가 강해지는 정도(공속/피해/크기/투사체) | `stats.csv` 계수, 레벨업 스탯 카드 크기는 `Card.h` `kStatCards` |

### 새 밸런스 항목 추가하기

- **스칼라 하나**: `CircularBalance` 에 필드 + `Defaults()` 에 기본값 + `LoadCircularBalance` 해당 파일 블록의 `KvEntry entries[]` 에 한 줄(새 `key,value` 파일이면 `LoadKeyValueFile` 호출 블록 하나)
  (key 이름·최소값). 쓰는 곳은 `m_balance.<필드>`.
- **표 하나(예: 웨이브별 몹 타입)**: `CircularBalance` 에 `std::vector<Row>` + 새 CSV + 로더에 블록 하나(`levels.csv`
  블록을 복제). `Simulation` 은 `m_balance` 만 읽는다.
- **CSV 를 더 늘려도** `Application::LogBalanceReport` / HUD 상태줄은 `BalanceLoadReport` 하나만 보므로 안 고쳐도 된다.

### 하지 말 것

- 밸런스 숫자를 `Simulation.cpp` / `CircularConfig.h` 에 다시 하드코딩하지 말 것 — `CircularConfig.h` 는 *기본값(폴백)*
  이지 튜닝 지점이 아니다. 튜닝은 CSV.
- 게임 스레드가 아닌 곳(렌더 스레드/워커 잡)에서 `ReloadBalance` 를 부르지 말 것 — 메인 스레드(`Application::OnKey`)만.
  `MobField::Step` 의 `ParallelFor` 는 `Step()` 안에서 끝나므로 그 사이엔 안 불린다.
- `max_alive` 를 `MobField` capacity(4096) 이상으로 쓰지 말 것 — 잘리고 경고만 난다. 더 필요하면
  `kActiveMob.capacity` 를 올리고 다시 빌드.
- 시뮬레이터 결과를 "밸런스 완료"의 근거로 삼지 말 것(위 한계).
- 시뮬레이터의 대역 플레이어는 이동/대쉬를 하지 않는다 — 이동·스태미너 수치(`player.csv`)는 게임에서 직접 확인.

## 설계 메모

- **왜 CSV**: 한 줄이 곧 한 행인 표(레벨표·스폰 곡선)라 Excel/시트로 편집·그래프하기 쉽고, 별도 툴/직렬화
  없이 텍스트 diff 가 된다. 스칼라도 같은 형식(`key,value`)으로 통일.
- **왜 씬 진입마다 + F5**: 핫리로드 감시 스레드 없이 "저장 → 키 하나"로 끝나 단순하다. 셰이더의 파일 감시
  핫리로드(`docs/shader-pipeline.md`)와 달리 게임 상태(`m_balance`)를 건드리므로 메인 스레드가 명시적으로 호출.
- **왜 `core::CsvFile` 가 범용**: 같은 형식을 무기 표(`weapons.csv` ✅)·몹 타입 표 등으로 넓힐 때 재사용. `game/` 은 이 위에
  얇은 로더만 얹는다(`CircularBalance.cpp`).
- **결정성**: 시뮬레이터는 고정 시드(`kProgression.rngSeed`) + 고정 스텝이라 같은 CSV 면 항상 같은 표가 나온다.
- 무기 수치는 `weapons.csv` ✅(아래) — `game/Card.h` 의 `kCardDefs` 는 이제 폴백일 뿐.

---

## 기초 설계 기반 확장 계획 — 앞으로 얹을 CSV 표

기준 문서 [# Circular 기초 설계.md](<# Circular 기초 설계.md>) 와 [circular-design.md](circular-design.md) 를 위반하지 않는 범위에서,
**테이블로 설정한다**는 기초 설계 원칙(스폰 패턴의 숫자·속도·모양은 테이블)을 CSV 로 이어 간다. 아래는 **예정(❌)** 이고,
현재 구현된 것은 위 여덟 파일(`balance`/`levels`/`spawn_curve` + M1 의 `player`/`stats`/`characters` + M5 의 `weapons`/`accessories`, §"weapons.csv — 무기 (M5)"/"accessories.csv — 장신구 (M5)")뿐이다. 태그는 [circular-design.md](circular-design.md) §0 와 같다.

| 파일 (예정) | 한 행 = | 열(제안) [살] | 기초 설계 근거 | 단계 |
|---|---|---|---|---|
| `mobs.csv` | 몹 1종 | `id, name, class, biome, health, speed, radius, contact_damage, xp, range, projectile, telegraph_sec, zone_radius` | B-적(근접·탱커·원거리·마법) | M3 |
| `spawn_patterns.csv` | 스폰 패턴 1개 | `pattern_id, kind(oneway\|enclose\|lines_alt), mob, count, speed, shape(arrow\|rect\|line), width, spacing, direction, interval, telegraph_sec` | B-스폰(숫자·속도·모양은 테이블) | M4 |
| `stage_timeline.csv` | 시간표 1칸 | `timeline_id, time_sec, pattern_id` | B-스폰 + B-스테이지 | M4 |
| `stages.csv` | 스테이지 1개(20행) | `stage_no, biome, flow, survive_sec(=90), mob_pool, timeline_id, boss_pool` — **`arena` 열 없음: 무한 필드 [확정]** | B-스테이지, B-적(보스) + **[확정]** 90초 버티기 | M6 |
| `bosses.csv` | 보스 1체 | `boss_id, name, biome, health, phases, pattern_ids` — **바이옴당 2~3행 [확정]**, 이름·능력 [미정], 왕국 성에 `왕의 기사` [기초] | B-적(보스) + **[확정]** 컨셉별 2~3개 | M6 |
| `nodes.csv` | 마계숲 라운드 후보 가중치 | `node_type(battle\|rest\|shop\|event), weight, min_stage, max_stage` — **휴식·상점·이벤트 [확정]** | B-스테이지(마계숲 선택지) | M6 |
| `rest.csv` / `shop.csv` / `events.csv` | 노드 세부 | 내용 **[미정]** (휴식 효과·상점 진열/가격·이벤트 종류) — 파일만 예약 | 위 노드 | M6 |
| `anim_clips.csv` | 애니메이션 클립 1개 | `clip, fps, mode, fit_sec, pivot_x, pivot_y, hit_frame` | (살) — [circular-art-guide.md](circular-art-guide.md) §6 | M7 |
| `difficulty.csv` | 난이도 1단계(6행) | `id(normal\|hard\|very_hard\|hardcore\|extreme\|insane), name, mob_health_mul, mob_speed_mul, spawn_rate_mul, xp_mul` — **6단계 이름은 [기초] 고정**, 효과는 [미정]이라 처음엔 전부 1.0 | **B-씬**(타이틀 → 시작 → 난이도 설정) | M8 |
| `meta_upgrades.csv` | 다회차 강화 1칸 | 내용 **[미정]** (기초 설계 원문이 "디테일한 강화트리는 차후 추가") — 파일만 예약 | B-씬(타이틀 → 강화) | M8 |

### 공통 규칙 [살]

- **id 는 소문자 영문/숫자/밑줄**(`magic_knight`, `forest_imp`, `oneway_arrow_basic`). 한글 표시 이름은 `name` 열. 이미지 파일 이름이 이 id 를 그대로 쓴다([circular-art-guide.md](circular-art-guide.md) §3).
- **참조는 id 로만**(`stages.csv` 의 `mob_pool`, `timeline_id`, `boss_id` 등). 없는 id 참조는 **오류 행**(현재 로더의 "문제 행만 건너뛰고 보고" 규칙 그대로).
- 새 표 = `CircularBalance` 에 `std::vector<Row>` + 로더 블록 하나(아래 "새 밸런스 항목 추가하기"의 표 방식). `Simulation` 은 `m_balance` 만 읽는다. HUD 밸런싱 줄·F5/F6·시뮬레이터는 그대로 새 표를 포함한다.
- **[기초]·[확정] 항목(스폰 패턴 3종의 존재, 바이옴별 출현 분류, 스테이지 90초 버티기, 마계숲 노드 3종 등)을 CSV 로 뒤집을 수 있게 만들지 않는다** — CSV 는 숫자·속도·모양·개수 같은 [살] 값의 튜닝 지점이다.

### 지금 코드와의 대응

- 현재 `balance.csv` 의 `mob_*` 한 세트 = **근접 몹 1종**. `mobs.csv`(M3)가 생기면 이 키들은 `mobs.csv` 의 첫 근접 행으로 이관하고 `balance.csv` 에는 스폰 거리 등 전역 값만 남긴다.
- 현재 `kChargePattern`(붉은 구역 예고→돌진)은 **기초 설계 밖 임시 테스트**([circular-design.md](circular-design.md) §6.5) — CSV 로 빼지 않고 정식 스폰 패턴이 대체한다.
- "카드"는 기초 설계의 **"소지 무기"**다 — 코드 이름 `Card*` 는 그대로이며, 실제 수치는 `weapons.csv` ✅(`kCardDefs` 는 폴백).
