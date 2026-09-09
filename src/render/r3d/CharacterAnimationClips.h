#pragma once

#include <array>

// Demo asset manifest: every animation clip shipped with the Unity-chan model
// (assets/models/unitychan/animation/*.fbx), each its own bones-only FBX (see
// import::LoadAnimationClipsFromFile). Pure data - no ufbx/D3D11 types - so it
// can be included from both render/r3d (ModelMeshPass3D loads these files)
// and game (Simulation cycles through the resulting indices) without either
// module depending on the other's implementation. See
// docs/model-animation-research.md §5, docs/animation-design.md §1.
namespace engine::render
{
    struct CharacterAnimationClipInfo
    {
        const char* displayName;
        const char* fileName;    // relative to the model's directory + "animation/"
        float holdSeconds;       // demo round-robin: how long Simulation plays this before advancing
    };

    inline constexpr std::array<CharacterAnimationClipInfo, 26> kUnityChanClips{ {
        { "wait00",    "unitychan_WAIT00.fbx",     2.5f },
        { "wait01",    "unitychan_WAIT01.fbx",     2.5f },
        { "wait02",    "unitychan_WAIT02.fbx",     2.5f },
        { "wait03",    "unitychan_WAIT03.fbx",     2.5f },
        { "wait04",    "unitychan_WAIT04.fbx",     2.5f },
        { "walk_f",    "unitychan_WALK00_F.fbx",   2.5f },
        { "walk_b",    "unitychan_WALK00_B.fbx",   2.5f },
        { "walk_l",    "unitychan_WALK00_L.fbx",   2.5f },
        { "walk_r",    "unitychan_WALK00_R.fbx",   2.5f },
        { "run_f",     "unitychan_RUN00_F.fbx",    2.5f },
        { "run_l",     "unitychan_RUN00_L.fbx",    2.5f },
        { "run_r",     "unitychan_RUN00_R.fbx",    2.5f },
        { "jump00",    "unitychan_JUMP00.fbx",     2.5f },
        { "jump00b",   "unitychan_JUMP00B.fbx",    2.5f },
        { "jump01",    "unitychan_JUMP01.fbx",     2.5f },
        { "jump01b",   "unitychan_JUMP01B.fbx",    2.5f },
        { "umatobi00", "unitychan_UMATOBI00.fbx",  2.5f },
        { "slide00",   "unitychan_SLIDE00.fbx",    2.5f },
        { "handup_r",  "unitychan_HANDUP00_R.fbx", 2.5f },
        { "damaged00", "unitychan_DAMAGED00.fbx",  2.5f },
        { "damaged01", "unitychan_DAMAGED01.fbx",  2.5f },
        { "win00",     "unitychan_WIN00.fbx",      2.5f },
        { "lose00",    "unitychan_LOSE00.fbx",     2.5f },
        { "refresh00", "unitychan_REFLESH00.fbx",  2.5f },
        { "arpose1",   "unitychan_ARpose1.fbx",    2.5f },
        { "arpose2",   "unitychan_ARpose2.fbx",    2.5f },
    } };
}
