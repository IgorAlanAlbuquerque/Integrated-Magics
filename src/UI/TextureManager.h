#pragma once
#include <d3d11.h>

#include <filesystem>
#include <map>
#include <string>

#include "PCH.h"

namespace IntegratedMagic {

    enum class SpellIconType : std::int32_t {
        spell_default = 0,
        alteration,
        conjuration,
        illusion,
        restoration,
        destruction,
        destruction_fire,
        destruction_frost,
        destruction_shock,
        power,
        shout,
        total
    };

    enum class UiTextureType : std::int32_t { slot_bg = 0, slot_bg_active = 1, slot_bg_empty = 2, total };

    class TextureManager {
    public:
        struct Image {
            ID3D11ShaderResourceView* texture{nullptr};
            std::int32_t width{0};
            std::int32_t height{0};
            bool valid() const noexcept { return texture != nullptr; }
        };

        static void Init();

        static const Image& GetSpellIcon(const RE::SpellItem* spell);
        static const Image& GetIconForForm(RE::FormID formID);
        static const Image& GetIcon(SpellIconType type);

        static const Image& GetUiTexture(UiTextureType type);

    private:
                static bool LoadSVG(const char* path, Image& out, int targetSize = 0);

        static inline std::map<std::int32_t, Image> icons_;

        static inline std::map<RE::FormID, Image> formid_icons_;

        static inline std::map<std::int32_t, Image> ui_icons_;

        static inline std::string icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\icons)";
        static inline std::string spell_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\icons\spells)";
        static inline std::string ui_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\icons\ui)";

        static inline const std::map<std::string, SpellIconType> filename_map_ = {
            {"spell_default.svg", SpellIconType::spell_default},
            {"alteration.svg", SpellIconType::alteration},
            {"conjuration.svg", SpellIconType::conjuration},
            {"illusion.svg", SpellIconType::illusion},
            {"restoration.svg", SpellIconType::restoration},
            {"destruction.svg", SpellIconType::destruction},
            {"destruction_fire.svg", SpellIconType::destruction_fire},
            {"destruction_frost.svg", SpellIconType::destruction_frost},
            {"destruction_shock.svg", SpellIconType::destruction_shock},
            {"power.svg", SpellIconType::power},
            {"shout.svg", SpellIconType::shout},
        };

        static inline const std::map<std::string, UiTextureType> ui_filename_map_ = {
            {"slot_bg.svg", UiTextureType::slot_bg},
            {"slot_bg_active.svg", UiTextureType::slot_bg_active},
            {"slot_bg_empty.svg", UiTextureType::slot_bg_empty},
        };

        static SpellIconType ClassifySpell(const RE::SpellItem* spell);
    };
}