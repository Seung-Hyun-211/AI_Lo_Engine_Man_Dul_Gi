# Cpp Window Game

Small Windows 2D game foundation using **C++20, Win32, and DirectX 11**.

## Run

Open `CppWindowGame.vcxproj` with Visual Studio 2022, select **Debug | x64**, then press `F5`.

## Current features

- Win32 game window and non-blocking game loop
- Main Thread와 Render Thread가 분리된 프레임 구조
- Render Thread가 DX11 device, immediate context, swap chain, resize, `Present`를 단독 소유
- 최신 프레임 한 개만 보관하는 mailbox: 렌더가 늦어도 게임 스레드는 오래된 프레임을 쌓지 않음
- 값 기반 `RenderSnapshot`: 렌더러가 가변 게임 객체를 직접 읽지 않음
- Delta-time movement (stable movement speed across frame rates)
- DirectX 11 swap chain and resize-safe rendering
- A cyan 64×64 player square rendered by a tiny shader pipeline
- Arrow-key movement, normalized diagonal speed, and window-boundary clamping
- A retained UI tree: in-game window, interactive Start button, and text lines

## Thread contract

```text
Main Thread: Win32 message → Input → Update → RenderSnapshot → Submit
                                                         ↓
Render Thread:                   latest-frame mailbox → DX11 draw → Present
```

`src/main.cpp`은 입력과 시뮬레이션만 처리한다. DX11 API 호출은 `src/render/Dx11Renderer.cpp`의 Render Thread에만 있다. 다음 JobSystem은 simulation 완료 Fence 뒤에 snapshot을 생성하는 위치에 연결하면 된다.

전체 멀티스레드 계약과 로드맵은 [docs/multithreaded_game_engine_architecture.md](docs/multithreaded_game_engine_architecture.md)에 있다.

## Suggested next milestones

1. Load a PNG texture and draw it instead of the colored square.
2. Add a `Vector2` type and separate `Input`, `Graphics`, and `Player` classes into files.
3. Add sprite animation, a camera, tile maps, and AABB collisions.

## UI design

The proposed UI layer for in-game windows, buttons, and text is documented in [docs/ui-architecture.md](docs/ui-architecture.md). It keeps UI widgets independent from DirectX so the renderer can later move from DX11 to DX12.
