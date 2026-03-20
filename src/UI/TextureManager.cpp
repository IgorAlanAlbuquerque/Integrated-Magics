#include "TextureManager.h"

#include <format>
#include <utility>

#include "PCH.h"
#include "UI/StyleConfig.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include "lib/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "lib/nanosvgrast.h"
namespace IntegratedMagic {
    void TextureManager::Init() {
        if (!std::filesystem::exists(icon_dir_)) {
            spdlog::warn("[TextureManager] Icon directory not found: {}", icon_dir_);
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(icon_dir_)) {
                const auto& path = entry.path();
                if (path.extension() != ".svg") continue;

                const std::string filename = path.filename().string();
                auto it = filename_map_.find(filename);
                if (it == filename_map_.end()) continue;

                const auto idx = std::to_underlying(it->second);
                Image img;
                if (LoadSVG(path.string().c_str(), img)) {
                    icons_[idx] = img;
                } else {
                    spdlog::error("[TextureManager] Failed to load: {}", filename);
                }
            }
        }

        formid_icons_.clear();
        if (std::filesystem::exists(spell_icon_dir_)) {
            for (const auto& entry : std::filesystem::directory_iterator(spell_icon_dir_)) {
                const auto& path = entry.path();
                if (path.extension() != ".svg") continue;

                const std::string stem = path.stem().string();
                if (stem.size() != 8) continue;

                try {
                    const auto formID = static_cast<RE::FormID>(std::stoul(stem, nullptr, 16));
                    Image img;
                    if (LoadSVG(path.string().c_str(), img)) {
                        formid_icons_[formID] = img;
                        spdlog::info("[TextureManager] Loaded spell icon: {} -> FormID {:#010x}", stem, formID);
                    } else {
                        spdlog::error("[TextureManager] Failed to load spell icon: {}", stem);
                    }
                } catch (...) {
                    spdlog::warn("[TextureManager] Invalid spell icon filename (not hex8): {}", stem);
                }
            }
            spdlog::info("[TextureManager] Loaded {} per-spell icon(s).", formid_icons_.size());
        }

        ui_icons_.clear();
        if (std::filesystem::exists(ui_icon_dir_)) {
            for (const auto& entry : std::filesystem::directory_iterator(ui_icon_dir_)) {
                const auto& path = entry.path();
                if (path.extension() != ".svg") continue;

                const std::string filename = path.filename().string();
                auto it = ui_filename_map_.find(filename);
                if (it == ui_filename_map_.end()) continue;

                Image img;
                if (LoadSVG(path.string().c_str(), img, 256)) {
                    const auto idx = std::to_underlying(it->second);
                    ui_icons_[idx] = img;
                    spdlog::info("[TextureManager] Loaded UI texture: {}", filename);
                } else {
                    spdlog::error("[TextureManager] Failed to load UI texture: {}", filename);
                }
            }
        }

