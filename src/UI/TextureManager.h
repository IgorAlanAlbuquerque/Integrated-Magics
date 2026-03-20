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
    constexpr int kMouseMiddleIndex = 258;
    constexpr int kBlankKeyIndex = 259;

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
            {"XboxSeriesX_Dpad_Up.svg", 0},
            {"XboxSeriesX_Dpad_Down.svg", 1},
            {"XboxSeriesX_Dpad_Left.svg", 2},
            {"XboxSeriesX_Dpad_Right.svg", 3},
            {"XboxSeriesX_Menu.svg", 4},
            {"XboxSeriesX_View.svg", 5},
            {"XboxSeriesX_Left_Stick_Click.svg", 6},
            {"XboxSeriesX_Right_Stick_Click.svg", 7},
            {"XboxSeriesX_LB.svg", 8},
            {"XboxSeriesX_RB.svg", 9},
            {"XboxSeriesX_A.svg", 10},
            {"XboxSeriesX_B.svg", 11},
            {"XboxSeriesX_X.svg", 12},
            {"XboxSeriesX_Y.svg", 13},
            {"XboxSeriesX_LT.svg", 14},
            {"XboxSeriesX_RT.svg", 15},
        };

        static inline const std::map<std::string, int> ps_filename_map_ = {
            {"PS5_Dpad_Up.svg", 0},
            {"PS5_Dpad_Down.svg", 1},
            {"PS5_Dpad_Left.svg", 2},
            {"PS5_Dpad_Right.svg", 3},
            {"PS5_Options_Alt.svg", 4},
            {"PS5_Share_Alt.svg", 5},
            {"PS5_Left_Stick_Click.svg", 6},
            {"PS5_Right_Stick_Click.svg", 7},
            {"PS5_L1.svg", 8},
            {"PS5_R1.svg", 9},
            {"PS5_Cross.svg", 10},
            {"PS5_Circle.svg", 11},
            {"PS5_Square.svg", 12},
            {"PS5_Triangle.svg", 13},
            {"PS5_L2.svg", 14},
            {"PS5_R2.svg", 15},
        };

        static inline const std::map<std::string, int> kb_named_map_ = {

            {"Mouse_Left_Key_Dark.svg", kMouseLeftIndex},
            {"Mouse_Right_Key_Dark.svg", kMouseRightIndex},
            {"Mouse_Middle_Key_Dark.svg", kMouseMiddleIndex},

            {"Blank_Black_Normal.svg", kBlankKeyIndex},

            {"1_Key_Dark.svg", 0x02},
            {"2_Key_Dark.svg", 0x03},
            {"3_Key_Dark.svg", 0x04},
            {"4_Key_Dark.svg", 0x05},
            {"5_Key_Dark.svg", 0x06},
            {"6_Key_Dark.svg", 0x07},
            {"7_Key_Dark.svg", 0x08},
            {"8_Key_Dark.svg", 0x09},
            {"9_Key_Dark.svg", 0x0A},
            {"0_Key_Dark.svg", 0x0B},

            {"Q_Key_Dark.svg", 0x10},
            {"W_Key_Dark.svg", 0x11},
            {"E_Key_Dark.svg", 0x12},
            {"R_Key_Dark.svg", 0x13},
            {"T_Key_Dark.svg", 0x14},
            {"Y_Key_Dark.svg", 0x15},
            {"U_Key_Dark.svg", 0x16},
            {"I_Key_Dark.svg", 0x17},
            {"O_Key_Dark.svg", 0x18},
            {"P_Key_Dark.svg", 0x19},
            {"A_Key_Dark.svg", 0x1E},
            {"S_Key_Dark.svg", 0x1F},
            {"D_Key_Dark.svg", 0x20},
            {"F_Key_Dark.svg", 0x21},
            {"G_Key_Dark.svg", 0x22},
            {"H_Key_Dark.svg", 0x23},
            {"J_Key_Dark.svg", 0x24},
            {"K_Key_Dark.svg", 0x25},
            {"L_Key_Dark.svg", 0x26},
            {"Z_Key_Dark.svg", 0x2C},
            {"X_Key_Dark.svg", 0x2D},
            {"C_Key_Dark.svg", 0x2E},
            {"V_Key_Dark.svg", 0x2F},
            {"B_Key_Dark.svg", 0x30},
            {"N_Key_Dark.svg", 0x31},
            {"M_Key_Dark.svg", 0x32},

            {"Minus_Key_Dark.svg", 0x0C},
            {"Plus_Key_Dark.svg", 0x0D},
            {"Bracket_Left_Key_Dark.svg", 0x1A},
            {"Bracket_Right_Key_Dark.svg", 0x1B},
            {"Semicolon_Key_Dark.svg", 0x27},
            {"Mark_Left_Key_Dark.svg", 0x33},
            {"Mark_Right_Key_Dark.svg", 0x34},
            {"Slash_Key_Dark.svg", 0x2B},
            {"Question_Key_Dark.svg", 0x35},
            {"Asterisk_Key_Dark.svg", 0x37},

            {"Esc_Key_Dark.svg", 0x01},
            {"Tab_Key_Dark.svg", 0x0F},
            {"Caps_Lock_Key_Dark.svg", 0x3A},
            {"Shift_Key_Dark.svg", 0x2A},
            {"Ctrl_Key_Dark.svg", 0x1D},
            {"Alt_Key_Dark.svg", 0x38},
            {"Space_Key_Dark.svg", 0x39},
            {"Enter_Key_Dark.svg", 0x1C},
            {"Backspace_Key_Dark.svg", 0x0E},
            {"Win_Key_Dark.svg", 0xDB},

            {"F1_Key_Dark.svg", 0x3B},
            {"F2_Key_Dark.svg", 0x3C},
            {"F3_Key_Dark.svg", 0x3D},
            {"F4_Key_Dark.svg", 0x3E},
            {"F5_Key_Dark.svg", 0x3F},
            {"F6_Key_Dark.svg", 0x40},
            {"F7_Key_Dark.svg", 0x41},
            {"F8_Key_Dark.svg", 0x42},
            {"F9_Key_Dark.svg", 0x43},
            {"F10_Key_Dark.svg", 0x44},
            {"F11_Key_Dark.svg", 0x57},
            {"F12_Key_Dark.svg", 0x58},

            {"Insert_Key_Dark.svg", 0xD2},
            {"Del_Key_Dark.svg", 0xD3},
            {"Home_Key_Dark.svg", 0xC7},
            {"End_Key_Dark.svg", 0xCF},
            {"Page_Up_Key_Dark.svg", 0xC9},
            {"Page_Down_Key_Dark.svg", 0xD1},

            {"Arrow_Up_Key_Dark.svg", 0xC8},
            {"Arrow_Down_Key_Dark.svg", 0xD0},
            {"Arrow_Left_Key_Dark.svg", 0xCB},
            {"Arrow_Right_Key_Dark.svg", 0xCD},

            {"Num_Lock_Key_Dark.svg", 0x45},
        };

        static SpellIconType ClassifySpell(const RE::SpellItem* spell);

        static void LoadButtonIconDir(const std::string& dir, const std::map<std::string, int>& map,
                                      std::map<int, Image>& out);

        static void LoadKeyboardIconDir(const std::string& dir, std::map<int, Image>& out);
    };
}