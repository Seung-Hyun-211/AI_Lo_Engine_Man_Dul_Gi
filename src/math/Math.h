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

    // ---------------------------------------------------------------------------
    // 3D. World space is left-handed: +x right, +y up, +z into the screen.
    // ---------------------------------------------------------------------------

    struct Vec3
    {
        float x{};
        float y{};
        float z{};
    };

    [[nodiscard]] inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    [[nodiscard]] inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    [[nodiscard]] inline Vec3 operator*(Vec3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }

    [[nodiscard]] inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    [[nodiscard]] inline Vec3 Cross(Vec3 a, Vec3 b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    [[nodiscard]] inline float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
    [[nodiscard]] inline Vec3 Normalized(Vec3 v)
    {
        const float length = Length(v);
        return length > 1e-6f ? Vec3{ v.x / length, v.y / length, v.z / length } : Vec3{};
    }

    // Row-major 4x4. Vectors are rows: a point is transformed as `p * M`, and
    // matrices compose left-to-right (World * View * Projection). Uploaded to
    // HLSL as-is against a `row_major float4x4` so no transpose is needed.
    struct Mat4
    {
        // m[row * 4 + col]
        float m[16]{ 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };

        [[nodiscard]] static Mat4 Identity() { return Mat4{}; }
    };

    [[nodiscard]] inline Mat4 operator*(const Mat4& a, const Mat4& b)
    {
        Mat4 r{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                r.m[row * 4 + col] =
                    a.m[row * 4 + 0] * b.m[0 * 4 + col] +
                    a.m[row * 4 + 1] * b.m[1 * 4 + col] +
                    a.m[row * 4 + 2] * b.m[2 * 4 + col] +
                    a.m[row * 4 + 3] * b.m[3 * 4 + col];
        return r;
    }

    [[nodiscard]] inline Mat4 Translation(Vec3 t)
    {
        Mat4 r{};
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    [[nodiscard]] inline Mat4 Scaling(Vec3 s)
    {
        Mat4 r{};
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }

    [[nodiscard]] inline Mat4 RotationX(float radians)
    {
        const float c = std::cos(radians), s = std::sin(radians);
        Mat4 r{};
        r.m[5] = c;  r.m[6] = s;
        r.m[9] = -s; r.m[10] = c;
        return r;
    }

    [[nodiscard]] inline Mat4 RotationY(float radians)
    {
        const float c = std::cos(radians), s = std::sin(radians);
        Mat4 r{};
        r.m[0] = c; r.m[2] = -s;
        r.m[8] = s; r.m[10] = c;
        return r;
    }

    [[nodiscard]] inline Mat4 RotationZ(float radians)
    {
        const float c = std::cos(radians), s = std::sin(radians);
        Mat4 r{};
        r.m[0] = c;  r.m[1] = s;
        r.m[4] = -s; r.m[5] = c;
        return r;
    }

    // Left-handed view matrix (D3D convention).
    [[nodiscard]] inline Mat4 LookAtLH(Vec3 eye, Vec3 target, Vec3 up)
    {
        const Vec3 zAxis = Normalized(target - eye);
        const Vec3 xAxis = Normalized(Cross(up, zAxis));
        const Vec3 yAxis = Cross(zAxis, xAxis);
        Mat4 r{};
        r.m[0] = xAxis.x; r.m[1] = yAxis.x; r.m[2] = zAxis.x; r.m[3] = 0;
        r.m[4] = xAxis.y; r.m[5] = yAxis.y; r.m[6] = zAxis.y; r.m[7] = 0;
        r.m[8] = xAxis.z; r.m[9] = yAxis.z; r.m[10] = zAxis.z; r.m[11] = 0;
        r.m[12] = -Dot(xAxis, eye); r.m[13] = -Dot(yAxis, eye); r.m[14] = -Dot(zAxis, eye); r.m[15] = 1;
        return r;
    }

    // Left-handed perspective, depth mapped to [0, 1] (D3D convention).
    [[nodiscard]] inline Mat4 PerspectiveFovLH(float fovYRadians, float aspect, float nearZ, float farZ)
    {
        const float yScale = 1.0f / std::tan(fovYRadians * 0.5f);
        const float xScale = yScale / aspect;
        Mat4 r{};
        r.m[0] = xScale; r.m[1] = 0; r.m[2] = 0; r.m[3] = 0;
        r.m[4] = 0; r.m[5] = yScale; r.m[6] = 0; r.m[7] = 0;
        r.m[8] = 0; r.m[9] = 0; r.m[10] = farZ / (farZ - nearZ); r.m[11] = 1;
        r.m[12] = 0; r.m[13] = 0; r.m[14] = -nearZ * farZ / (farZ - nearZ); r.m[15] = 0;
        return r;
    }
}
