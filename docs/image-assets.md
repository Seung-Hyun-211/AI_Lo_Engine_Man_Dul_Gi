# 이미지 에셋 처리 (Image assets)

디코드·색공간·dev/ship 경계의 **한 곳**. 아틀라스 빌드/런타임 소비는 별도 문서가 계약이고
(`atlas-build-pipeline.md`, `texture-atlas-and-sprite-pass.md`, `loading-and-streaming.md`),
이 문서는 그 셋이 공유하는 "픽셀이 파일에서 메모리로 들어오는 방식" 만 정한다.

---

## 1. 디코드 계약 — 진입점은 하나

```cpp
engine::import::ImageData  engine::import::LoadImageFromFile(const std::string& path);
```

- `ImageData` = `{ bool ok; int width, height; std::vector<uint8_t> rgba; }` — **RGBA8, top-down 행 순서, straight(non-premultiplied) alpha**. `src/import/ImageData.h`.
- 디스패치: `.tga` → 내장 `LoadTga`(`ImageData.cpp`, 무압축 24/32, 서드파티 의존 0) · `.png .jpg .jpeg .bmp .gif` → `stb_image`(`src/vendor/stb`, `src/import/ImageFile.cpp`).
- **이게 유일한 이미지 디코드 seam.** 런타임 코드도, 오프라인 `tools/atlas_pack` 도 이 함수를 부른다 — `stb_*` 를 `ImageFile.cpp` 밖에서 직접 호출하지 않는다(정규화 로직 중복 금지, CLAUDE.md 규칙 9).
- 스레딩: 디코드는 CPU 전용, 메인 스레드 밖에서도 호출 가능(장차 `AssetLoader`). SRV 생성은 렌더 스레드만(규칙 1·2).

### 새 포맷 추가

`stb_image_impl.cpp` 에 `#define STBI_ONLY_<FMT>` 추가 + `ImageFile.cpp` 디스패치에 확장자 추가. 그 외 코드는 안 바뀐다.

---

## 2. dev / ship 경계

| | loose 이미지(`.png` `.jpg` …) | `.tga` | `.dds` (+ `.atlas`) |
|---|---|---|---|
| 오프라인 `tools/atlas_pack` 입력 | ✅ 주 입력 | ✅ | — |
| 런타임 dev 편의(비아틀라스 텍스처: 모델 디퓨즈, 빠른 테스트) | ✅ | ✅ (모델 파이프라인 기본) | ✅ |
| **배포 런타임** | ❌ | ❌ | ✅ **이것만** |

- 배포 빌드의 런타임은 `.dds`(+`.atlas` 매니페스트)만 로드한다 — `atlas-build-pipeline.md` "런타임은 로드만", 런타임 패킹 없음(v1).
- loose 이미지 런타임 디코드(`LoadImageFromFile`)는 **오프라인 패커 + dev 비아틀라스 경로** 용도. 셰이더 핫리로드처럼 "개발 편의, 배포엔 없음".
- `.tga` 는 모델 파이프라인의 축복받은 dev 텍스처 포맷(작은 자체 디코더, 모델 경로에 서드파티 의존 0). 배포 시엔 모델 텍스처도 `.dds` 로.

---

## 3. 색공간 · 알파

- `ImageData.rgba` 는 **파일 바이트 그대로** — 색공간 변환 없음. 감마 처리는 **SRV 포맷** 으로 소비자가 정한다.
- 규칙:
  - albedo / diffuse / UI 색 이미지 → `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` (BC7 은 `BC7_UNORM_SRGB`)
  - normal / roughness / mask / packed-data → `..._UNORM` (linear)
- `math::Color`(틴트)는 linear · straight-alpha(`Math2D.h` 참조). 틴트 곱은 linear 에서.
- 전체 sRGB/linear 렌더 파이프라인(sRGB 백버퍼)은 아직 미구현(`lighting.md`). 그전까지 SRV 포맷 선택이 유일한 sRGB 레버.
- **premultiplied alpha 아님.** stb `stbi_set_unpremultiply_on_load` 는 쓰지 않는다. 블렌드는 straight-alpha(`Dx11Renderer` blend state).

## 4. 방향

top-down 이 엔진 규약. `LoadTga` 는 bottom-origin TGA 를 뒤집고, stb 는 기본이 top-down(`stbi_set_flip_vertically_on_load` 안 씀). 아틀라스 패커도 top-down 유지.

---

## 사용 방법 (How to use)

### 이미지 한 장 읽기

```cpp
#include "import/ImageFile.h"

const engine::import::ImageData img = engine::import::LoadImageFromFile("assets/ui/icon_sword.png");
if (img.ok)
{
    // img.width, img.height, img.rgba (width*height*4, RGBA8 top-down straight alpha)
}
```

### 그걸로 텍스처 만들기 (렌더 스레드)

```cpp
D3D11_TEXTURE2D_DESC d{};
d.Width = img.width; d.Height = img.height; d.MipLevels = 1; d.ArraySize = 1;
d.Format = isColor ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB     // §3: albedo/UI
                   : DXGI_FORMAT_R8G8B8A8_UNORM;         //      normal/mask/data
d.SampleDesc.Count = 1;
d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
D3D11_SUBRESOURCE_DATA init{ img.rgba.data(), (UINT)img.width * 4, 0 };
device->CreateTexture2D(&d, &init, &tex);
```

`ModelMeshPass3D::CreateTextureSrv` (디퓨즈 = 색) · `SpritePass2D` (아틀라스 = 색) 가 예시.

### 하지 말 것

- `ImageFile.cpp` 밖에서 `stb_*` / 다른 디코더를 직접 부르지 않는다.
- premultiplied alpha 를 가정하지 않는다.
- 배포되는 코드 경로에서 loose 이미지(`.png` 등)를 런타임에 로드하지 않는다 — 아틀라스(`.dds`).
- 색 이미지를 `_UNORM`(linear) 로 샘플하지 않는다(어둡게 뜬다). 데이터 맵을 `_SRGB` 로 샘플하지 않는다.
