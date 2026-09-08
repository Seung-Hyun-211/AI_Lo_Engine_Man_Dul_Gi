#pragma once

#include <algorithm>
#include <cmath>

namespace engine::math
{
    // 2D screen/world coordinate. Pixels, origin at the top-left, +y downward,
    // matching Win32 client coordinates and the sprite vertex shader.
    struct Vec2
    {
        float x{};
        float y{};
    };

    [[nodiscard]] inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
    [[nodiscard]] inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
    [[nodiscard]] inline Vec2 operator*(Vec2 v, float s) { return { v.x * s, v.y * s }; }

    [[nodiscard]] inline float Length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

    // Returns v scaled to unit length, or the zero vector when v is (near) zero.
    // Used to keep diagonal input the same speed as axis-aligned input.
    [[nodiscard]] inline Vec2 Normalized(Vec2 v)
    {
        const float length = Length(v);
        return length > 1e-6f ? Vec2{ v.x / length, v.y / length } : Vec2{};
    }

    // Axis-aligned rectangle in the same coordinate space as Vec2.
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

    // Linear, non-premultiplied RGBA in the 0..1 range. Alpha < 1 is honoured by
    // the renderer's SrcAlpha / InvSrcAlpha blend state.
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
