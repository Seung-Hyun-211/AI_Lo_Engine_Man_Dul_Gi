#pragma once

#include <cmath>

// 3D math module. Built and used only where 3D is wanted (the 3D render path,
// the 3D collision module). Peer of math/Math2D.h; neither includes the other.
// World space is left-handed: +x right, +y up, +z into the screen.
namespace engine::math
{
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

    // Row-major 4x4. Vectors are rows: a point transforms as `p * M`, matrices
    // compose left-to-right (World * View * Projection). Uploaded to HLSL as-is
    // against a `row_major float4x4`, so no transpose.
    struct Mat4
    {
        float m[16]{ 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };  // m[row * 4 + col]
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

    // Unit quaternion (x, y, z, w). Used for animation keyframe rotations and
    // for blending poses (nlerp / slerp).
    struct Quat
    {
        float x{};
        float y{};
        float z{};
        float w{ 1.0f };

        [[nodiscard]] static Quat Identity() { return Quat{}; }
    };

    [[nodiscard]] inline float Dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    [[nodiscard]] inline Quat Normalized(Quat q)
    {
        const float length = std::sqrt(Dot(q, q));
        if (length < 1e-6f) return Quat::Identity();
        const float inv = 1.0f / length;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }

    // Spherical linear interpolation, shortest arc. `t` in [0, 1].
    [[nodiscard]] inline Quat Slerp(Quat a, Quat b, float t)
    {
        float cosom = Dot(a, b);
        if (cosom < 0.0f) { cosom = -cosom; b = { -b.x, -b.y, -b.z, -b.w }; }

        float sa = 1.0f - t;
        float sb = t;
        if (cosom < 0.9995f)
        {
            const float omega = std::acos(cosom);
            const float invSin = 1.0f / std::sin(omega);
            sa = std::sin(sa * omega) * invSin;
            sb = std::sin(sb * omega) * invSin;
        }
        return Normalized({ a.x * sa + b.x * sb, a.y * sa + b.y * sb,
                            a.z * sa + b.z * sb, a.w * sa + b.w * sb });
    }

    // Row-major rotation matrix for a unit quaternion (row-vector convention).
    [[nodiscard]] inline Mat4 QuatToMat4(Quat q)
    {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat4 r{};
        r.m[0] = 1.0f - 2.0f * (yy + zz); r.m[1] = 2.0f * (xy + wz);       r.m[2] = 2.0f * (xz - wy);
        r.m[4] = 2.0f * (xy - wz);        r.m[5] = 1.0f - 2.0f * (xx + zz); r.m[6] = 2.0f * (yz + wx);
        r.m[8] = 2.0f * (xz + wy);        r.m[9] = 2.0f * (yz - wx);        r.m[10] = 1.0f - 2.0f * (xx + yy);
        return r;
    }

    // Translation * Rotation * Scale as a single row-major matrix
    // (applied to a row vector as scale, then rotate, then translate).
    [[nodiscard]] inline Mat4 ComposeTRS(Vec3 translation, Quat rotation, Vec3 scale)
    {
        return Scaling(scale) * QuatToMat4(rotation) * Translation(translation);
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
