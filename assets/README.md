# assets/

런타임 리소스. 게임 코드는 여기 상대경로로 리소스를 참조한다.

```
assets/
  models/    FBX 모델 (테스트/데모)
```

## 테스트 FBX 넣는 곳

`assets/models/` 에 `.fbx` 파일을 두면 된다. 예: `assets/models/test.fbx`.

로드 코드 (`docs/model-animation-research.md` §7 참고):

```cpp
#include "import/ModelImporter.h"

auto result = engine::import::LoadModelFromFile("assets/models/test.fbx");
if (!result.ok) { /* result.error 로그 */ }
else { engine::import::Model& model = result.model; /* ... */ }
```

## 작업 디렉터리 주의

- **VS 디버깅(F5)**: 작업 디렉터리 기본값이 `$(ProjectDir)`(저장소 루트)라 `"assets/models/test.fbx"` 가 그대로 맞다.
- **exe 직접 실행**(`x64/Debug/CppWindowGame.exe`): 작업 디렉터리가 `x64/Debug/` 이므로 `"../../assets/models/test.fbx"` 로 접근하거나, 나중에 리소스 경로 해석기(`GetModuleFileName` 기준 루트 탐색)를 두는 게 좋다.

## 커밋 정책

작은 테스트 에셋(수백 KB 이하의 리깅된 큐브 등)은 저장소에 커밋해도 된다. 큰 에셋(수 MB+)은 `.gitignore` 에 패턴 추가 후 별도 관리.
