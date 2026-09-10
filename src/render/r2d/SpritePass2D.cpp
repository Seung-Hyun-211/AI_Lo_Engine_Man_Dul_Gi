#include "render/r2d/SpritePass2D.h"

#include "render/r2d/TextureAtlas.h"          // kUiAtlasId
#include "render/shader/ShaderLibrary.h"

#include <d3d11.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{
    struct Vertex { float x, y, u, v, r, g, b, a; };   // stride 32
    struct Constants { float screenWidth, screenHeight, unused0, unused1; };

    constexpr std::size_t kMaxVertices = 262'144;   // ~43k sprites/frame

    void ThrowIfFailed(HRESULT result, const char* message)
    {
        if (FAILED(result)) throw std::runtime_error(message);
    }

    template <typename T>
    void SafeRelease(T*& object)
    {
        if (object != nullptr) { object->Release(); object = nullptr; }
    }

    void PushSpriteVerts(std::vector<Vertex>& out, const engine::render::SpriteDraw& s)
    {
        const Vertex tl{ s.x,           s.y,            s.u0, s.v0, s.r, s.g, s.b, s.a };
        const Vertex tr{ s.x + s.width, s.y,            s.u1, s.v0, s.r, s.g, s.b, s.a };
        const Vertex bl{ s.x,           s.y + s.height, s.u0, s.v1, s.r, s.g, s.b, s.a };
        const Vertex br{ s.x + s.width, s.y + s.height, s.u1, s.v1, s.r, s.g, s.b, s.a };
        out.insert(out.end(), { tl, tr, bl, bl, tr, br });
    }

    bool ClipEqual(const engine::math::Rect& a, const engine::math::Rect& b)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }

    // ---- minimal .dds reader -------------------------------------------------
    // Supports what tools/atlas_pack emits: a DX10-header .dds, single 2D image,
    // arraySize 1, format R8G8B8A8_UNORM(_SRGB) or BC7_UNORM(_SRGB) or BC4_UNORM,
    // one or more mips. Enough for the debug (uncompressed) atlas now and BC7
    // from the packer later.
    struct DdsImage
    {
        DXGI_FORMAT format{ DXGI_FORMAT_UNKNOWN };
        std::uint32_t width{}, height{}, mipCount{ 1 };
        std::vector<std::uint8_t> pixels;   // all mips, tightly packed
    };

    std::uint32_t Read32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }

    bool FormatBlockSize(DXGI_FORMAT fmt, std::uint32_t& blockBytes, bool& blockCompressed, std::uint32_t& bytesPerPixel)
    {
        blockCompressed = false; blockBytes = 0; bytesPerPixel = 0;
        switch (fmt)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: bytesPerPixel = 4; return true;
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:      blockCompressed = true; blockBytes = 16; return true;
        case DXGI_FORMAT_BC4_UNORM:           blockCompressed = true; blockBytes = 8;  return true;
        default: return false;
        }
    }

    std::size_t MipByteSize(DXGI_FORMAT fmt, std::uint32_t w, std::uint32_t h)
    {
        std::uint32_t blockBytes = 0, bpp = 0; bool bc = false;
        if (!FormatBlockSize(fmt, blockBytes, bc, bpp)) return 0;
        if (bc)
        {
            const std::uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
            return static_cast<std::size_t>(bw) * bh * blockBytes;
        }
        return static_cast<std::size_t>(w) * h * bpp;
    }

    bool LoadDds(const std::string& path, DdsImage& out)
    {
        std::ifstream file;
        for (const std::string& prefix : { std::string{}, std::string{ "../../" }, std::string{ "../../../" } })
        {
            file.open(prefix + path, std::ios::binary);
            if (file.is_open()) break;
        }
        if (!file.is_open()) return false;

        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (bytes.size() < 4 + 124) return false;
        if (std::memcmp(bytes.data(), "DDS ", 4) != 0) return false;

        const std::uint8_t* h = bytes.data() + 4;                 // DDS_HEADER, 124 bytes
        const std::uint32_t height = Read32(h + 8);
        const std::uint32_t width  = Read32(h + 12);
        std::uint32_t mipCount     = Read32(h + 24);
        if (mipCount == 0) mipCount = 1;
        const std::uint32_t fourCC  = Read32(h + 80);             // ddspf.dwFourCC

        std::size_t offset = 4 + 124;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        if (fourCC == 0x30315844u)                                 // 'DX10'
        {
            if (bytes.size() < offset + 20) return false;
            format = static_cast<DXGI_FORMAT>(Read32(bytes.data() + offset));
            offset += 20;                                          // DDS_HEADER_DXT10
        }
        else
        {
            return false;   // only the DX10-header path is supported (packer emits it)
        }

        std::uint32_t blockBytes = 0, bpp = 0; bool bc = false;
        if (!FormatBlockSize(format, blockBytes, bc, bpp)) return false;

        std::size_t total = 0;
        std::uint32_t w = width, hh = height;
        for (std::uint32_t m = 0; m < mipCount; ++m)
        {
            total += MipByteSize(format, w, hh);
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
        }
        if (bytes.size() < offset + total) return false;

        out.format = format;
        out.width = width;
        out.height = height;
        out.mipCount = mipCount;
        out.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + total));
        return true;
    }

    ID3D11ShaderResourceView* CreateSrvFromDds(ID3D11Device* device, const DdsImage& img)
    {
        std::vector<D3D11_SUBRESOURCE_DATA> subs(img.mipCount);
        const std::uint8_t* cursor = img.pixels.data();
        std::uint32_t w = img.width, h = img.height;
        for (std::uint32_t m = 0; m < img.mipCount; ++m)
        {
            std::uint32_t blockBytes = 0, bpp = 0; bool bc = false;
            FormatBlockSize(img.format, blockBytes, bc, bpp);
            const std::uint32_t rowPitch = bc ? ((w + 3) / 4) * blockBytes : w * bpp;
            subs[m].pSysMem = cursor;
            subs[m].SysMemPitch = rowPitch;
            subs[m].SysMemSlicePitch = 0;
            cursor += MipByteSize(img.format, w, h);
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = img.width;
        desc.Height = img.height;
        desc.MipLevels = img.mipCount;
        desc.ArraySize = 1;
        desc.Format = img.format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device->CreateTexture2D(&desc, subs.data(), &tex))) return nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        return srv;
    }
}

