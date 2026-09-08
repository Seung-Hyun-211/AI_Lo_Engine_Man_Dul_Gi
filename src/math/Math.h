#pragma once

// Umbrella header. The 2D module is always present; the 3D module is pulled in
// only when the build asks for it. Code that needs 3D types unconditionally
// (the 3D render path, the 3D collision module) includes math/Math3D.h directly.
//
//   ENGINE_WITH_3D   defined  -> Vec3 / Mat4 / ... available through this header
//                    undefined-> only the 2D types are visible

#include "math/Math2D.h"

#if defined(ENGINE_WITH_3D)
#include "math/Math3D.h"
#endif
