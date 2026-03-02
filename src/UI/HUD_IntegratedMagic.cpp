#include "HUD_IntegratedMagic.h"

#include <cmath>
#include <numbers>

#include "Config/MagicSlots.h"
#include "PCH.h"
#include "RE/B/BSFixedString.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"
#include "SKSEMenuFramework.h"
#include "State/MagicState.h"

namespace ImGui = ImGuiMCP;
namespace DL = ImGuiMCP::ImDrawListManager;

namespace IntegratedMagic::HUD {
    namespace {
        using namespace ImGuiMCP;

        constexpr float kPI = std::numbers::pi_v<float>;
        constexpr float kSlotRadius = 32.f;  // maiores
        constexpr float kRingRadius = 54.f;
        constexpr float kGlowPad = 16.f;  // margem extra para o glow não ser cortado pela janela
        constexpr float kMargin = kRingRadius + kSlotRadius + kGlowPad + 4.f;
        constexpr float kDivWidth = 1.5f;

        static const RE::BSFixedString kMagicMenu{"MagicMenu"};

        bool IsInMagicMenu() {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen(kMagicMenu);
        }

        // Retorna true quando o HUD deve ser desenhado.
        bool ShouldDraw() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return false;

            auto* ui = RE::UI::GetSingleton();
            if (!ui) return false;

            static const RE::BSFixedString mainMenu{"Main Menu"};
            static const RE::BSFixedString loadingMenu{"Loading Menu"};
            static const RE::BSFixedString faderMenu{"Fader Menu"};
            if (ui->IsMenuOpen(mainMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(faderMenu)) {
                return false;
            }

            // MagicMenu: sempre mostrar
            if (ui->IsMenuOpen(kMagicMenu)) return true;

            static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
            static const RE::BSFixedString statsMenu{"StatsMenu"};
            static const RE::BSFixedString mapMenu{"MapMenu"};
            static const RE::BSFixedString journalMenu{"Journal Menu"};
            static const RE::BSFixedString containerMenu{"ContainerMenu"};
            static const RE::BSFixedString barterMenu{"BarterMenu"};
            static const RE::BSFixedString craftingMenu{"Crafting Menu"};
            static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
            static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
            static const RE::BSFixedString dialogueMenu{"Dialogue Menu"};
            static const RE::BSFixedString console{"Console"};
            static const RE::BSFixedString mcm{"Mod Configuration Menu"};

            if (ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(statsMenu) || ui->IsMenuOpen(mapMenu) ||
                ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) ||
                ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(lockpickingMenu) || ui->IsMenuOpen(sleepWaitMenu) ||
                ui->IsMenuOpen(dialogueMenu) || ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm)) {
                return false;
            }

