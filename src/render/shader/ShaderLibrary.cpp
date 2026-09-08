#include "render/shader/ShaderLibrary.h"

#include "core/AssetPaths.h"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace
{
    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr) { object->Release(); object = nullptr; }
    }

    bool ReadWholeFile(const std::string& path, std::string& out)
    {
        std::FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) return false;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        if (size < 0) { std::fclose(file); return false; }
        out.resize(static_cast<std::size_t>(size));
        const std::size_t read = std::fread(out.data(), 1, out.size(), file);
        std::fclose(file);
        out.resize(read);
        return true;
    }

    std::uint64_t FileWriteTime(const std::string& path)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return 0;
        return (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32)
             | data.ftLastWriteTime.dwLowDateTime;
    }

    // Resolves `#include "x.hlsli"` against the shader directory.
    class IncludeFromDir final : public ID3DInclude
    {
    public:
        explicit IncludeFromDir(std::string dir) : m_dir(std::move(dir)) {}

        HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* data, UINT* bytes) override
        {
            std::string contents;
            if (!ReadWholeFile(m_dir + "\\" + fileName, contents)) return E_FAIL;
            char* buffer = new char[contents.size()];
            std::memcpy(buffer, contents.data(), contents.size());
            *data = buffer;
            *bytes = static_cast<UINT>(contents.size());
            return S_OK;
        }

        HRESULT __stdcall Close(LPCVOID data) override
        {
            delete[] static_cast<const char*>(data);
            return S_OK;
        }

    private:
        std::string m_dir;
    };

    void LogBlob(const char* prefix, const std::string& name, ID3DBlob* errors)
    {
        std::string message = std::string(prefix) + " " + name;
        if (errors != nullptr)
            message += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        message += "\n";
        OutputDebugStringA(message.c_str());
    }
}

namespace engine::render
{
    ShaderLibrary::ShaderLibrary() : m_shaderDir(core::ResolveAsset("shaders")) {}

    ShaderLibrary::~ShaderLibrary() { ReleaseAll(); }

    void ShaderLibrary::ReleaseAll()
    {
        for (auto& entry : m_entries)
        {
            SafeRelease(entry.second.program.vs);
            SafeRelease(entry.second.program.ps);
            SafeRelease(entry.second.program.inputLayout);
        }
        m_entries.clear();
    }

    bool ShaderLibrary::Compile(ID3D11Device* device, const std::string& name, const std::string& sourcePath,
                                const std::vector<D3D11_INPUT_ELEMENT_DESC>& layout, ShaderProgram& out) const
    {
        std::string source;
        if (!ReadWholeFile(sourcePath, source))
        {
            OutputDebugStringA(("ShaderLibrary: cannot read " + sourcePath + "\n").c_str());
            return false;
        }

        IncludeFromDir include(m_shaderDir);
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errors = nullptr;

        HRESULT hr = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, &include,
            "VSMain", "vs_5_0", flags, 0, &vsBlob, &errors);
        if (FAILED(hr)) { LogBlob("VS compile failed", name, errors); SafeRelease(errors); return false; }
        SafeRelease(errors);

        hr = D3DCompile(source.data(), source.size(), name.c_str(), nullptr, &include,
            "PSMain", "ps_5_0", flags, 0, &psBlob, &errors);
        if (FAILED(hr)) { LogBlob("PS compile failed", name, errors); SafeRelease(errors); SafeRelease(vsBlob); return false; }
        SafeRelease(errors);

        ID3D11VertexShader* vs = nullptr;
        ID3D11PixelShader* ps = nullptr;
        ID3D11InputLayout* inputLayout = nullptr;

        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        if (SUCCEEDED(hr))
            hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
        if (SUCCEEDED(hr) && !layout.empty())
            hr = device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()),
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);

        SafeRelease(vsBlob);
        SafeRelease(psBlob);

        if (FAILED(hr))
        {
            SafeRelease(vs); SafeRelease(ps); SafeRelease(inputLayout);
            OutputDebugStringA(("ShaderLibrary: device object creation failed for " + name + "\n").c_str());
            return false;
        }

        out.vs = vs;
        out.ps = ps;
        out.inputLayout = inputLayout;
        return true;
    }

    const ShaderProgram* ShaderLibrary::Get(ID3D11Device* device, const std::string& name,
                                            const D3D11_INPUT_ELEMENT_DESC* layout, std::uint32_t layoutCount)
    {
        if (const auto it = m_entries.find(name); it != m_entries.end()) return &it->second.program;

        Entry entry;
        entry.sourcePath = core::ResolveAsset("shaders/" + name + ".hlsl");
        entry.layout.assign(layout, layout + layoutCount);
        if (!Compile(device, name, entry.sourcePath, entry.layout, entry.program))
            throw std::runtime_error("ShaderLibrary: failed to compile shader '" + name + "' from " + entry.sourcePath);
        entry.lastWriteTime = FileWriteTime(entry.sourcePath);

        const auto [iter, inserted] = m_entries.emplace(name, std::move(entry));
        (void)inserted;
        return &iter->second.program;
    }

    void ShaderLibrary::PollHotReload(ID3D11Device* device)
    {
        for (auto& [name, entry] : m_entries)
        {
            const std::uint64_t writeTime = FileWriteTime(entry.sourcePath);
            if (writeTime == 0 || writeTime == entry.lastWriteTime) continue;
            entry.lastWriteTime = writeTime;   // do not retry every frame; wait for the next edit

            ShaderProgram fresh{};
            if (Compile(device, name, entry.sourcePath, entry.layout, fresh))
            {
                SafeRelease(entry.program.vs);
                SafeRelease(entry.program.ps);
                SafeRelease(entry.program.inputLayout);
                entry.program = fresh;
                OutputDebugStringA(("ShaderLibrary: reloaded " + name + "\n").c_str());
            }
            else
            {
                OutputDebugStringA(("ShaderLibrary: reload failed, keeping previous: " + name + "\n").c_str());
            }
        }
    }
}
