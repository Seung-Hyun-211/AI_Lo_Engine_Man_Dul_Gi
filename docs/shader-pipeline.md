# 셰이더 자산 파이프라인 (Shader Asset Pipeline)

`src/render/shader/` + `assets/shaders/`. 셰이더를 각 패스의 인라인 C 문자열에서 **디스크의 `.hlsl` 파일**로 옮기고, 컴파일·캐시·핫리로드를 한곳에서 한다.

## 왜

- 셰이더를 고치려고 C++ 를 재컴파일할 필요가 없다 (실행 중 `.hlsl` 저장 → 다음 프레임 반영).
- 공통 코드(카메라·조명 cbuffer, 조명 함수)를 `.hlsli` 로 공유. 복붙 제거.
- 패스 코드가 짧아진다 — `D3DCompile` + `CreateVertexShader/PixelShader/InputLayout` 보일러플레이트가 사라지고 `shaders.Get(...)` 한 줄.

## 구성

```text
assets/shaders/
  common3d.hlsli   Frame(b0: viewProj + key/ambient 조명) / Object(b1) cbuffer
                   + ApplyLighting() / ApplyCelLighting()   ← 조명은 docs/lighting.md
  mesh.hlsl        MeshPass3D       (pos+normal, 평면 색, 램버트)
  model.hlsl       ModelMeshPass3D  (pos+normal+uv, 텍스처 + 램버트) — 대안
  cel.hlsl         ModelMeshPass3D  (pos+normal+uv, 텍스처 + 4밴드 셀)  ← 기본
  outline.hlsl     ModelMeshPass3D  (pos+normal, 인버티드 헐 실루엣)
  crease.hlsl      ModelMeshPass3D  (pos+color, CPU 생성 크리즈 리본)
  quad2d.hlsl      QuadPass2D       (스크린 공간 pos+color)
  ← 툰 3종(cel/outline/crease) 세부: docs/toon-rendering.md

src/core/AssetPaths.{h,cpp}          작업 디렉터리와 무관하게 assets/ 루트를 찾음
src/render/shader/ShaderLibrary.{h,cpp}   .hlsl -> ShaderProgram{vs, ps, inputLayout}, 캐시 + 핫리로드
src/render/r3d/FrameConstants.h      Frame cbuffer(b0) 의 C++ 레이아웃 + FillFrameConstants()
```

### `AssetPaths`

`core::ResolveAsset("shaders/mesh.hlsl")` → 절대경로. 최초 호출 시 자산 루트를 한 번 찾아 캐시한다: **작업 디렉터리**와 **실행 파일 디렉터리**에서 위로 최대 6단계 올라가며 `assets/` 폴더를 찾는다. F5(작업 디렉터리 = 저장소 루트)와 exe 직접 실행(`x64/Debug/`) 둘 다 동작. 못 찾으면 입력 경로를 그대로 반환. `ModelMeshPass3D` 의 텍스처 로딩도 이걸 쓴다 (기존 `../../` 하드코딩 대체 예정).

### `ShaderLibrary`

```cpp
class ShaderLibrary {
    const ShaderProgram* Get(device, name, layout, layoutCount);  // <name>.hlsl 컴파일 + 캐시
    void PollHotReload(device);   // 프레임마다: 바뀐 파일 재컴파일
    void ReleaseAll();
};
struct ShaderProgram { ID3D11VertexShader* vs; ID3D11PixelShader* ps; ID3D11InputLayout* inputLayout; };
```

- `Dx11Renderer` 가 멤버로 하나 소유. `IRenderPass::Initialize(device, ShaderLibrary&)` 로 넘긴다.
- **엔트리 포인트 규약**: `VSMain` / `PSMain`, 셰이더 모델 `vs_5_0` / `ps_5_0`.
- `#include "x.hlsli"` 는 `assets/shaders/` 기준으로 해석 (`ID3DInclude` 구현).
- `Get()` 은 `const ShaderProgram*` 를 반환하고 그 **주소는 고정**이다. 핫리로드 시 내부 포인터(vs/ps/inputLayout)만 교체되므로 패스는 `m_shader` 를 들고 매 프레임 `m_shader->vs` 만 읽으면 된다.
- **첫 컴파일 실패는 throw** (시작 오류로 표면화). **핫리로드 실패는 이전 버전 유지** + `OutputDebugString` 로그, 다음 파일 수정 때까지 재시도 안 함.
- `_DEBUG` 빌드는 `D3DCOMPILE_DEBUG | SKIP_OPTIMIZATION`.
- 입력 레이아웃도 라이브러리가 소유·재생성한다 (셰이더 서명이 바뀌어도 리로드가 맞춘다). semantic name 포인터는 문자열 리터럴이어야 한다(패스가 그렇게 넘김).

