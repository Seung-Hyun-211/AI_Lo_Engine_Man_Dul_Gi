# 2D 농사 게임 로드맵 (Farming 2D Roadmap)

`docs/roadmap.md`(엔진 전체) · `docs/synopsis.md`(현재 확정 장르: 대규모 디펜스)와 별도로,
**"2D 농사 게임"으로 피벗한다면** 무엇을 배제하고 무엇을 새로 만들어야 하는지 모으는 문서.
`synopsis.md`의 장르 확정과 충돌하므로 착수 전 그 문서를 갱신할지부터 판단 필요(§4).

---

## 1. 전제 — 3D 배제

CLAUDE.md 불변규칙 7에 따라 3D는 `ENGINE_WITH_3D`로 이미 분리돼 있음. 2D 농사 게임은 이 매크로를 끄고
아래를 정리하는 것으로 시작한다.

| 대상 | 처리 |
|---|---|
| `CppWindowGame.vcxproj` `PreprocessorDefinitions` | `ENGINE_WITH_3D` 제거(`ENGINE_WITH_2D`만 유지) |
| `src/render/r3d/*`, `src/physics/p3d/*`, `src/math/Math3D.h` | 매크로로 빌드 제외됨(삭제는 선택 — 나중에 3D 부활 여지 vs 저장소 정리 트레이드오프) |
| `src/import/ModelImporter.*`(ufbx) | FBX 로더 자체가 3D 전용 → 매크로 밖이면 미사용, 완전 제거 후보 |
| `assets/shaders/{mesh,model,cel,outline,crease,shadow,shadow_instanced,mesh_instanced,mesh_instanced_toon,common3d.hlsli,ssao*,composite*,particle,debugline}.hlsl` | 전부 3D/포스트프로세스 전용 → 미사용, 삭제 후보(`quad2d.hlsl`/`sprite2d.hlsl`만 남김) |
| `src/game/vfx/*`(파티클) | `ParticlePass3D` 의존 → 2D 이펙트는 새로 설계(§3) |
| `game/CrowdConfig.h`, `Ordnance/GibPiece/SlowZone`, `defense-combat-design.md` 관련 코드 | 디펜스 게임 콘텐츠 — 장르 전환 시 전부 미사용 |
| `Simulation`/`SnapshotBuilder`의 `DemoScene{CharacterDemo,DefenseCombat,ShadowShowcase,EffectsTest}` 분기 | 전부 3D 데모 → 농사 씬 하나로 교체 |

**판단지점**: 삭제 vs 매크로 배제 유지. 권장(A) = 당장은 매크로만 끄고 빌드 확인 → 실제 새 2D 콘텐츠가 자리잡으면 그때 죽은 코드 일괄 삭제(빌드 시간·저장소 정리는 나중 커밋으로 분리, 리뷰 용이).

---

## 2. 현재 있는 2D 인프라 (재사용)

| 있음 | 위치 |
|---|---|
| 쿼드/스프라이트 드로우 | `render/r2d/QuadPass2D`, `SpritePass2D` + `sprite2d.hlsl` |
| 텍스처 아틀라스(무압축 `.dds`) | `render/r2d/TextureAtlas`, `tools/atlas_pack`(`atlas-build-pipeline.md`) |
| UI 프레임워크 | `ui::UIContext`/`Widget`(`ui-architecture.md`), 스크롤 목록(`scrollable-list-and-pool.md`) |
| 씬 상태·설정 | `scene-flow-design.md`, `game-settings.md` — 장르 무관, 그대로 재사용 |
| 고정 스텝 + time scale | `time-design.md` — 계절/일일 사이클의 기반으로 재사용 |
| 오디오(XAudio2 믹서+스트리밍) | `audio-design.md` — 그대로 재사용 |
| 충돌 탐지(2D) | `physics/p2d`(Box/Circle, `CollisionWorld2D`) — 밭 타일/상호작용 판정에 재사용 가능하나 브로드페이즈 없음(P1) |
| 엔티티 뼈대 | `core::EntityId`/`EntityRegistry`(`entity-lifecycle-design.md`) — 아직 `Simulation` 미연결 |
| 오브젝트 풀 | `core::ObjectPool<T>` — 작물/아이템 드롭 등에 재사용 |

---

## 3. 신규로 필요한 것 (농사 게임 핵심)

