# 툰 렌더링: 셀 셰이딩 · 아웃라인 · 크리즈 라인

`ModelMeshPass3D` 가 임포트한 FBX 모델을 3패스로 그린다.

```
1. 실루엣 아웃라인  outline.hlsl   인버티드 헐, front-cull, 검정 링
2. 셀 셰이딩 표면    cel.hlsl       텍스처 + 4단 램프 + alpha cutout
3. 내부 크리즈 라인  crease.hlsl    import/CreaseLines 로 CPU 생성한 리본, AO 톤
```

셰이더는 `assets/shaders/`, 컴파일은 `ShaderLibrary` (핫리로드됨 — [shader-pipeline.md](shader-pipeline.md)).

---

## 1. 셀 셰이딩 (`cel.hlsl` → `common3d.hlsli::ApplyCelLighting`)

"램프" = key 라이트 항을 **각도로 4등분**한 것. 표면 법선과 광원 방향 사이 각도를 10 / 30 / 50도에서 끊어 4밴드, 검정 → 흰색.

| 각도 | 램프 |
|---|---|
| < 10°           | 1.00 (흰색) |
| 10° ≤ θ < 30°   | 0.66 |
| 30° ≤ θ < 50°   | 0.33 |
| θ ≥ 50°         | 0.00 (검정) |

`최종색 = albedo · (ambientColor + keyColor · 램프 · 세기)`. 조명 값은 `Scene3D::lighting` → `Frame` cbuffer ([lighting.md](lighting.md)). 앰비언트가 있어 가장 어두운 밴드도 `albedo · ambient` (완전 검정 아님) — 순검정 원하면 `BuildLighting` 의 `ambient.color` 를 0으로, 또는 `cel.hlsl` `#define LAMP_FLOOR` 를 올려 그림자 밴드에 색을 남긴다.

> **"시꺼멓다" 였던 이유**: 앰비언트가 없고 광원이 위에서만 와서 카메라 정면 면(법선 ~ -z)이 전부 50° 초과 → 순검정이었다. key 방향을 전상 3/4(`{0.35,-0.55,0.75}`)로 바꾸고 앰비언트를 넣어 해결. 자세히는 [lighting.md](lighting.md).

밴드 경계·개수는 `common3d.hlsli` 의 `ApplyCelLighting` if 체인 (저장 즉시 핫리로드). `"cel"` ↔ `"model"` (`ModelMeshPass3D::Initialize`) 바꾸면 평범한 램버트(`ApplyLighting`)와 비교.

## 2. 실루엣 아웃라인 (`outline.hlsl`, 인버티드 헐)

모델을 한 번 더 그리되 정점을 법선 방향으로 부풀리고 **front face culling** → 부푼 껍질의 뒷면만 남아 실루엣 둘레에 검정 링. 셀 표면보다 **먼저** 그려서(깊이 기록) 모델이 그 위를 덮으면 링만 남는다.

- 두께: `outline.hlsl` 의 `outlineWidth` (b2 cbuffer). 클립공간에서 `w` 로 스케일 → 거리와 무관하게 화면상 일정 두께. 현재 값 `kOutlineWidth = 0.002f` (기존의 반) (`ModelMeshPass3D.cpp`).
- 상태: `m_outlineRasterizer` = `D3D11_CULL_FRONT`.
- **스무딩**: 헐 팽창에 원본 정점 법선이 아니라 `import::BuildSmoothNormals` 로 만든 **위치 기준 welded·면적가중 평균 법선**을 쓴다. hard normal(UV seam)에서 껍질이 갈라져 각지는 걸 막아 외곽선이 부드럽게 이어진다. `ModelMeshPass3D` 가 서브메시마다 `pos + smoothNormal`(stride 24) 헐 VB 를 만들고(인덱스는 모델과 공유), 아웃라인 패스가 그걸로 그린다.
- 서브픽셀 계단: 씬 타깃이 MSAA(최대 8x, `docs/msaa.md`)라 실루엣 엣지가 부드럽게 그려진다. 셀 밴드 경계는 셰이딩 단차라 MSAA 밖 — `smoothstep` 또는 포스트 AA.

대안(선택 안 함): 포스트프로세스 엣지 검출(오프스크린 RT 필요), 림/프레넬(1패스, 두께 불균일). [command-playbook](command-playbook.md) 3g 참조.

## 3. 내부 크리즈 라인 (`import/CreaseLines`, `crease.hlsl`)

**두 면의 법선각이 임계값을 넘는 에지**에 서피스 리본을 CPU 로 생성. GPU 지오메트리 셰이더 없음 — 모든 계산이 로드 시 1회.

### 알고리즘 (`BuildCreaseLines`)