### 핫리로드 흐름

```text
Dx11Renderer::Render  (프레임마다, 렌더 스레드)
  m_shaders.PollHotReload(device)
    for each cached shader:
      파일 write-time 바뀌었나? → 재컴파일
        성공 → 옛 vs/ps/layout Release, 새 것으로 교체, "reloaded" 로그
        실패 → 그대로 두고 "reload failed" 로그
```

파일 몇 개 `GetFileAttributesExA` 하는 비용이라 매 프레임 호출해도 무시할 수준.

## 불변 규칙

1. 패스는 셰이더/입력레이아웃을 **직접 만들지 않는다**. `ShaderLibrary::Get` 으로만. 소유권도 라이브러리.
2. 패스는 `const ShaderProgram*` 를 보관하고 매 프레임 `->vs/->ps/->inputLayout` 를 읽는다. 캐싱 금지(리로드 시 stale).
3. 3D 패스 셰이더는 `common3d.hlsli` 의 `b0`(Frame) / `b1`(Object) 레이아웃을 지킨다. 새 cbuffer 는 `b2+`.
4. `.hlsl` / `.hlsli` 는 소스다 — 저장소에 커밋. `assets/shaders/` 없이 실행하면 시작 시 throw.
5. 셰이더 로딩·리로드는 렌더 스레드에서만 (`Initialize`, `PollHotReload`).

## 다음 (미구현)

- 셰이더 순열/디파인 (`ShaderLibrary::Get` 에 `D3D_SHADER_MACRO*`): 스키닝 on/off, 라이트 개수 등.
- 바이트코드 디스크 캐시 (`.cso`) — 첫 실행 컴파일 시간 단축.
- 컴퓨트 셰이더 (`CSMain`, `cs_5_0`).
- `PassContext` 에 리로드 콜백 — 셰이더가 바뀌면 패스가 파생 리소스(예: 상수 버퍼 크기) 재생성.
- Release 빌드에서 핫리로드 컴파일 비활성 (파일 없으면 어차피 no-op).

## 사용 방법 (How to use)

### 기존 패스의 셰이더 수정

`assets/shaders/<name>.hlsl` 을 편집하고 저장. 앱 실행 중이면 다음 프레임에 반영(콘솔/디버그 출력에 `ShaderLibrary: reloaded <name>`). 컴파일 에러면 이전 셰이더 유지 + `reload failed` 로그, 고쳐서 다시 저장하면 반영.

### 새 패스에서 셰이더 쓰기

```cpp
// 1) assets/shaders/myeffect.hlsl 작성 (VSMain / PSMain, 필요하면 #include "common3d.hlsli")

// 2) 패스 헤더
#include "render/RenderPass.h"
struct engine::render::ShaderProgram;   // 전방 선언
class MyPass : public IRenderPass {
    void Initialize(ID3D11Device* device, ShaderLibrary& shaders) override;
    const ShaderProgram* m_shader{};
};

// 3) 패스 .cpp
#include "render/shader/ShaderLibrary.h"
void MyPass::Initialize(ID3D11Device* device, ShaderLibrary& shaders) {
    const D3D11_INPUT_ELEMENT_DESC layout[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, /* ... */ };
    m_shader = shaders.Get(device, "myeffect", layout, ARRAYSIZE(layout));
    // ... 상수 버퍼 / 상태 객체 생성 ...
}
void MyPass::Execute(const PassContext& ctx) {
    if (m_shader == nullptr) return;
    ctx.context->IASetInputLayout(m_shader->inputLayout);
    ctx.context->VSSetShader(m_shader->vs, nullptr, 0);
    ctx.context->PSSetShader(m_shader->ps, nullptr, 0);
    // ...
}
void MyPass::Release() { m_shader = nullptr; /* 라이브러리 소유 */ /* ... 나머지 Release ... */ }
```

`main.cpp` 에서 `renderer.AddRenderPass(std::make_unique<MyPass>())` (Start 전).

### 새 리소스 파일을 assets/ 에서 찾기

```cpp
#include "core/AssetPaths.h"
const std::string path = engine::core::ResolveAsset("models/foo.bin");   // "assets/" 접두는 생략 가능
```

### 하지 말 것

- 패스에서 `D3DCompile` 직접 호출. `.hlsl` 파일 + `ShaderLibrary::Get`.
- `m_shader->vs` 같은 포인터를 멤버에 캐싱 (리로드하면 무효).
- `b0`/`b1` 레지스터를 3D 패스에서 다른 용도로 재사용.
- `.hlsl` 을 `<ClCompile>` 로 vcxproj 에 추가 (FXC 가 빌드 때 컴파일하려 함) — `<None>` 으로.
