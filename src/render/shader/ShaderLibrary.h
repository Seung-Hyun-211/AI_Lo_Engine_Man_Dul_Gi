#pragma once

#include "core/NonCopyable.h"

#include <d3d11.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::render
{
    // A compiled VS + PS + input layout for one .hlsl file. Its address is stable
    // for the life of the ShaderLibrary; the contained pointers are swapped in
    // place when the source file changes (hot reload), so a pass can hold a
    // `const ShaderProgram*` and just read `program->vs` every frame.
    struct ShaderProgram
    {
        ID3D11VertexShader* vs{};
        ID3D11PixelShader* ps{};
        ID3D11InputLayout* inputLayout{};
    };

    // Loads shaders from `<assets>/shaders/<name>.hlsl` (entry points VSMain /
    // PSMain, shader model 5.0). `#include "x.hlsli"` resolves against the same
    // folder. Compiled programs are cached by name; PollHotReload() recompiles
    // any whose file changed and keeps the last good version on error.
    class ShaderLibrary final : private core::NonCopyable
    {
    public:
        ShaderLibrary();
        ~ShaderLibrary();

        // Compiles `<name>.hlsl` on first request and caches it. `layout` /
        // `layoutCount` describe the vertex input; the semantic-name pointers
        // must outlive the library (string literals do).
        [[nodiscard]] const ShaderProgram* Get(ID3D11Device* device, const std::string& name,
                                               const D3D11_INPUT_ELEMENT_DESC* layout, std::uint32_t layoutCount);

        // Recompiles shaders whose source file's write time changed. Call once
        // per frame on the render thread.
        void PollHotReload(ID3D11Device* device);

        void ReleaseAll();

    private:
        struct Entry
        {
            ShaderProgram program;
            std::string sourcePath;
            std::uint64_t lastWriteTime{};
            std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
        };

        // Returns false and leaves `out` untouched on failure (logs via
        // OutputDebugString).
        bool Compile(ID3D11Device* device, const std::string& name, const std::string& sourcePath,
                     const std::vector<D3D11_INPUT_ELEMENT_DESC>& layout, ShaderProgram& out) const;

        std::string m_shaderDir;
        std::unordered_map<std::string, Entry> m_entries;
    };
}
