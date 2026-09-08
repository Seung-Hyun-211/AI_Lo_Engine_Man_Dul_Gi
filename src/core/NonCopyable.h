#pragma once

namespace engine::core
{
    // Shared base for owning types. Copying a system that owns threads, GPU
    // resources, or an OS window is always a bug, so the whole engine opts out
    // of copy by inheriting this instead of repeating four `= delete` lines.
    // Move is left enabled per-type: derive and add the move members where a
    // type genuinely needs to be relocated.
    class NonCopyable
    {
    protected:
        NonCopyable() = default;
        ~NonCopyable() = default;

    public:
        NonCopyable(const NonCopyable&) = delete;
        NonCopyable& operator=(const NonCopyable&) = delete;
    };
}
