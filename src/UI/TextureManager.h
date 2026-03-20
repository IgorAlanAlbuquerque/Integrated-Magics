#pragma once
#include <d3d11.h>

#include <filesystem>
#include <map>
#include <string>

#include "PCH.h"
#include "StyleConfig.h"

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

    constexpr int kGamepadButtonCount = 16;

    constexpr int kMouseLeftIndex = 256;
    constexpr int kMouseRightIndex = 257;

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

        static const Image& GetGamepadButtonIcon(int buttonIndex, ButtonIconType type);
        static const Image& GetKeyboardIcon(int scancode);

    private:
        static bool LoadSVG(const char* path, Image& out, int targetSize = 0);

        static inline std::map<std::int32_t, Image> icons_;
        static inline std::map<RE::FormID, Image> formid_icons_;
        static inline std::map<std::int32_t, Image> ui_icons_;

        static inline std::map<int, Image> xbox_icons_;
        static inline std::map<int, Image> ps_icons_;
        static inline std::map<int, Image> keyboard_icons_;

        static inline std::string icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\icons)";
        static inline std::string spell_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\icons\unique)";
        static inline std::string ui_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\ui)";
        static inline std::string xbox_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\buttons\xbox)";
        static inline std::string ps_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\buttons\ps)";
        static inline std::string kb_icon_dir_ = R"(.\Data\SKSE\Plugins\IntegratedMagics\resources\buttons\keyboard)";

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

        static inline const std::map<std::string, int> xbox_filename_map_ = {
            {"xbox_up.svg", 0},        {"xbox_down.svg", 1}, {"xbox_left.svg", 2}, {"xbox_right.svg", 3},
            {"xbox_start.svg", 4},     {"xbox_back.svg", 5}, {"xbox_ls.svg", 6},   {"xbox_rs.svg", 7},
            {"xbox_lb.svg", 8},        {"xbox_rb.svg", 9},   {"xbox_a.svg", 10},   {"XboxSeriesX_B.svg", 11},
            {"XboxSeriesX_X.svg", 12}, {"xbox_y.svg", 13},   {"xbox_lt.svg", 14},  {"xbox_rt.svg", 15},
        };

        static inline const std::map<std::string, int> ps_filename_map_ = {
            {"ps_up.svg", 0},       {"ps_down.svg", 1},      {"ps_left.svg", 2},   {"ps_right.svg", 3},
            {"ps_options.svg", 4},  {"ps_share.svg", 5},     {"ps_l3.svg", 6},     {"ps_r3.svg", 7},
            {"ps_l1.svg", 8},       {"ps_r1.svg", 9},        {"ps_cross.svg", 10}, {"PS5_Circle.svg", 11},
            {"PS5_Square.svg", 12}, {"ps_triangle.svg", 13}, {"ps_l2.svg", 14},    {"ps_r2.svg", 15},
        };

        static inline const std::map<std::string, int> kb_named_map_ = {
            {"Mouse_Left_Key_Dark.svg", kMouseLeftIndex},
            {"Mouse_Right_Key_Dark.svg", kMouseRightIndex},
        };

        static SpellIconType ClassifySpell(const RE::SpellItem* spell);

        static void LoadButtonIconDir(const std::string& dir, const std::map<std::string, int>& map,
                                      std::map<int, Image>& out);

        static void LoadKeyboardIconDir(const std::string& dir, std::map<int, Image>& out);
    };
}