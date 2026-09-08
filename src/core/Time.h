#pragma once

#include <algorithm>
#include <chrono>

namespace engine::core
{
    // Wall-clock delta source for the main loop. One instance, ticked once per
    // frame. The delta is clamped so a breakpoint or a stalled frame cannot make
    // the simulation jump (or the FixedTimestep accumulator explode).
    class FrameClock
    {
    public:
        // Seconds elapsed since the previous Tick(), clamped to kMaxDelta.
        [[nodiscard]] float Tick()
        {
            const auto now = std::chrono::steady_clock::now();
            const float delta = std::chrono::duration<float>(now - m_previous).count();
            m_previous = now;
            return std::min(delta, kMaxDelta);
        }

    private:
        static constexpr float kMaxDelta = 0.1f;
        std::chrono::steady_clock::time_point m_previous{ std::chrono::steady_clock::now() };
    };

    // Fixed-timestep accumulator. Feed it the variable frame delta; it returns
    // how many fixed steps the simulation should run this frame so gameplay and
    // physics stay deterministic regardless of frame rate. Capped per frame to
    // avoid the "spiral of death" when a frame runs long.
    class FixedTimestep
    {
    public:
        explicit FixedTimestep(float stepSeconds = 1.0f / 60.0f) : m_step(stepSeconds) {}

        [[nodiscard]] int Advance(float deltaSeconds)
        {
            m_accumulator += deltaSeconds;
            int steps = 0;
            while (m_accumulator >= m_step && steps < kMaxStepsPerFrame)
            {
                m_accumulator -= m_step;
                ++steps;
            }
            // Drop any backlog beyond the cap instead of trying to catch up.
            if (m_accumulator > m_step * kMaxStepsPerFrame)
                m_accumulator = 0.0f;
            return steps;
        }

        [[nodiscard]] float Step() const { return m_step; }

    private:
        static constexpr int kMaxStepsPerFrame = 5;
        float m_step;
        float m_accumulator{};
    };
}