| # | 항목 | 왜 | 메모 |
|---|---|---|---|
| F1 | **타일맵 시스템** | 밭/지형의 기본 단위. 지금 엔진엔 그리드 개념 자체가 없음(3D 디펜스는 자유 좌표) | 새 `game/Tilemap.*` — 셀 좌표↔월드좌표, 레이어(지면/오브젝트), `SpritePass2D` 배치 렌더 |
| F2 | **작물 성장 상태 머신** | 심기→성장 단계(N단계 스프라이트)→수확, 시간 기반 | `time-design.md`의 개체별 time scale/일시정지 재사용. 새 `game/Crop.*` |
| F3 | **일일/계절 사이클** | 낮/밤 조명 톤, 계절별 작물 제약 | §3-A(2D 라이트/톤) + 새 `game/Calendar.*`(고정 스텝 tick 소비) |
| F4 | **인벤토리/아이템** | 씨앗·수확물·도구 소지 | `ui::ScrollList`(있음) 위에 그리드 UI, 새 `game/Inventory.*` |
| F5 | **2D 캐릭터 이동+상호작용** | 플레이어가 타일 앞에서 행동(심기/물주기/수확) | `physics/p2d` 재사용(레이캐스트 없음 — 근접 판정은 오버랩 질의로 충분), 새 `game/PlayerFarmer.*` |
| F6 | **세이브/로드** | 진행 상태(밭 상태, 인벤토리, 날짜) 영속화 | `roadmap.md` P2 "프리팹/직렬화/세이브"가 선행 조건 — 농사 게임에선 P0로 승격 |
| F7 | **2D 프레임 애니메이션** | 캐릭터/작물 스프라이트 애니 | `animation-design.md` §4에 설계만 존재 — 이제 실착수 대상 |
| F8 | **NPC/상점(선택)** | 판매·구매 루프 | 장르 확정 후 결정 |

### 3-A. 2D 조명/그림자 (별도 논의 반영)

이전 논의 결론 — **라이트맵 합성 + 가짜 그림자 스프라이트**(스타듀밸리류) 채택 권장:

- 새 오프스크린 타깃에 광원(태양/랜턴) 원형 그라디언트 quad를 가산 블렌딩 → 라이트맵
- 메인 씬 컬러 × 라이트맵 = 최종 합성(곱연산) → 낮/밤 톤 + 국소 광원
- 그림자는 실시간 오클루전 없이 오브젝트 발밑 반투명 실루엣 스프라이트(광원 방향에 따라 스케일/오프셋)
- 새 패스 `render/r2d/LightMapPass2D` 1개 + 기존 `sprite2d.hlsl` 변형 정도로 충분(신규 셰이더 최소)

착수 시 → `docs/lighting.md`에 "2D 라이트맵" 절 신설(3D 조명과 별개 절, 서로 `#include` 금지 원칙 유지) + "사용 방법".

---

## 4. 우선순위

### P0 — 장르 전환 착수 시 바로 필요

| 항목 | 문서/파일 |
|---|---|
| `ENGINE_WITH_3D` 끄고 빌드 확인 | §1 |
| F1 타일맵 | 신규 `game/Tilemap.*` |
| F2 작물 성장 | 신규 `game/Crop.*` |
| F5 2D 이동+상호작용 | 신규 `game/PlayerFarmer.*` |

### P1 — 루프 완성

| 항목 |
|---|
| F4 인벤토리/UI |
| F3 일일/계절 사이클 + §3-A 2D 라이트맵 |
| F6 세이브/로드 |
| 2D 브로드페이즈(`p2d`, 타일 수 늘면 선형 스캔 한계) |

### P2 — 이후

| 항목 |
|---|
| F7 프레임 애니메이션 고도화(블렌드/이벤트) |
| F8 NPC/상점 |
| 게임패드 지원(엔진 공통 P1 항목, `roadmap.md`) |

---

## 5. 판단 필요 (착수 전에 결정)

1. **`synopsis.md` 장르 재확정** — 현재 "대규모 디펜스"로 확정돼 있음. 농사 게임으로 실제 피벗이면 그 문서를 갱신해야 함(엔진 우선순위 재정렬 문서이므로). 지금은 이 로드맵만 별도로 존재 — 실행 명령 시 같이 갱신할지 확인.
2. **타일 크기/그리드 좌표계** — 픽셀 단위 vs 논리 셀 단위, `Math2D.h`에 그리드 헬퍼 추가 여부.
3. **세이브 포맷** — 텍스트(JSON) vs 바이너리, 버전 마이그레이션 정책.
4. **아트 파이프라인** — 기존 `atlas_pack`(무압축 `.dds`) 그대로 쓸지, 타일셋 전용 슬라이싱 규칙 추가할지.

---

## 6. 사용 방법 (How to use)

- **다음 작업 고르기**: §4 P0 위에서부터. §5 판단 항목이 막으면 그것부터 사용자에게 확인.
- **항목 착수**: 이 문서의 표 항목을 그 시스템 문서(`Tilemap.*`라면 새 `docs/tilemap-design.md` 등)로 옮기고 "사용 방법"까지 채운 뒤, 여기 표 행은 링크로 축약(기존 `roadmap.md`와 동일한 관례).
- **`docs/roadmap.md`와의 관계**: 그 문서는 엔진 전체(3D 포함) 기준 로드맵이라 이 문서와 우선순위가 다르다. 장르가 농사로 확정되면 `roadmap.md` §3의 "디펜스 게임 우선순위" 절을 이 문서 기준으로 교체한다.
- **하지 말 것**: 3D 전용 시스템(포인트/스팟광, GPU 스키닝, 호드/VAT 등)을 이 로드맵에 올리지 않는다 — `ENGINE_WITH_3D` 배제 대상이라 무관.
