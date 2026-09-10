# 아틀라스 빌드 파이프라인 — 번들 · 페이지 · 포맷 · 밉맵

`docs/texture-atlas-and-sprite-pass.md` 가 **런타임**(SpriteDraw / SpritePass2D / DrawList)을
다룬다면, 이 문서는 그 앞단 — **아틀라스를 어떻게 묶고, 언제 다시 만들고, 어떤 크기·포맷·밉맵으로
굽는지**. 핵심 원칙: **매번 전부 다시 만들지 않는다.** 함께 바뀌는 것끼리 묶어(그룹) 그룹 단위로만
증분 빌드한다.

**범위: DirectX 11 데스크톱(Windows) 전용.** 모바일·OpenGL ES·Metal·WebGL 은 타겟이 아니다 —
포맷·페이지 크기·컨테이너를 전부 D3D11 에 맞춰 **하나로 고정**한다(크로스플랫폼 분기 없음).
모바일로 확장할 일이 생기면 §4 "참고: 모바일" + §5 노트를 출발점으로.

**상태: `tools/atlas_pack` v1 구현** — **무압축** `R8G8B8A8_UNORM_SRGB` `.dds` 페이지만(BC7/BC4 인코더 미vendor, 후속 `--format bc7` 로 격리 확장 예정). shelf 패킹 + edge-extend gutter + box-filter 밉 + `.dds`/`.atlas`/`.cache`(증분). 디코드는 엔진과 같은 `import::LoadImageFromFile`. 그룹 매니페스트는 아래 §1 의 블록 형식 대신 **플랫 한 줄 형식**(파서 단순화). §10 참조.

관련: `docs/texture-atlas-and-sprite-pass.md`(런타임 소비), `docs/loading-and-streaming.md`(아틀라스 = `AssetKind::Atlas` 에셋, 그룹 = 레지스트리 1항목), `docs/game-settings.md`(텍스처 품질 설정), `command-playbook.md` #3·#3d.

---

## 1. 아틀라스 그룹 (번들)

**그룹 = "같이 쓰이고 같이 바뀌는" 소스 이미지 집합.** 각 그룹은 고정 크기 페이지(§3) 1장 이상으로 구워진다.

| 그룹 | 내용 | 바뀌는 때 | 로드 시점 |
|---|---|---|---|
| `ui` | HUD·메뉴·아이콘·폰트 글리프 | UI 아트 변경 | 부팅, 상주 |
| `char/<name>` | 캐릭터별 — 초상화·능력 아이콘·(2D면) 페이퍼돌 파츠 | 그 캐릭터 아트 변경 | 그 캐릭터가 씬에 들어올 때 |
| `obj/<category>` | 프롭·픽업·투사체·타일 스프라이트 | 카테고리 아트 변경 | 씬/카테고리 단위 |
| `scene/<id>` | 씬 전용 일회성(배경을 스프라이트로 구운 것 등) | 그 씬 변경 | 그 씬 로드 시 |

- **변경 격리**: 그룹마다 독립 빌드 산출물이라, UI 아이콘 하나 고치면 `ui` 만 다시 굽는다. `char/*`·`obj/*` 는 그대로.
- **증분 빌드 ("매번 안 만듦")**: 그룹마다 `.cache` 에 입력 파일별 콘텐츠 해시 + 패커/설정 버전 + (포맷·pageSize·mips) 를 기록. 빌드 시 그룹의 입력 해시가 전부 일치하고 설정도 그대로면 **스킵**(기존 산출물 재사용), 아니면 그 그룹만 재패킹.
- 런타임은 절대 패킹하지 않는다(v1). 산출물은 커밋하거나 CI 가 굽고, 엔진은 **로드만**.

### 그룹 매니페스트 (`assets/atlas/atlas.groups`)

**v1 실제 형식** — 파서를 단순·무결하게 유지하려고 블록/글롭 대신 플랫 한 줄:

```
# group <name>  page <1024|2048|4096>  mips <N | -1 | 0>  gutter <px>
group ui page 1024 mips 2 gutter 4
group char/unitychan page 2048 mips -1 gutter 12
group obj/props page 4096 mips 4 gutter 8
```