        xbox_icons_.clear();
        ps_icons_.clear();
        keyboard_icons_.clear();
    }

    const TextureManager::Image& TextureManager::GetSpellIcon(const RE::SpellItem* spell) {
        if (spell) {
            if (auto it = formid_icons_.find(spell->GetFormID()); it != formid_icons_.end()) return it->second;
        }
        return GetIcon(ClassifySpell(spell));
    }

    const TextureManager::Image& TextureManager::GetIconForForm(RE::FormID formID) {
        if (!formID) return GetIcon(SpellIconType::spell_default);

        if (auto it = formid_icons_.find(formID); it != formid_icons_.end()) return it->second;

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

    const TextureManager::Image& TextureManager::GetUiTexture(UiTextureType type) {
        const auto idx = std::to_underlying(type);
        if (auto it = ui_icons_.find(idx); it != ui_icons_.end() && it->second.valid()) return it->second;
        static const Image kEmpty{};
        return kEmpty;
    }

    const TextureManager::Image& TextureManager::GetGamepadButtonIcon(int buttonIndex, ButtonIconType type) {
        static const Image kEmpty{};
        if (buttonIndex < 0 || buttonIndex >= kGamepadButtonCount) return kEmpty;
        return LoadGamepadIconOnDemand(buttonIndex, type);
    }

    const TextureManager::Image& TextureManager::GetKeyboardIcon(int scancode) {
        return LoadKeyboardIconOnDemand(scancode);
    }

    const TextureManager::Image& TextureManager::LoadGamepadIconOnDemand(int buttonIndex, ButtonIconType type) {
        static const Image kEmpty{};
        auto& iconMap = (type == ButtonIconType::PlayStation) ? ps_icons_ : xbox_icons_;
        auto& nameMap = (type == ButtonIconType::PlayStation) ? ps_filename_map_ : xbox_filename_map_;
        auto& dir = (type == ButtonIconType::PlayStation) ? ps_icon_dir_ : xbox_icon_dir_;

        const int targetSize = std::clamp(static_cast<int>(StyleConfig::Get().modifierWidgetRadius * 4.f), 16, 1024);

        if (auto it = iconMap.find(buttonIndex); it != iconMap.end()) {
            if (it->second.loadedSize == targetSize) return it->second.valid() ? it->second : kEmpty;

            if (it->second.texture) it->second.texture->Release();
            iconMap.erase(it);
        }

        for (auto const& [filename, idx] : nameMap) {
            if (idx != buttonIndex) continue;
            const std::string fullPath = dir + "\\" + filename;
            Image img;
            if (LoadSVG(fullPath.c_str(), img, targetSize)) {
                img.loadedSize = targetSize;
                spdlog::info("[TextureManager] Lazy loaded button icon: {} ({}px)", filename, targetSize);
                iconMap[buttonIndex] = img;
                return iconMap[buttonIndex];
            }
            Image sentinel{};
            sentinel.loadedSize = targetSize;
            iconMap[buttonIndex] = sentinel;
            spdlog::warn("[TextureManager] Failed to lazy load button icon: {}", filename);
            return kEmpty;
        }

        Image sentinel{};
        sentinel.loadedSize = targetSize;
        iconMap[buttonIndex] = sentinel;
        return kEmpty;
    }

    const TextureManager::Image& TextureManager::LoadKeyboardIconOnDemand(int scancode) {
        static const Image kEmpty{};

        const int targetSize = std::clamp(static_cast<int>(StyleConfig::Get().modifierWidgetRadius * 4.f), 16, 1024);

        if (auto it = keyboard_icons_.find(scancode); it != keyboard_icons_.end()) {
            if (it->second.loadedSize == targetSize) return it->second.valid() ? it->second : kEmpty;
            if (it->second.texture) it->second.texture->Release();
            keyboard_icons_.erase(it);
        }

        auto tryLoad = [&](const std::string& filename) -> bool {
            const std::string fullPath = kb_icon_dir_ + "\\" + filename;
            Image img;
            if (LoadSVG(fullPath.c_str(), img, targetSize)) {
                img.loadedSize = targetSize;
                spdlog::info("[TextureManager] Lazy loaded keyboard icon: {} ({}px)", filename, targetSize);
                keyboard_icons_[scancode] = img;
                return true;
            }
            return false;
        };

        for (auto const& [filename, idx] : kb_named_map_) {
            if (idx != scancode) continue;
            if (!tryLoad(filename)) {
                Image sentinel{};
                sentinel.loadedSize = targetSize;
                keyboard_icons_[scancode] = sentinel;
                spdlog::warn("[TextureManager] Failed to lazy load keyboard icon: {}", filename);
            }
            return keyboard_icons_[scancode].valid() ? keyboard_icons_[scancode] : kEmpty;
        }

        const std::string hexName = std::format("{:02x}.svg", static_cast<unsigned>(scancode));
        if (tryLoad(hexName)) return keyboard_icons_[scancode];

        Image sentinel{};
        sentinel.loadedSize = targetSize;
        keyboard_icons_[scancode] = sentinel;
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

    bool TextureManager::LoadSVG(const char* path, Image& out, int targetSize) {
        auto* device = RE::BSGraphics::Renderer::GetDevice();
        if (!device) {
            spdlog::error("[TextureManager] BSGraphics::Renderer::GetDevice() retornou null.");
            return false;
        }

        auto* svg = nsvgParseFromFile(path, "px", 96.0f);
        if (!svg) return false;

        auto* rast = nsvgCreateRasterizer();
        const float svgW = svg->width;
        const float svgH = svg->height;

        if (svgW <= 0.f || svgH <= 0.f) {
            nsvgDelete(svg);
            nsvgDeleteRasterizer(rast);
            return false;
        }

        float scale = 1.f;
        int w, h;
        if (targetSize > 0) {
            scale = static_cast<float>(targetSize) / std::max(svgW, svgH);
            w = targetSize;
            h = targetSize;
        } else {
            w = static_cast<int>(svgW);
            h = static_cast<int>(svgH);
        }

        auto* data = static_cast<unsigned char*>(malloc(w * h * 4));

        const float tx = (w - svgW * scale) * 0.5f;
        const float ty = (h - svgH * scale) * 0.5f;
        nsvgRasterize(rast, svg, tx, ty, scale, data, w, h, w * 4);
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