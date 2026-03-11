#include "TextureManager.h"

#include <utility>

#include "PCH.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "lib/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "lib/nanosvgrast.h"
namespace IntegratedMagic {
    void TextureManager::Init() {
        if (!std::filesystem::exists(icon_dir_)) {
            spdlog::warn("[TextureManager] Icon directory not found: {}", icon_dir_);
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(icon_dir_)) {
            const auto& path = entry.path();
            if (path.extension() != ".svg") continue;

            const std::string filename = path.filename().string();
            auto it = filename_map_.find(filename);
            if (it == filename_map_.end()) {
                spdlog::warn("[TextureManager] Unknown icon file (ignored): {}", filename);
                continue;
            }

            const auto idx = std::to_underlying(it->second);
            Image img;
            if (LoadSVG(path.string().c_str(), img)) {
                icons_[idx] = img;
            } else {
                spdlog::error("[TextureManager] Failed to load: {}", filename);
            }
        }
    }

    const TextureManager::Image& TextureManager::GetSpellIcon(const RE::SpellItem* spell) {
        return GetIcon(ClassifySpell(spell));
    }

    const TextureManager::Image& TextureManager::GetIconForForm(RE::FormID formID) {
        if (!formID) return GetIcon(SpellIconType::spell_default);

        auto* form = RE::TESForm::LookupByID(formID);
        if (!form) return GetIcon(SpellIconType::spell_default);

        if (form->As<RE::TESShout>()) return GetIcon(SpellIconType::shout);

        if (auto const* spell = form->As<RE::SpellItem>()) return GetSpellIcon(spell);

        return GetIcon(SpellIconType::spell_default);
    }

    const TextureManager::Image& TextureManager::GetIcon(SpellIconType type) {
        const auto idx = std::to_underlying(type);
        if (auto it = icons_.find(idx); it != icons_.end() && it->second.valid()) return it->second;
        const auto defIdx = std::to_underlying(SpellIconType::spell_default);
        if (auto defIt = icons_.find(defIdx); defIt != icons_.end() && defIt->second.valid()) return defIt->second;
        static const Image kEmpty{};
        return kEmpty;
    }

    SpellIconType TextureManager::ClassifySpell(const RE::SpellItem* spell) {
        using enum IntegratedMagic::SpellIconType;
        if (!spell) return spell_default;

        using SpellType = RE::MagicSystem::SpellType;
        if (spell->GetSpellType() == SpellType::kLesserPower || spell->GetSpellType() == SpellType::kPower)
            return power;
        if (spell->GetSpellType() == SpellType::kVoicePower) return shout;

        const auto* fx = spell->GetCostliestEffectItem();
        if (!fx || !fx->baseEffect) return spell_default;

        auto school = fx->baseEffect->GetMagickSkill();
        if (school == RE::ActorValue::kNone) school = fx->baseEffect->data.primaryAV;

        switch (school) {
            using enum RE::ActorValue;

            case kAlteration:
                return alteration;
            case kConjuration:
                return conjuration;
            case kIllusion:
                return illusion;
            case kRestoration:
                return restoration;

            case kDestruction: {
                const auto resist = fx->baseEffect->data.resistVariable;
                if (resist == RE::ActorValue::kResistFire) return destruction_fire;
                if (resist == RE::ActorValue::kResistShock) return destruction_shock;
                if (resist == RE::ActorValue::kResistFrost) return destruction_frost;
                return destruction;
            }

            default:
                return spell_default;
        }
    }

    bool TextureManager::LoadSVG(const char* path, Image& out) {
        auto* device = RE::BSGraphics::Renderer::GetDevice();
        if (!device) {
            spdlog::error("[TextureManager] BSGraphics::Renderer::GetDevice() retornou null.");
            return false;
        }

        auto* svg = nsvgParseFromFile(path, "px", 96.0f);
        if (!svg) return false;

        auto* rast = nsvgCreateRasterizer();
        const auto w = static_cast<int>(svg->width);
        const auto h = static_cast<int>(svg->height);

        if (w <= 0 || h <= 0) {
            nsvgDelete(svg);
            nsvgDeleteRasterizer(rast);
            return false;
        }

        auto* data = static_cast<unsigned char*>(malloc(w * h * 4));
        nsvgRasterize(rast, svg, 0, 0, 1, data, w, h, w * 4);
        nsvgDelete(svg);
        nsvgDeleteRasterizer(rast);

        auto* d3dDevice = reinterpret_cast<ID3D11Device*>(device);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub{};
        sub.pSysMem = data;
        sub.SysMemPitch = static_cast<UINT>(w * 4);

        ID3D11Texture2D* tex{nullptr};
        HRESULT hr = d3dDevice->CreateTexture2D(&desc, &sub, &tex);
        free(data);
        if (FAILED(hr) || !tex) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        srv_desc.Texture2D.MostDetailedMip = 0;

        hr = d3dDevice->CreateShaderResourceView(tex, &srv_desc, &out.texture);
        tex->Release();
        if (FAILED(hr)) return false;

        out.width = w;
        out.height = h;
        return true;
    }
}