- 입력 = `assets/src/<name>/` **아래 모든 이미지**(`.png .jpg .jpeg .bmp .gif .tga`, 재귀, 경로순 정렬). 별도 글롭 문법 없음 — 하위폴더로 그룹을 나눈다.
- 스프라이트 이름 = 파일 stem(확장자 제외). 그룹 내 중복 이름은 에러.
- 산출 basename = `<name>` 의 `/` → `_` (`char/unitychan` → `char_unitychan.*.dds` / `.atlas`).
- GPU 포맷은 매니페스트에 안 적는다 — **엔진 전역 고정**(§5, v1 = 무압축 `R8G8B8A8_UNORM_SRGB`). 그룹은 크기·밉·gutter 만 고른다.
- 실행: 프로젝트 루트에서 `build\tools\atlas_pack.exe [--all | --group <name>] [--force]` (빌드는 `tools\build_atlas_pack.bat`). 소스 낱장이 없는 fresh checkout 용 fixture 생성기: `tools/make_test_atlas_src.cpp`.

### 산출물 (그룹당)

```
assets/atlas/ui.0.dds       # 페이지 0 (밉 포함, BC7)
assets/atlas/ui.1.dds       # 오버플로 시 페이지 1, 2, ...
assets/atlas/ui.atlas       # 스프라이트 표: name → { page, u0 v0 u1 v1, pixelSize, pivot, nineSlice }
assets/atlas/ui.cache       # 증분 빌드용 (커밋 안 함, .gitignore)
```

### `loading-and-streaming` 연결

- **그룹(모든 페이지) = `AssetRegistry` 의 논리 에셋 1개**, ref-count, `SceneManifest` 에 그룹 id 로 등록.
- `ui` 그룹은 부팅 매니페스트(상주). `char/<name>` 은 캐릭터 입·퇴장에 맞춰 load/release.
- 매니페스트 diff 로 공유 그룹은 재로드 안 됨.

---

## 2. 런타임 페이지 바인딩

- `SpriteDraw`(다른 문서 §2.1)의 `atlasId` 는 **그룹+페이지** 를 식별(예: `hash("ui") ^ page`). `AtlasIndex::Find(name)` 가 `page` 도 돌려줌.
- `SpritePass2D` 그룹핑 키에 페이지 포함 — `(atlasId=group+page, clip)` 연속 구간마다 SRV 바인드 + draw. 한 그룹이 2페이지면 그 화면에서 최대 2 draw(대개 페이지 0만 쓰임).
- 패커는 **자주 같이 그려지는 스프라이트를 같은 페이지에** 넣는다(패킹 힌트: 입력 하위폴더 = 페이지 그룹핑 우선순위).

---

## 3. 페이지 크기 — 고정 집합

페이지는 항상 **정해진 크기 집합 `{1024, 2048, 4096}`** 중 하나의 정사각형. "필요한 만큼" 이 아니라 고정:

- GPU 할당이 균일·예측 가능 → 페이지 크기 텍스처를 풀링 가능.
- 메모리 예산이 `상주 페이지 수 × 페이지 바이트` 로 단순.
- 밉 체인이 규칙적(2의 거듭제곱).
- 반쯤 빈 페이지의 낭비는 감수 — 투명 영역은 BC7 로 잘 압축되고, 오버플로는 다음 페이지로.

| pageSize | 쓰는 그룹 | 근거 |
|---|---|---|
| **4096** (기본) | `ui`, `obj/*`, 큰 `char` | D3D11 FL11 최대는 16384 지만, 4096 이 패킹 효율 ↔ 페이지당 메모리(4096² BC7 = 16 MiB) 균형. 8192 는 단일 할당이 커서 스트리밍·통합 GPU 에 부담 |
| 2048 | 중간 규모 그룹 | |
| 1024 | 아이콘 몇 개짜리 그룹 | 4096 을 잡으면 낭비 — 그룹이 `pageSize` 를 낮춰 선언 |

- 크로스플랫폼 분기 없음 — `tools/atlas_pack` 은 타겟이 하나(pc)라 `--target` 플래그 자체가 없다.
- 로드 시 `ID3D11Device` 의 feature level 상 최대 텍스처가 페이지보다 작을 일은 없다(FL11 = 16384). 그래도 방어적으로 확인 후 초과 시 로드 실패 로그.

---

## 4. 텍스처 크기 — D3D11 기준

### 하드웨어 최대 2D 텍스처 (한 변)

| D3D11 feature level | 최대 | 비고 |
|---|---|---|
| **FL 11_0 / 11_1 / 12_x** | **16384** | 이 엔진 타겟(v143, DX11). 4096·8192 페이지 여유 충분 |
| FL 10_0 / 10_1 | 8192 | 이 엔진은 FL11 요구 — 참고만 |
| FL 9_3 | 4096 | |

