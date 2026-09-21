# 서큘러 이미지 리소스 가이드 — 추가 방법 · 이름 규칙 · 애니메이션 설계

서큘러에 이미지(캐릭터·몹·무기 아이콘·이펙트·타일·UI)를 넣는 **작업자용 사용 방법 문서**다.
기준은 [# Circular 기초 설계.md](<# Circular 기초 설계.md>)(HUD 위젯, 캐릭터 4종, 적 5분류, 바이옴 4개)와 [circular-design.md](circular-design.md).

**이 문서를 읽는 법**

| 표기 | 뜻 |
|---|---|
| ✅ 지금 된다 | 실제로 실행해 확인한 절차 (오프라인 패킹) |
| ❌ 아직 없다 | 코드가 없다. 설계만 적고 **구현 단계 M7**([circular-design.md](circular-design.md) §10)에서 만든다 |
| **[규칙]** | 이 프로젝트의 이름/치수 규칙 — 지키면 나중에 코드가 파일 이름만으로 자동 연결된다 |
| **[미정]** | 사용자 결정 대기 (§9) |

관련: [atlas-build-pipeline.md](atlas-build-pipeline.md)(패커 상세) · [texture-atlas-and-sprite-pass.md](texture-atlas-and-sprite-pass.md)(런타임) ·
[image-assets.md](image-assets.md)(디코드·색공간) · [animation-design.md](animation-design.md) §2(2D 프레임 애니메이션).

---

## 1. 지금 무엇이 되고 안 되나

| 단계 | 상태 | 비고 |
|---|---|---|
| PNG/JPG/BMP/GIF/TGA 디코드 | ✅ | `import::LoadImageFromFile` (stb) |
| 아틀라스 패킹 (`tools/atlas_pack`) | ✅ | 그룹 단위 증분 빌드, 무압축 `.dds` + `.atlas` |
| 스프라이트 이름 → uv 조회 (`AtlasIndex::Find`) | ✅ | 코드는 있으나 지금 이걸 로드하는 곳이 없다(데모 UI 삭제로 미사용) |
| 화면에 그리기 (`SpritePass2D`) | 🟡 | **UI 스프라이트만**(`uiSprites`, 화면 공간), 아틀라스 **1장**(`assets/atlas/ui.0.dds`, `main.cpp` 에 하드코딩)만 바인드 |
| **월드 스프라이트**(캐릭터·몹·이펙트를 월드 좌표로) | ❌ | 월드용 `SpriteDraw` 배열 + 다중 아틀라스 필요 (M7) |
| **스프라이트 애니메이션** (`SpriteAnimator`) | ❌ | 설계만 ([animation-design.md](animation-design.md) §2) → 이 문서 §6 이 서큘러용으로 구체화 |
| 픽셀 아트용 point 샘플링 | ❌ | 지금 샘플러는 LINEAR — §7 참고 |

**결론**: 지금 작업자는 이미지를 **만들고, 이름을 붙이고, 아틀라스로 굽는 것까지** 할 수 있다. 화면에 나오려면 M7 코드가 필요하다. 그때까지 게임은 색 사각형 자리표시자로 돈다 — 이름 규칙만 미리 지켜 두면 M7 에서 재작업이 없다.

---

## 2. 폴더 구조와 아틀라스 그룹

```
assets/
  src/<그룹 경로>/*.png      ← 원본(작업 파일). 그룹 폴더 아래 하위 폴더 자유(재귀로 전부 읽음)
  atlas/atlas.groups         ← 그룹 목록 (한 줄 = 한 그룹)
  atlas/<그룹>.0.dds         ← 산출물: 페이지 (그룹 이름의 / 는 _ 로)
  atlas/<그룹>.atlas         ← 산출물: 스프라이트 이름 → uv 표
  atlas/<그룹>.cache         ← 증분 빌드용 (git 무시)
```

**그룹 = 같이 로드되고 같이 바뀌는 이미지 묶음.** 서큘러 권장 그룹 **[규칙]**:

| 그룹 | 내용 | 로드 시점 | 권장 `page / mips / gutter` |
|---|---|---|---|
| `ui` | HUD·버튼·게이지·재화 아이콘 | 상주 | 1024~2048 / 2 / 4 |
| `char/<id>` | 플레이어블 1명 전 클립 + 초상화 (`char/madoknight` …) | 그 캐릭터 선택 시 | 1024 (넘치면 2048) / 0 / 2~4 |
| `mob/<biome>` | 바이옴별 일반 몹 (`mob/forest` `mob/meadow` `mob/village` `mob/castle`) | 그 바이옴 스테이지 | 1024~2048 / 0 / 2~4 |
| `boss/<id>` | 보스 1체 (`boss/kingknight` = 왕의 기사) | 그 보스 스테이지 | 2048 / 0 / 4 |
| `weapon` | 무기 아이콘 + 투사체/스윙 프레임 | 상주 | 1024~2048 / 0 / 2~4 |
| `accessory` | 장신구 아이콘 | 상주 | 1024 / 0 / 2 |
| `fx` | 이펙트 프레임(폭발·힛·텔레그래프 등) | 상주 | 1024~2048 / 0 / 2~4 |
| `tile/<biome>` | 배경 타일 (`tile/forest`…) | 그 바이옴 | 2048 / 0 / 4 |

- 페이지는 **무압축**이라 메모리 = `page² × 4 바이트`(1024=4 MB, 2048=16 MB, 4096=64 MB). **가장 작은 페이지**를 쓰고, 넘치면 패커가 페이지를 늘린다(`.1.dds`…).
- `mips 0`(밉 없음)을 스프라이트 그룹의 기본으로 권장한다: 밉이 켜지면 축소 시 인접 프레임 색이 번질 수 있다. UI 는 크기가 다양해서 `2`.
- 그룹 이름의 `/` 는 산출 파일에서 `_` 가 된다: `mob/forest` → `mob_forest.0.dds`, `mob_forest.atlas`.

---

## 3. 파일 이름 규칙 **[규칙]**

**스프라이트 이름 = 파일 이름(확장자 제외, "stem")** 이고, 코드는 이 이름으로 `AtlasIndex::Find(name)` 한다. 그룹 안에서 **중복 불가**(패커가 에러 — 하위 폴더가 달라도 같은 이름이면 에러, 확인함).

### 3.1 형식

```
<분류>_<id>_<상태>[_<시점>]_<NN>.png        애니메이션 프레임
<분류>_<id>_<이름>.png                       단일 이미지 (아이콘·초상화·UI 조각)
```

- **소문자 영문 + 숫자 + 밑줄만.** 공백·한글·대문자·하이픈·점 금지. (`.atlas` 파서가 공백으로 칸을 나눈다.) 정규식: `^[a-z0-9]+(_[a-z0-9]+)*$`
- **`<id>` 는 데이터 표의 id 와 같아야 한다**: `characters.csv`/`mobs.csv`/`weapons.csv` 의 `id`. 한글 표시 이름은 CSV 의 `name` 열에 둔다(지금 UI 폰트는 한글 불가).
- **`<NN>`** 은 **0 부터 2자리** 연속 번호(`00 01 02 …`). 빠진 번호가 있으면 그 지점에서 클립이 끊긴 것으로 본다(§6). 클립 = 이름에서 `_NN` 을 뺀 것.

### 3.2 분류(접두어)

| 분류 | 접두어 | 예 (파일 stem) | 그룹 |
|---|---|---|---|
| 플레이어블 | `char` | `char_madoknight_walk_00`, `char_madoknight_portrait` | `char/<id>` |
| 일반 몹 | `mob` | `mob_forest_imp_walk_03` (`mob_<바이옴>_<이름>_…`) | `mob/<biome>` |
| 보스 | `boss` | `boss_kingknight_atk_05` | `boss/<id>` |
| 무기 | `wpn` (아이콘) · `prj` (투사체/스윙 프레임) | `wpn_pulse_icon`, `prj_bolt_02` | `weapon` |
| 장신구 | `acc` | `acc_ring_speed_icon` | `accessory` |
| 이펙트 | `fx` | `fx_hit_00`, `fx_telegraph_arrow_02` | `fx` |
| 타일 | `tile` | `tile_forest_grass_00` | `tile/<biome>` |
| UI | `ui` | `ui_hud_stamina_frame`, `ui_hud_stamina_fill` | `ui` |

바이옴 id: `forest`(마계숲) `meadow`(초원) `village`(인간마을) `castle`(왕국 성).
캐릭터 id: `madoknight`(마도기사) `skullmage`(스컬매지션) `succubus`(서큐버스) `assassinlizard`(어쎄신리자드).
몹 id: `<바이옴>_<이름>` — 예 `forest_imp`. 보스 id 는 `bosses.csv` 의 `boss_id`(이름 [미정] — 정해지기 전엔 `<바이옴>_boss_a`/`_b`/`_c` 임시 id). **분류(근접/탱커/원거리/마법)는 이름이 아니라 `mobs.csv` 의 `class` 열**이 정한다(파일 이름에 넣지 않는다 — 분류는 바뀔 수 있다).

### 3.3 상태 어휘 (고정 목록)

`idle` `walk` `run` `dash` `hit` `die` `atk` `cast` `ult` `telegraph` `spawn`

- 이 목록 밖 상태를 새로 쓰면 코드가 어떤 엔진 상태와 연결할지 모른다 — 필요하면 **목록을 먼저 늘리고 §6.2 대응표에 한 줄 추가**한다.
- 시점: 기본(접미어 없음) = **오른쪽을 보는 옆모습**. 왼쪽은 **런타임에서 좌우 반전**하므로 **왼쪽 프레임을 따로 그리지 않는다**. 위/아래를 보는 그림이 꼭 필요할 때만 `_up` / `_down` 을 상태 뒤에 붙인다(`char_madoknight_walk_up_00`).

### 3.4 UI 이름 — HUD.png 기준 예

| 위젯 ([circular-design.md](circular-design.md) §7.2) | stem |
|---|---|
| (좌상 1) 캐릭터 초상화 프레임 / 초상화 | `ui_hud_portrait_frame` / `char_<id>_portrait` |
| (좌상 2-1, 2-2) 재화 아이콘 | `ui_hud_currency_run_icon`, `ui_hud_currency_meta_icon` |
| (좌상 3) 스테미너 게이지 틀 / 채움 | `ui_hud_stamina_frame`, `ui_hud_stamina_fill` |
| (좌상 4) 궁극기 게이지 틀 / 채움 / 사용가능 | `ui_hud_ult_frame`, `ui_hud_ult_fill`, `ui_hud_ult_ready` |
| (우상 1) 설정 버튼 일반/호버 | `ui_btn_settings_normal`, `ui_btn_settings_hover` |
| (우상 2-1) 스탯 버튼 | `ui_btn_status_normal`, `ui_btn_status_hover` |
| (우상 2-2) 스탯 팝업 배경 (9-slice 후보) | `ui_hud_status_popup_bg` |
| (하단 1, 2) 슬롯 배경 / 빈 슬롯 | `ui_hud_slot_bg`, `ui_hud_slot_empty` |
| (우상 2-2) 상태창의 능력치 아이콘 — id 는 `stats.csv` 의 `stat_id`(체력 `vit` 등 [확정 스텟]) | `ui_icon_stat_vit`, `ui_icon_stat_int`, `ui_icon_stat_cor`, `ui_icon_stat_agi`, `ui_icon_stat_luck`, `ui_icon_stat_attack_speed` … |
| 마계숲 노드 아이콘([확정] 휴식·상점·이벤트 + 전투) | `ui_map_node_battle`, `ui_map_node_rest`, `ui_map_node_shop`, `ui_map_node_event` |
| 레벨업 선택지 표식(신규/강화/오버플로우/스탯) | `ui_levelup_badge_new`, `ui_levelup_badge_upgrade`, `ui_levelup_badge_overflow`, `ui_levelup_badge_stat` |

- **호버 아웃라인**([기초] UI 규칙)은 **코드가 그린다**(위젯 공통) — `_hover` 이미지를 따로 만들 필요는 없다. 버튼 그림 자체가 바뀌는 경우에만 `_hover` 를 만든다.
- 지금 `ui` 아틀라스(`assets/atlas/ui.*`)의 예전 이름(`bar_fill`, `icon_play`, `icon_settings`, `panel_bg`)은 **임시 테스트 이미지**다. 실제 UI 아트가 들어올 때 위 규칙 이름으로 교체한다.

---

## 4. 이미지 사양 **[규칙]**

| 항목 | 규칙 |
|---|---|
| 포맷 | **PNG-32(RGBA)**. 그 외(JPG/BMP/GIF/TGA)도 읽히지만 알파가 필요한 스프라이트는 PNG |
| 알파 | **straight(non-premultiplied)**. 프리멀티 저장 금지 ([image-assets.md](image-assets.md) §3) |
| 색공간 | **sRGB** 로 작업(패커가 `R8G8B8A8_UNORM_SRGB` 로 굽는다). 리니어로 작업하지 말 것 |
| 투명 픽셀의 RGB | **가장자리 색으로 번지게(dilate)** 저장 — 검정 투명(0,0,0,0)이 경계에 섞이면 테두리가 어두워진다 (패커의 gutter 는 이 색을 그대로 연장) |
| 클립 안 프레임 | **캔버스 크기가 전부 같아야 함**(같은 클립 `_00`~`_NN`). 캐릭터가 캔버스 안에서 움직이는 양은 그림 안에서 처리 |
| 캔버스 크기 [살] | 플레이어 64×64(현재 `kPlayerSize` 64), 일반 몹 32×32(반경 10 기준 — 탱커 48×48), 보스 128~192, 이펙트 32~128, 타일 64×64, 무기/장신구 아이콘 48×48, 초상화 ≈176×168(HUD 좌상 1 실측) |
| 원점(pivot) | 캐릭터·몹·보스는 **캔버스 하단 중앙(발밑)**, 이펙트·투사체·아이콘은 **중앙**. 패커는 pivot 을 안 쓰므로(기본 0.5,0.5) **`anim_clips.csv` 의 `pivot_x/pivot_y` 로 지정**(§6.1) |
| 방향 | 원본은 top-down 행 순서 그대로(뒤집지 말 것) |
| 큰 배경 | 페이지(최대 4096)보다 큰 그림은 아틀라스에 못 넣는다(패커 에러). 큰 배경은 타일로 쪼개거나 단독 텍스처 경로가 필요([미정], 지금 없음) |

---

## 5. 이미지 추가 절차 ✅ (오프라인 패킹 — 실제로 확인한 절차)

예: 마도기사 걷기 프레임 6장 + 초상화를 추가한다.

1. **원본 배치** — `assets/src/char/madoknight/` 아래에 §3 규칙 이름으로 저장:
   `char_madoknight_walk_00.png … char_madoknight_walk_05.png`, `char_madoknight_portrait.png`
   (하위 폴더 `walk/` 등으로 정리해도 되지만 **stem 은 그룹 전체에서 유일**해야 한다).
2. **그룹 등록** — `assets/atlas/atlas.groups` 에 없으면 한 줄 추가:
   ```
   group char/madoknight page 1024 mips 0 gutter 2
   ```
   (`page` = 1024|2048|4096, `mips` = 개수 | -1(끝까지) | 0(없음), `gutter` = 스프라이트 둘레 여백 px 0~64.)
3. **패커 빌드**(처음 한 번) — `tools\build_atlas_pack.bat` → `build\tools\atlas_pack.exe`.
4. **굽기** — 저장소 루트에서:
   ```bash
   build\tools\atlas_pack.exe --group char/madoknight
   ```
   (`--all` 전체, `--force` 캐시 무시 재빌드.) 성공하면 `assets/atlas/char_madoknight.0.dds` + `char_madoknight.atlas` 가 생긴다.
   **같은 입력으로 다시 실행하면 `up to date` 로 건너뛴다**(입력 해시 캐시) — 이미지를 고쳤는데 안 바뀌면 `--force`.
5. **확인** — `.atlas` 를 열어 이름·개수를 본다(한 줄 = 한 스프라이트):
   ```
   # name  page  u0 v0 u1 v1  pixelW pixelH  [pivotX pivotY]
   char_madoknight_walk_00 0 0.001953 0.001953 0.033203 0.033203 32 32
   ```
   에러 메시지: `duplicate sprite name` (stem 중복), `a sprite is larger than the … page` (페이지보다 큼), `failed to decode` (깨진 이미지).
6. **커밋** — 원본 PNG(`assets/src/...`), `atlas.groups`, 산출물 `.dds`/`.atlas` 를 커밋한다. **`.cache` 는 커밋하지 않는다**(`.gitignore`). `.dds` 는 무압축이라 크다(1024 페이지 4 MB) — 자주 갈아엎을 임시 이미지는 커밋 전에 정리.
7. **게임에 나오게 하기** ❌ — 지금은 여기까지가 한계다. 화면에 그리려면 **M7 코드**(월드 스프라이트 경로)가 필요하다:
   - 다중 아틀라스 로드(현재 `main.cpp` 는 `SpritePass2D("assets/atlas/ui.0.dds")` 한 장, `kUiAtlasId = 1`)
   - `RenderSnapshot` 월드 스프라이트 배열 + `SnapshotBuilder` 가 `AtlasIndex::Find(stem)` 로 uv 를 채워 값으로 방출
   - 사용 모양(설계 스케치):
     ```cpp
     // 그룹 로드 (Application, 시작/스테이지 진입 시)
     atlases.Load("assets/atlas/mob_forest.atlas", /*atlasId*/ 3);
     // 스냅샷 (메인 스레드) — 이름은 §3 규칙으로 조립
     const render::SpriteRect* r = atlases.Find("mob_forest_imp_walk_03");
     snapshot.worldSprites.push_back({ x, y, w, h, r->u0, r->v0, r->u1, r->v1, /*tint*/ 1,1,1,1, /*atlasId*/ 3 });
     ```

**아트가 아직 없을 때**: 그림 없이도 개발은 진행된다 — 분류별 색 자리표시자([circular-design.md](circular-design.md) §8)를 쓰고, 그림이 생기면 위 절차만 밟으면 된다(이름 규칙 덕에 코드 수정 최소).

---

## 6. 애니메이션 설계 방법

기초 설계에는 애니메이션 언급이 없다 — 이 절은 **[살] 설계 + [규칙]** 이다. 구조는 [animation-design.md](animation-design.md) §2(`SpriteAnimationClip`/`SpriteAnimator`)를 서큘러에 맞춰 구체화한 것이며 **구현은 M7 ❌**.

### 6.1 클립 = 이름 + 표 한 줄

- **프레임은 이름으로 찾는다**: 클립 `char_madoknight_walk` 는 `char_madoknight_walk_00`, `_01`, … 를 **연속으로 만나는 데까지**가 프레임이다(끊기면 거기가 끝). 프레임 목록을 어디에도 적지 않는다.
- **재생 속성은 표로**: `assets/data/circular/anim_clips.csv` (예정 — 밸런스 CSV 와 같은 로더 [circular-balance.md](circular-balance.md)):

| 열 | 뜻 | 기본 |
|---|---|---|
| `clip` | 프레임 접두어(=클립 id), 예 `char_madoknight_walk` | 필수 |
| `fps` | 초당 프레임 | 10 |
| `mode` | `loop` / `once` / `pingpong` | 상태별(§6.3) |
| `fit_sec` | 비우지 않으면 **클립 전체 길이를 이 시간에 맞춤**(fps 무시) — 대쉬(0.18s)·텔레그래프 예고 시간처럼 게임플레이 시간과 애니메이션을 일치시킬 때 | 비움 |
| `pivot_x`, `pivot_y` | 캔버스 기준 0..1 | 캐릭터류 `0.5,1.0` / 이펙트류 `0.5,0.5` |
| `hit_frame` | (무기/공격 클립) 피해가 들어가는 프레임 번호 — 애니메이션과 타격 타이밍 동기 | 비움 |

```csv
clip,fps,mode,fit_sec,pivot_x,pivot_y,hit_frame
char_madoknight_idle,6,loop,,0.5,1.0,
char_madoknight_walk,10,loop,,0.5,1.0,
char_madoknight_run,14,loop,,0.5,1.0,
char_madoknight_dash,,once,0.18,0.5,1.0,
mob_forest_imp_walk,8,loop,,0.5,1.0,
fx_hit,20,once,,0.5,0.5,
```

- **표에 없는 클립**은 기본값으로 재생한다 — 이름 규칙만 지키면 표 없이도 뜬다(fps 10, 상태별 기본 mode).

### 6.2 엔진 상태 → 클립 대응 [규칙]

| 엔티티 | 엔진 상태 | 클립 상태 | 없을 때 폴백 |
|---|---|---|---|
| 플레이어 | `PlayerMotion::Idle/Walk/Run/Dash` ([circular-design.md](circular-design.md) §2.2) | `idle` / `walk` / `run` / `dash` | `run`→`walk`→`idle`, `dash`→`run` |
| 플레이어 | 피격 / 사망 / 궁극기 | `hit` / `die` / `ult` | `hit` 없으면 색 번쩍임 |
| 몹 | `MobState::Seek` | `walk` | `idle` |
| 몹 | `MobState::Windup`(예고 정지) | `telegraph`(없으면 `idle`) — `fit_sec` = 예고 시간 | `idle` |
| 몹 | `MobState::Charge`/`March`(직진) | `run` | `walk` |
| 몹 | 원거리/마법의 발사·시전 | `atk` / `cast` | `idle` |
| 몹 | 피격 / 사망 | `hit` / `die` | 색 번쩍임 / 즉시 제거 |
| 보스 | 위와 동일 + 패턴 | `atk` `cast` `ult` | |

- 클립 이름은 **`<분류>_<id>_<상태>`** 로 조립해서 찾는다 → 새 캐릭터/몹은 **이름만 규칙대로 지으면 코드 변경 없이** 연결된다(OCP).

### 6.3 프레임 수 · 속도 가이드 [살]

| 상태 | 프레임 | fps | mode |
|---|---|---|---|
| idle | 4 | 6 | loop (부드럽게 하려면 pingpong) |
| walk | 6 | 10 | loop |
| run | 6~8 | 12~14 | loop |
| dash | 3~4 | `fit_sec`=대쉬 시간 | once |
| hit | 2~3 | 20 | once |
| die | 6 | 12 | once (마지막 프레임 유지 후 제거) |
| atk / cast | 6~8 | 12~14 / 10 | once |
| telegraph | 3~4 | `fit_sec`=예고 시간 | once (또는 loop) |
| 이펙트(fx) | 4~8 | 16~24 | once |

### 6.4 재생 구조 (수천 마리 대응) [설계, M7]

- **재생 규칙은 순수 함수 하나**: `frame = FrameIndex(t, fps, count, mode)` — 스텝 함수(보간 없음). 결과는 uv 사각형으로 **값 복사**되어 스냅샷에 실린다(렌더 스레드는 애니메이션을 모른다 — 규칙 3).
- **플레이어(1체)**: `SpriteAnimator`(상태 + 경과 시간)를 `Simulation` 이 소유하고 **고정 스텝에서만** `Tick`(결정성). 상태가 바뀌면 그 클립으로 전환(0부터).
- **몹(수천 마리)**: 몹마다 `SpriteAnimator` 객체를 두지 않는다. **`MobField` SoA 에 `animTime`(float) 한 열만 추가**하고 `Step` 의 `ParallelFor` 안에서 자기 슬롯만 증가(규칙 6). 클립은 저장하지 않는다 — `MobState`/`MobBehavior` 에서 §6.2 로 **매 프레임 계산**. 무리가 발맞춰 걷지 않도록 **위상 오프셋 = 슬롯 인덱스 해시**(지금 색 애니메이션 테스트가 이미 쓰는 방식, [animation-design.md](animation-design.md)).
- **속도 연동(선택)**: 걷기 fps 를 `fps × (실제 속도 / 기준 속도)` 로 스케일하면 발이 미끄러져 보이지 않는다.
- **좌우 반전**: 몹은 `velX` 부호, 플레이어는 마지막 이동 방향 — 반전은 uv 의 `u0/u1` 교환(추가 저장 없음).
- **이펙트**: 힛/폭발은 수명(`age`)을 가진 값 타입(지금 `HitFlash` 와 같은 패턴) → `frame = age × fps`. 아트가 있으면 `fx_*` 프레임, 없으면 지금의 절차적 글로우(`EffectPass2D`) 유지.
- **성능**: 스프라이트도 몹 수천 = 쿼드 수천. 같은 아틀라스 연속 구간은 한 번에 그린다(`SpritePass2D` 는 `(atlasId, clip)` 그룹으로 이미 배칭). 몹 그룹은 한 페이지에 모아 두면 유리하다 → **바이옴 몹을 한 그룹(`mob/<biome>`)으로** 묶는 이유.

### 6.5 애니메이션 추가 체크리스트

1. 상태 어휘(§3.3)에 있는 상태인가? 없으면 어휘·§6.2 대응표부터 추가.
2. 프레임 이름 `<분류>_<id>_<상태>_<NN>`, 0 부터 **끊김 없이**, 같은 캔버스 크기.
3. 기본 속성(§6.3)과 다르면 `anim_clips.csv` 에 한 줄 (`fps`/`mode`/`fit_sec`/`pivot`).
4. 그룹에 들어 있고 패킹했는가 (§5).
5. 폴백(§6.2)이 없어도 게임이 안 깨지는지(누락 → 색 자리표시자) 확인.

### 6.6 필요한 클립 목록 (기초 설계 기준)

| 대상 | 필수 | 선택 |
|---|---|---|
| 플레이어블 4종 | `idle walk run dash` + `portrait` | `hit die ult` |
| 근접 몹 | `walk` | `hit die` |
| 탱커 몹 | `walk` | `hit die` (느리고 묵직한 프레임) |
| 원거리 몹 | `walk atk` (+ 투사체 `prj_*`) | `hit die` |
| 마법 몹 | `walk cast` (+ 예고 `fx_telegraph_*`) | `hit die` |
| 보스 (**바이옴당 2~3체 [확정]** — 체마다 별도 그룹 `boss/<id>`) | `idle walk atk` | `cast hit die` |
| HUD | §3.4 표 | |

---

## 7. 픽셀 아트 / 해상도 주의 (알아둘 것)

- **샘플러가 LINEAR 다**(`SpritePass2D`). 픽셀 아트를 정수 배율이 아닌 크기로 그리면 흐려진다. 아트 스타일이 픽셀 아트로 확정되면 **그룹별 point 샘플링 옵션**(`atlas.groups` 에 `filter point` 추가 + 패스가 그룹별 샘플러 선택)을 만들어야 한다 ❌. 지금은 밉 없음(`mips 0`) + gutter 로 번짐만 줄인다.
- **화면 배율**: 월드 1 px = 화면 1 px 이다(카메라 줌 없음). HUD 는 1920×1080 기준 앵커 배치로 갈 예정([circular-design.md](circular-design.md) §7.3). 월드 스프라이트의 최종 배율은 **[미정]** — 캔버스는 §4 크기로 만들어 두면 엔진이 배율을 결정한다.
- **한글 텍스트**는 지금 폰트가 못 그린다 → 이미지에 글자를 굽지 말고 아이콘/숫자 위주로(한글 표기는 글리프 아틀라스 이후).

---

## 8. 사용 방법 요약 (How to use)

**새 이미지를 추가할 때**
1. 종류(§3.2)와 id(데이터 표와 같은 id)를 정한다.
2. §3 규칙으로 이름을 짓고 §4 사양으로 저장 → `assets/src/<그룹>/`.
3. 그룹이 없으면 `atlas.groups` 한 줄 → `atlas_pack --group <그룹>` (§5).
4. `.atlas` 로 확인하고 원본+산출물 커밋.
5. 애니메이션이면 §6.5 체크리스트, 기본과 다를 때만 `anim_clips.csv` 한 줄.

**새 캐릭터/몹/무기를 추가할 때**: 데이터 행(`characters.csv`/`mobs.csv`/`weapons.csv`, [circular-design.md](circular-design.md) "종류를 늘리는 법")의 `id` 를 먼저 정하고 **그 id 로 이미지 이름을 짓는다** — 이름이 곧 연결 고리다.

**하지 말 것**
- 이름에 **공백·한글·대문자·하이픈**을 쓰지 말 것. 프레임 번호를 **1 부터 시작하거나 한 자리**로 쓰지 말 것(`_00` 부터 2자리).
- **왼쪽 보는 프레임을 따로 만들지 말 것**(런타임 반전).
- 같은 클립 안에서 **캔버스 크기를 바꾸지 말 것**, 프레임 번호를 **건너뛰지 말 것**.
- **프리멀티플라이드 알파**로 저장하지 말 것. 투명 영역을 **검정(0,0,0,0)** 으로 두지 말 것(가장자리 색으로).
- 배포된 스프라이트 **이름을 바꾸지 말 것** — 코드·CSV 가 이름으로 찾는다. 바꿔야 하면 CSV·코드 참조를 함께 갱신.
- `.cache` 를 커밋하지 말 것. 코드에 **아틀라스 id 를 숫자로 박지 말 것**(그룹 이름으로 등록).
- 그림이 없다고 **코드에 임시 색/크기를 하드코딩하지 말 것** — 자리표시자 규칙([circular-design.md](circular-design.md) §8)을 쓴다.

---

## 9. 확인이 필요한 질문 (사용자 결정 대기)

1. **아트 스타일**: 픽셀 아트인가, 고해상도 일러스트인가? (샘플러·캔버스 크기·배율이 여기서 결정 — §7)
2. 캔버스 크기(§4 제안값)와 월드 스프라이트 화면 배율.
3. 몹 이름(id) 목록과 바이옴별 개수 — 이름 규칙(`mob_<바이옴>_<이름>`)은 위로 확정해도 되는지.
4. 큰 배경(스테이지 배경)의 처리 방식 — 타일로 쪼갤지, 단독 텍스처 경로를 둘지.
5. `anim_clips.csv` 형식(열)과 `fit_sec`/`hit_frame` 필요 여부.