namespace engine::render
{
    SpritePass2D::SpritePass2D(std::string atlasDdsPath) : m_atlasDdsPath(std::move(atlasDdsPath)) {}

    ID3D11ShaderResourceView* SpritePass2D::SrvFor(std::uint32_t atlasId) const
    {
        if (atlasId == kUiAtlasId && m_atlasSrv != nullptr) return m_atlasSrv;
        return m_whiteSrv;
    }

    void SpritePass2D::Initialize(ID3D11Device* device, ShaderLibrary& shaders)
    {
        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        m_shader = shaders.Get(device, "sprite2d", layout, ARRAYSIZE(layout));

        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * kMaxVertices);
        vertexDesc.Usage = D3D11_USAGE_DYNAMIC;
        vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ThrowIfFailed(device->CreateBuffer(&vertexDesc, nullptr, &m_vertexBuffer), "CreateBuffer (sprite vertex) failed");

        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(Constants);
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(device->CreateBuffer(&constantDesc, nullptr, &m_constantBuffer), "CreateBuffer (sprite constant) failed");

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ThrowIfFailed(device->CreateBlendState(&blendDesc, &m_blendState), "CreateBlendState (sprite) failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = FALSE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.StencilEnable = FALSE;
        ThrowIfFailed(device->CreateDepthStencilState(&depthDesc, &m_depthDisabled), "CreateDepthStencilState (sprite) failed");

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.ScissorEnable = TRUE;         // the whole point of this pass's clip support
        rasterDesc.MultisampleEnable = TRUE;     // scene target is multisampled
        ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, &m_scissorRaster), "CreateRasterizerState (sprite) failed");

        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(device->CreateSamplerState(&samplerDesc, &m_sampler), "CreateSamplerState (sprite) failed");

        // 1x1 white for atlasId 0 (solid tinted rects).
        const std::uint32_t white = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC whiteDesc{};
        whiteDesc.Width = 1; whiteDesc.Height = 1; whiteDesc.MipLevels = 1; whiteDesc.ArraySize = 1;
        whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        whiteDesc.SampleDesc.Count = 1;
        whiteDesc.Usage = D3D11_USAGE_IMMUTABLE;
        whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA whiteInit{ &white, sizeof(white), 0 };
        ID3D11Texture2D* whiteTex = nullptr;
        ThrowIfFailed(device->CreateTexture2D(&whiteDesc, &whiteInit, &whiteTex), "CreateTexture2D (sprite white) failed");
        device->CreateShaderResourceView(whiteTex, nullptr, &m_whiteSrv);
        whiteTex->Release();

        DdsImage atlas;
        if (LoadDds(m_atlasDdsPath, atlas))
        {
            m_atlasSrv = CreateSrvFromDds(device, atlas);
            OutputDebugStringA(("SpritePass2D: loaded atlas '" + m_atlasDdsPath + "' ("
                + std::to_string(atlas.width) + "x" + std::to_string(atlas.height) + ", "
                + std::to_string(atlas.mipCount) + " mips)\n").c_str());
        }
        else
        {
            OutputDebugStringA(("SpritePass2D: could not load atlas '" + m_atlasDdsPath
                + "' - sprites with atlasId " + std::to_string(kUiAtlasId) + " fall back to white\n").c_str());
        }
    }

    void SpritePass2D::Execute(const PassContext& context)
    {
        const RenderSnapshot& snapshot = *context.snapshot;
        if (snapshot.uiSprites.empty() || m_shader == nullptr) return;

        ID3D11DeviceContext* device = context.context;

        const Constants constants{ static_cast<float>(context.viewportWidth),
                                   static_cast<float>(context.viewportHeight), 0, 0 };
        device->UpdateSubresource(m_constantBuffer, 0, nullptr, &constants, 0, 0);

        const float blendFactor[4]{ 0, 0, 0, 0 };
        device->OMSetBlendState(m_blendState, blendFactor, 0xffffffff);
        device->OMSetDepthStencilState(m_depthDisabled, 0);
        device->RSSetState(m_scissorRaster);
        device->IASetInputLayout(m_shader->inputLayout);
        device->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(m_shader->vs, nullptr, 0);
        device->VSSetConstantBuffers(0, 1, &m_constantBuffer);
        device->PSSetShader(m_shader->ps, nullptr, 0);
        device->PSSetSamplers(0, 1, &m_sampler);

        const UINT stride = sizeof(Vertex), offset = 0;
        device->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);

        // Walk uiSprites in painter order; a run of consecutive sprites sharing
        // (atlasId, clip) is one Draw. A change of either flushes the run.
        std::vector<Vertex> verts;
        verts.reserve(6 * snapshot.uiSprites.size());

        std::uint32_t runAtlas = snapshot.uiSprites.front().atlasId;
        math::Rect runClip = snapshot.uiSprites.front().clip;
        UINT vbOffset = 0;   // sprites already flushed, in vertices

        auto flush = [&](std::uint32_t atlasId, const math::Rect& clip)
        {
            if (verts.empty()) return;
            if (vbOffset + verts.size() > kMaxVertices) { verts.clear(); return; }

            D3D11_MAPPED_SUBRESOURCE mapped{};
            const D3D11_MAP mapType = (vbOffset == 0) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
            if (SUCCEEDED(device->Map(m_vertexBuffer, 0, mapType, 0, &mapped)))
            {
                std::memcpy(static_cast<Vertex*>(mapped.pData) + vbOffset, verts.data(), sizeof(Vertex) * verts.size());
                device->Unmap(m_vertexBuffer, 0);
            }

            if (clip.width > 0.0f && clip.height > 0.0f)
            {
                const D3D11_RECT r{
                    static_cast<LONG>(clip.x), static_cast<LONG>(clip.y),
                    static_cast<LONG>(clip.x + clip.width), static_cast<LONG>(clip.y + clip.height) };
                device->RSSetScissorRects(1, &r);
            }
            else
            {
                const D3D11_RECT full{ 0, 0, static_cast<LONG>(context.viewportWidth),
                                       static_cast<LONG>(context.viewportHeight) };
                device->RSSetScissorRects(1, &full);
            }

            ID3D11ShaderResourceView* srv = SrvFor(atlasId);
            device->PSSetShaderResources(0, 1, &srv);
            device->Draw(static_cast<UINT>(verts.size()), vbOffset);

            vbOffset += static_cast<UINT>(verts.size());
            verts.clear();
        };

        for (const SpriteDraw& s : snapshot.uiSprites)
        {
            if (s.atlasId != runAtlas || !ClipEqual(s.clip, runClip))
            {
                flush(runAtlas, runClip);
                runAtlas = s.atlasId;
                runClip = s.clip;
            }
            PushSpriteVerts(verts, s);
        }
        flush(runAtlas, runClip);
    }

    void SpritePass2D::Release()
    {
        m_shader = nullptr;   // owned by ShaderLibrary
        SafeRelease(m_atlasSrv);
        SafeRelease(m_whiteSrv);
        SafeRelease(m_sampler);
        SafeRelease(m_scissorRaster);
        SafeRelease(m_depthDisabled);
        SafeRelease(m_blendState);
        SafeRelease(m_constantBuffer);
        SafeRelease(m_vertexBuffer);
    }
}