            return true;
        }

        struct Palette {
            ImU32 fill;
            ImU32 glow;
        };

        Palette SchoolPalette(RE::ActorValue av) {
            switch (av) {
                using enum RE::ActorValue;
                case kAlteration:
                    return {IM_COL32(100, 180, 255, 200), IM_COL32(100, 180, 255, 70)};
                case kConjuration:
                    return {IM_COL32(160, 100, 240, 200), IM_COL32(160, 100, 240, 70)};
                case kDestruction:
                    return {IM_COL32(255, 75, 55, 200), IM_COL32(255, 75, 55, 70)};
                case kIllusion:
                    return {IM_COL32(230, 175, 35, 200), IM_COL32(230, 175, 35, 70)};
                case kRestoration:
                    return {IM_COL32(70, 215, 120, 200), IM_COL32(70, 215, 120, 70)};
                default:
                    return {IM_COL32(100, 100, 100, 160), IM_COL32(0, 0, 0, 0)};
            }
        }

        Palette SpellPalette(RE::SpellItem const* spell) {
            if (!spell) return {IM_COL32(40, 40, 40, 120), IM_COL32(0, 0, 0, 0)};
            const auto* fx = spell->GetCostliestEffectItem();
            if (!fx || !fx->baseEffect) return SchoolPalette(RE::ActorValue::kNone);
            auto av = fx->baseEffect->GetMagickSkill();
            if (av == RE::ActorValue::kNone) av = fx->baseEffect->data.primaryAV;
            return SchoolPalette(av);
        }

        void FillSector(ImDrawList* dl, ImVec2 c, float r, float a0, float a1, ImU32 col, int segments = 24) {
            DL::PathLineTo(dl, c);
            DL::PathArcTo(dl, c, r, a0, a1, segments);
            DL::PathLineTo(dl, c);
            DL::PathFillConvex(dl, col);
        }

        void DrawGlow(ImDrawList* dl, ImVec2 c, float r, ImU32 glowCol) {
            const ImU32 base = glowCol & 0x00FFFFFFu;
            const auto baseA = static_cast<int>((glowCol >> 24) & 0xFF);
            for (int i = 5; i >= 1; --i) {
                const ImU32 layer = base | (static_cast<ImU32>(baseA / (i + 1)) << 24);
                DL::AddCircle(dl, c, r + static_cast<float>(i) * 2.5f, layer, 48, 1.2f);
            }
        }

        void DrawSlot(ImDrawList* dl, ImVec2 center, float r, int slot, bool isActive) {
            using Hand = MagicSlots::Hand;

            const auto rightID = MagicSlots::GetSlotSpell(slot, Hand::Right);
            const auto leftID = MagicSlots::GetSlotSpell(slot, Hand::Left);
            auto const* rSpell = rightID ? RE::TESForm::LookupByID<RE::SpellItem>(rightID) : nullptr;
            auto const* lSpell = leftID ? RE::TESForm::LookupByID<RE::SpellItem>(leftID) : nullptr;

            const auto rPal = SpellPalette(rSpell);
            const auto lPal = SpellPalette(lSpell);

            if (isActive) {
                DrawGlow(dl, center, r, rPal.glow);
                DrawGlow(dl, center, r, lPal.glow);
            }

            DL::AddCircleFilled(dl, center, r, isActive ? IM_COL32(30, 22, 15, 230) : IM_COL32(12, 12, 12, 200), 48);

            FillSector(dl, center, r - 2.f, -kPI * 0.5f, kPI * 0.5f, rPal.fill);
            FillSector(dl, center, r - 2.f, kPI * 0.5f, kPI * 1.5f, lPal.fill);

            DL::AddLine(dl, {center.x, center.y - r + 3.f}, {center.x, center.y + r - 3.f}, IM_COL32(0, 0, 0, 180),
                        kDivWidth);

            if (isActive) {
                const double t = ImGui::GetTime();
                const double pulse = 0.65 + 0.35 * std::sin(t * 4.5);
                const ImU32 ring = IM_COL32(255, static_cast<int>(210 * pulse), static_cast<int>(50 * pulse), 245);
                DL::AddCircle(dl, center, r, ring, 48, 2.5f);
                DL::AddCircle(dl, center, r + 3.f, (ring & 0x00FFFFFFu) | (70u << 24), 48, 1.0f);
            } else {
                DL::AddCircle(dl, center, r, IM_COL32(70, 70, 70, 150), 48, 1.2f);
            }

            if (!rSpell && !lSpell) {
                const float d = r * 0.32f;
                const ImU32 xc = IM_COL32(90, 90, 90, 110);
                DL::AddLine(dl, {center.x - d, center.y - d}, {center.x + d, center.y + d}, xc, 1.f);
                DL::AddLine(dl, {center.x + d, center.y - d}, {center.x - d, center.y + d}, xc, 1.f);
            }

            ImGuiIO const* io = ImGui::GetIO();
            const float dx = io->MousePos.x - center.x;
            const float dy = io->MousePos.y - center.y;
            if (dx * dx + dy * dy < r * r) {
                ImGui::BeginTooltip();
                ImGui::TextDisabled("Slot %d", slot + 1);
                ImGui::Separator();
                ImGui::Text("R: %s", rSpell ? rSpell->GetName() : "—");
                ImGui::Text("L: %s", lSpell ? lSpell->GetName() : "—");
                ImGui::EndTooltip();
            }
        }

        ImVec2 SlotCenter(ImVec2 ringCenter, float ringRadius, int i, int n) {
            const float angle = kPI + (2.f * kPI / static_cast<float>(n)) * static_cast<float>(i);
            return {ringCenter.x + ringRadius * std::cos(angle), ringCenter.y + ringRadius * std::sin(angle)};
        }

        void DrawRingCenter(ImDrawList* dl, ImVec2 c) {
            DL::AddCircleFilled(dl, c, 4.f, IM_COL32(70, 70, 70, 150), 16);
            DL::AddCircle(dl, c, 4.f, IM_COL32(110, 110, 110, 170), 16, 1.f);
        }
    }

    void Draw() {
        if (!ShouldDraw()) return;

        const auto n = static_cast<int>(MagicSlots::GetSlotCount());
        if (n <= 0) return;

        const bool inMagicMenu = IsInMagicMenu();
        ImGuiIO const* io = ImGui::GetIO();

        const ImVec2 ringCenter = {io->DisplaySize.x - kMargin, io->DisplaySize.y - kMargin};

        const float half = kRingRadius + kSlotRadius + kGlowPad;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

        ImGui::SetNextWindowPos({ringCenter.x - half, ringCenter.y - half}, 0, {0.f, 0.f});
        ImGui::SetNextWindowSize({half * 2.f, half * 2.f});
        ImGui::SetNextWindowBgAlpha(0.f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoDecoration;

        // 🔑 Só bloqueia input fora do MagicMenu
        if (!inMagicMenu) {
            flags |= ImGuiWindowFlags_NoInputs;
        }

        ImGui::Begin("##IMAGIC_HUD", nullptr, flags);

        auto* dl = ImGui::GetWindowDrawList();

        DrawRingCenter(dl, ringCenter);

        const int activeSlot = MagicState::Get().ActiveSlot();
        for (int i = 0; i < n; ++i) {
            DrawSlot(dl, SlotCenter(ringCenter, kRingRadius, i, n), kSlotRadius, i, activeSlot == i);
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) return;
        static auto const* hudElement = SKSEMenuFramework::AddHudElement(Draw);
        (void)hudElement;
    }
}