→ 실제 값은 `ID3D11Device` 생성 시 얻은 feature level 로 결정(FL11 = 16384). WARP(이 개발 환경)도 FL11 = 16384.

### 페이지 1장 메모리 (밉 없음. 풀 밉 체인이면 ×약 1.33)

| 페이지 | RGBA8 (무압축) | **BC7** (컬러, 1 B/px) | **BC4** (단일채널, 0.5 B/px) |
|---|---|---|---|
| 4096² | 64 MiB | **16 MiB** | 8 MiB |
| 2048² | 16 MiB | 4 MiB | 2 MiB |
| 1024² | 4 MiB | 1 MiB | 0.5 MiB |

**실무 예산**: 4096² BC7 상주 페이지당 16 MiB(+밉 ~21). `ui` 1~2 + 씬 `char`/`obj` 몇 페이지 → 수십~200 MiB 대. `loading-and-streaming` LRU 로 관리. 밉 포함 시 +33% 반영.

### 참고: 모바일 (범위 밖)

지금은 안 다루지만 나중을 위한 리서치 요약 —
OpenGL ES 3.0 이 보장하는 최소 `GL_MAX_TEXTURE_SIZE` 는 **2048**(실기기는 4096~16384, 최신 iOS A11+ 은 16384), Vulkan 보장은 4096. 모바일은 페이지를 2048 로 낮추고 GPU 포맷을 ASTC(4×4 ~ 8×8) / ETC2 로 바꿔야 하며, 이는 KTX2 + Basis Universal(로드 시 타겟 포맷으로 트랜스코드)로 파일 하나를 유지하는 게 정석이다.
Sources: [Unity — Graphics hardware capabilities](https://docs.unity3d.com/550/Documentation/Manual/GraphicsEmulation.html), [Apple — Best Practices for A7 GPUs and Later](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/OpenGLES_ProgrammingGuide/BestPracticesforAppleA7GPUsandLater/BestPracticesforAppleA7GPUsandLater.html), [Apple — OpenGL ES in iOS Simulator](https://developer.apple.com/library/archive/documentation/OpenGLES/Conceptual/OpenGLESHardwarePlatformGuide_iOS/OpenGLESiniOSSimulator/OpenGLESiniOSSimulator.html).

---

## 5. 텍스처 포맷 — 하나로 고정

**컨테이너 `.dds`, GPU 포맷 BC7/BC4 로 전 그룹 고정.** DirectX 만 쓰므로 크로스플랫폼 트랜스코드 계층이 필요 없다 — `.dds` 의 `DXGI_FORMAT` 이 `CreateTexture2D` 로 1:1, 밉은 서브리소스 배열로 그대로.

| 용도 | 포맷 | 근거 |
|---|---|---|
| **컬러 아틀라스** | **BC7** — `DXGI_FORMAT_BC7_UNORM_SRGB` | FL11 항상 지원, RGBA 고품질(그라디언트·알파 깔끔). sRGB 로 감마 정확 |
| **폰트 커버리지 / 마스크 / 단일 채널** | **BC4** — `DXGI_FORMAT_BC4_UNORM` (linear) | 1채널 4bpp. SDF 폰트로 가도 BC4 유지(샘플만 바뀜) |
| 디버그 폴백 | 무압축 `R8G8B8A8_UNORM(_SRGB)` `.dds` | 압축 파이프라인 없이 눈으로 확인 |

- **BC7 인코딩**은 느리다(고품질 모드) — 빌드타임 오프라인이라 무방(`bc7enc` / `ispc_texcomp`). 증분 빌드라 바뀐 그룹만.
- 소스 알파: cutout(하드 엣지)이면 BC7 이 알파를 잘 보존. 반투명 그라디언트가 많으면 BC7 mode 로도 충분.
- 로더는 최소 DDS 리더 하나면 됨(헤더 + `DX10` 확장 헤더로 `DXGI_FORMAT` + 밉 수 읽고 `CreateTexture2D`). `d3dx`/`DirectXTex` 의존 안 함.

---

## 6. 밉맵 / LOD

### 생성

- **빌드타임 생성** — 패커가 box 또는 Kaiser/Lanczos 필터로 밉을 계산해 `.dds` 서브리소스로 저장. 결정적, 좋은 필터, 로드 히치 없음.
- 로드타임 `GenerateMips` 는 폴백만 — `D3D11_RESOURCE_MISC_GENERATE_MIPS` + RT 바인드 + 드로우(렌더 스레드 업로드 펌프 안, 추가 GPU 작업).

### 아틀라스 특유 문제: 셀 간 블리딩

밉 레벨 N 의 텍셀은 원본 2^N 텍셀 평균 → 이웃 스프라이트가 서로 번진다.

- **gutter = `2^(mips)` 텍셀** (예: mips 3 → ≥ 8px), edge-extend(clamp) 채움 → 번져도 자기 가장자리 색.
- **셀당 깊은 밉 금지** — 셀이 16×16 정도가 되면 그만(그 아래는 셀끼리 섞인 죽). `.dds` 밉 레벨 수를 그에 맞춰 자른다(전체 체인 다 굽지 않음).

### 그룹별 밉 정책

| 그룹 | mips | gutter | 이유 |
|---|---|---|---|
| `ui` | 0 ~ 2 | 2 ~ 4 px | 대개 1:1 근처. 2 레벨은 hi-DPI·해상도 변화 시 축소에 도움 |
| `char` | -1 (셀 16px 에서 컷) | 8 ~ 16 px | 목록의 작은 초상화 ↔ 상세의 큰 초상화 등 스케일 변동 |
| `obj` | 3 ~ 5 | 8 px | 원근에 따라 축소 |
| 배경 / 타일 / 지형 | **아틀라스 안 함** | — | 깊은 축소 + wrap 샘플 필요 → 독립 텍스처 + 자체 풀 밉 체인 |

### LOD (별도 축)

- **품질 설정**: 그룹을 1× 와 (선택) 0.5× / 0.25× 스케일 페이지로 구워, 로드 시 "텍스처 품질"(`game-settings.md`)로 선택. 거칠지만 단순.
- **런타임 mip 클램프**: `SamplerState` `MaxLOD` / mip LOD bias 로 저사양·메모리 압박 시 top 밉 스킵(데이터 덜 스트림, 약간 흐림).
- 런타임 부분 밉 스트리밍(레지던트 밉 테일)은 범위 밖 — 메모.

---

## 7. 빌드 파이프라인 흐름

```
assets/src/<group>/**.png                     (낱장 소스, 커밋)
      │  tools/atlas_pack --group ui                    (또는 --all)
      │    · <group>.cache 와 입력 해시 비교 → 바뀐 그룹만
      │    · 페이지(고정 크기)에 배치 + gutter/edge-extend
      │    · 밉 생성 (그룹 정책)
      │    · BC7(컬러) / BC4(단일채널) 로 압축
      ▼
assets/atlas/<group>.<page>.dds  +  <group>.atlas      (산출물, 커밋 or CI)
      │  loading-and-streaming: AssetKind::Atlas
      │    · IO 워커가 .dds 헤더/밉 파싱 + .atlas 파싱 → CPU AtlasIndex
      │    · 렌더 스레드 업로드 펌프가 ID3D11Texture2D(밉 포함) + SRV → AssetRegistry
      ▼
런타임: texture-atlas-and-sprite-pass.md (SpriteDraw / SpritePass2D / DrawList)
```

- `tools/atlas_pack` 는 엔진 빌드 밖(`tools/entity_memory_bench.cpp` 와 같은 위상). 의존: 이미지 디코드는 **엔진과 같은 계약** — `src/import/ImageFile.cpp` + `src/import/ImageData.cpp` + `src/vendor/stb` 를 그대로 컴파일해 `import::LoadImageFromFile`(RGBA8 top-down straight-alpha, `docs/image-assets.md`) 를 재사용(stb 를 raw 로 다시 부르지 않는다). 그 외: BC7/BC4 인코더(bc7enc / ispc_texcomp), 최소 DDS writer.

---

## 8. 사용 방법 (How to use)

### 새 스프라이트 추가

`assets/src/<group>/` 에 PNG 를 넣고 `tools/atlas_pack --group <group>` 실행 → `<group>.dds` + `<group>.atlas` 갱신. 그 그룹만 다시 구워진다. 다른 그룹·엔진 코드 안 건드림.

### 새 그룹 추가

1. `assets/atlas/atlas.groups` 에 `group "<name>" { inputs=[...]; pageSize=...; mips=...; gutter=... }`.
2. `assets/src/<name>/` 에 소스.
3. `SceneManifest`(`loading-and-streaming.md` §3.3)에 그룹 id 추가(상주면 부팅 매니페스트).
4. 화면/엔티티 조립 코드가 `registry.Atlas("<name>")` → `const AtlasIndex*` 를 받아 씀.

### 텍스처 품질 설정 연결

`game-settings.md` 에 "텍스처 품질"(High/Med/Low) → 로더가 `<group>.dds` / `<group>@0.5.dds` / `<group>@0.25.dds` 중 선택, 또는 `SamplerState MaxLOD` 로 top 밉 스킵.

### 하지 말 것

- 런타임에 아틀라스를 패킹하지 않는다(v1). 빌드 산출물 로드만.
- gutter 없이, 또는 밉 레벨 수보다 작은 gutter 로 패킹하지 않는다(셀 블리딩).
- 페이지 크기를 임의값으로 두지 않는다 — `{1024, 2048, 4096}` 중 하나.
- 아틀라스 파일 포맷·GPU 포맷을 그룹마다 다르게 하지 않는다 — **전 그룹 `.dds` + BC7/BC4 고정**. 크로스플랫폼 분기·트랜스코드 계층을 다시 들이지 않는다(DX 전용).
- 배경·지형·타일링 텍스처를 아틀라스에 넣지 않는다(깊은 밉 + wrap 필요 → 독립 텍스처).
- 서로 다른 주기로 바뀌는 것을 한 그룹에 넣지 않는다 — UI 고칠 때 캐릭터 아틀라스까지 다시 구워진다.
- `tools/atlas_pack` 의 `.cache` 를 커밋하지 않는다(`.gitignore`).
- `JobSystem` 워커에서 `AtlasIndex`/`AssetRegistry` 접근 금지(불변 규칙 6).

---

## 9. 결정 요약

| 항목 | 결정 |
|---|---|
| 타겟 | **DirectX 11 데스크톱 전용.** 모바일·타 백엔드 미고려 → 크로스플랫폼 분기 없음 |
| 그룹핑 | 변경 주기 + 사용 세트로 (`ui` / `char/<name>` / `obj/<cat>` / `scene/<id>`) |
| 재빌드 | 그룹별 증분(입력 해시 캐시) — 매번 전부 안 만듦 |
| 페이지 크기 | 고정 집합 `{1024, 2048, 4096}`, 기본 4096. 그룹이 선언, 타겟 분기 없음 |
| 파일 포맷 | 전 그룹 `.dds` |
| GPU 포맷 | BC7 `*_SRGB`(컬러) / BC4 linear(단일채널) — 전역 고정 |
| 밉 | 빌드타임 생성, 그룹별 레벨(셀 16px 에서 컷), gutter = 2^mips. 품질 설정용 스케일 페이지 / 런타임 `MaxLOD` 는 별도 축 |
| 패킹 시점 | 오프라인(`tools/atlas_pack`), 런타임 로드만. (런타임 빈팩은 UGC/모드 대량 시에만 재고) |

---

## 10. 지을 것

- ~~`tools/atlas_pack.*` — 그룹 매니페스트 파싱, 입력 해시 캐시, 고정 페이지 배치 + gutter/edge-extend, 밉 생성, `.dds` + `.atlas` 쓰기, 디코드는 `import::LoadImageFromFile` 재사용~~ **v1 됨** (`tools/atlas_pack.cpp` + `tools/build_atlas_pack.bat`, 엔진 빌드 밖). 남은 것: **BC7/BC4 압축** (`bc7enc` vendor + `--format bc7`, `ui` 그룹은 무압축 유지 가능), 셀 16px 밉 컷, `--all` 병렬, per-page 패킹 힌트(자주 같이 그리는 것 같은 페이지).
- `assets/atlas/atlas.groups` + 최초 그룹(`ui`, `char/unitychan`) + `assets/src/` 재배치.
- `.gitignore` 에 `assets/atlas/*.cache`.
- 로더: 최소 `.dds` 파서(`DXGI_FORMAT` + 밉 수) + `.atlas` 파서 → `AtlasIndex`. `loading-and-streaming` 의 `AssetKind::Atlas` 경로와 함께.
- `command-playbook.md` #3/#3d·`texture-atlas-and-sprite-pass.md` §1.1·`game-settings.md`(텍스처 품질) 갱신.