1. 정점을 위치로 weld (임포터가 정점을 전부 분리해두므로 위치 해시로 공유 에지를 찾음).
2. 삼각형별 **기하 법선**(정점 법선 아님) + centroid UV 로 텍스처 샘플한 **면 중심색**.
3. 정확히 2개 삼각형이 공유하는 에지마다: `angle = acos(dot(n_a, n_b))`.
4. `angle > thresholdDegrees` (기본 **90°**) 이면 리본 생성:
   - **두께**: `angle` 이 임계값→180° 로 갈수록 `minHalfWidth`(≈1.3mm) → `maxHalfWidth`(≈6.7mm), 모델 공간. 각질수록 굵다.
   - **색**: 좌·우 면 중심색 평균 → 채도 `×0.65` + 명도 `×0.80` → AO 처럼.
   - **형태**: 에지를 따라 `side = cross(edgeDir, foldNormal)` 방향으로 폭만큼 확장한 쿼드(삼각형 2개), 표면에서 `surfaceOffset`(1.5mm) 만큼 띄움.
5. 모든 서브메시 리본을 한 VB 로 합침. `crease.hlsl` 은 변환 + 정점색 통과, 깊이 `LESS_EQUAL` + 기록 off.

### Unity-chan 세그먼트 수 (fbx_probe 측정)

| 임계각 | 세그먼트 |
|---:|---:|
| > 90° | 382 |
| > 60° | 1,112 |
| > 45° | 1,795 |
| > 30° | 3,078 |
| > 20° | 5,152 |

90°는 손가락 관절·옷단·부츠 이음새 같은 **정말 각진 부분**만. 내부 라인을 더 촘촘히 원하면 `thresholdDegrees` 를 낮춘다.

### 튜닝 파라미터 (`import::CreaseOptions`, `ModelMeshPass3D::LoadModel` 에서 `{}` 로 전달)

| 필드 | 기본 | 의미 |
|---|---:|---|
| `thresholdDegrees` | 90 | 이 각도 초과에서만 라인 |
| `minHalfWidth` / `maxHalfWidth` | 0.00133 / 0.00667 | 임계각/180°에서의 반폭(모델 공간 m). 기존 3배에서 1/3 로 낮춤 |
| `saturationScale` | 0.65 | <1 채도 낮춤 |
| `valueScale` | 0.80 | <1 어둡게 (AO 느낌) |
| `surfaceOffset` | 0.0015 | 표면에서 띄우는 거리(z-fight 방지) |
| `weldEpsilon` | 1e-4 | 이 거리 이내는 같은 정점 |
| `maxVertices` | 600k | 안전 상한 |

## 알려진 한계 / 다음

- **미검증(시각)**: 이 환경은 GPU 없음. 빌드·로드·세그먼트 수만 확인. 실제 모양은 F5.
- 크리즈 리본이 월드 공간 일정 폭이라 멀어지면 얇아짐 (모델에 그려진 잉크 느낌 — 의도). 화면 일정 폭 원하면 `crease.hlsl` 에서 확장하도록 바꿔야 함(현재는 CPU 확장).
- 스킨드 애니메이션 붙으면 크리즈 리본도 본을 따라야 함 → 리본 정점에 본 인덱스/가중치를 상속시키거나, 매 프레임 재생성.
- 아웃라인·크리즈가 반투명 머티리얼(머리카락)과 겹칠 때 정렬 이슈 가능.
- 크리즈 색을 텍스처가 아닌 **셀 셰이딩 결과**의 평균으로 하면 조명에 반응하는 AO가 됨 (지금은 라이팅 무관 고정색).

## 사용 방법 (How to use)

**두께·임계·색 조정**: `assets/shaders/cel.hlsl` (램프 밴드), `ModelMeshPass3D.cpp` 의 `kOutlineWidth` (아웃라인), `import::CreaseOptions` (크리즈 — 위 표). `.hlsl` 은 저장 즉시 핫리로드, 나머지는 재빌드.

**다른 모델에 적용**: `ModelMeshPass3D` 는 생성자 경로의 FBX 하나를 그린다. 크리즈는 자동 생성됨. 텍스처는 `MaterialToTga` 이름 테이블이 Unity-chan 전용이라 다른 모델은 FBX 의 텍스처 ref(→ basename `.tga`)에 의존하거나 테이블을 늘려야 함.

**아웃라인만/크리즈만 끄기**: `ModelMeshPass3D::Execute` 의 해당 패스 블록을 스킵. (토글 필드는 아직 없음 — 필요하면 추가.)

**하지 말 것**: 크리즈 빌드를 매 프레임 호출(로드 1회), `crease.hlsl` 에서 `objColor` 를 색으로 사용(정점색을 씀), 아웃라인 패스를 셀 패스 뒤에 그리기(링이 안 생김).
