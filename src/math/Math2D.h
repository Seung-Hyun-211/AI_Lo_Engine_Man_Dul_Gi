#pragma once

#include <algorithm>
#include <cmath>

// 2D math module. Always available (the UI and the 2D render path depend on it).
// Peer of math/Math3D.h; neither includes the other. Include math/Math.h for the
// umbrella that also pulls in 3D when ENGINE_WITH_3D is defined.
namespace engine::math
{
    // Screen/world coordinate. Pixels, origin top-left, +y down (Win32 client
    // space, and the 2D sprite shader).
    struct Vec2
    {
        float x{};
        float y{};
    };

    [[nodiscard]] inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
    [[nodiscard]] inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
    [[nodiscard]] inline Vec2 operator*(Vec2 v, float s) { return { v.x * s, v.y * s }; }

    [[nodiscard]] inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
    [[nodiscard]] inline float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }

    // v scaled to unit length, or zero when v is (near) zero. Keeps diagonal
    // input the same speed as axis-aligned input.
    [[nodiscard]] inline Vec2 Normalized(Vec2 v)
    {
        const float length = Length(v);
        return length > 1e-6f ? Vec2{ v.x / length, v.y / length } : Vec2{};
    }

    struct Rect
    {
        float x{};
        float y{};
        float width{};
        float height{};

        [[nodiscard]] bool Contains(Vec2 point) const
        {
            return point.x >= x && point.x < x + width &&
                   point.y >= y && point.y < y + height;
        }

        [[nodiscard]] Vec2 Origin() const { return { x, y }; }
    };

    // Linear, non-premultiplied RGBA in 0..1. Alpha < 1 is honoured by the 2D
    // pass's SrcAlpha / InvSrcAlpha blend state.
    struct Color
    {
        float r{};
        float g{};
        float b{};
        float a{ 1.0f };
    };

    [[nodiscard]] inline float Clamp(float value, float low, float high)
    {
        return std::clamp(value, low, high);
    }
}
