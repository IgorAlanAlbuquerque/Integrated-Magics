#include "HudManager.h"

#include <imgui.h>

#include "Config/Config.h"
#include "Config/Slots.h"
#include "HudState.h"
#include "Input/Input.h"
#include "PCH.h"
#include "PopupDrawer.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"
#include "SlotDrawer.h"
#include "State/State.h"

namespace IntegratedMagic::HUD {

    bool IsHardBlocked() {
        if (!RE::PlayerCharacter::GetSingleton()) return true;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
        static const RE::BSFixedString mainMenu{"Main Menu"};
        static const RE::BSFixedString loadingMenu{"Loading Menu"};
        static const RE::BSFixedString faderMenu{"Fader Menu"};
        return ui->IsMenuOpen(mainMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(faderMenu);
    }

    bool IsSoftBlocked() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        static const RE::BSFixedString tweenMenu{"TweenMenu"};
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
        static const RE::BSFixedString bestiary{"BestiaryMenu"};
        static const RE::BSFixedString ostim{"OstimSceneMenu"};
        static const RE::BSFixedString dialogueTopicMenu{"Dialogue Topic Menu"};
        return ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(statsMenu) || ui->IsMenuOpen(mapMenu) ||
               ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) ||
               ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(lockpickingMenu) || ui->IsMenuOpen(sleepWaitMenu) ||
               ui->IsMenuOpen(dialogueMenu) || ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) ||
               ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(tweenMenu) || ui->IsMenuOpen(bestiary) ||
               ui->IsMenuOpen(ostim) || ui->IsMenuOpen(dialogueTopicMenu);
    }

    bool IsInMagicMenu() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        return ui->IsMenuOpen(magicMenu);
    }

    bool EvaluateHudVisibility() {
        using F = IntegratedMagic::HudVisibilityFlag;
        const auto& cfg = IntegratedMagic::GetMagicConfig();
        if (cfg.hudVisibilityFlags == 0) return false;
        if (cfg.HudFlagSet(F::Always)) return true;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        if (cfg.HudFlagSet(F::SlotActive) && IntegratedMagic::MagicState::Get().IsActive()) return true;
        if (cfg.HudFlagSet(F::InCombat) && player->IsInCombat()) return true;
        if (cfg.HudFlagSet(F::WeaponDrawn)) {
            using WS = RE::WEAPON_STATE;
            const auto ws = player->AsActorState()->GetWeaponState();
            if (ws == WS::kDrawn || ws == WS::kWantToDraw || ws == WS::kDrawing) return true;
        }
        return false;
    }

    void DrawHudFrame() {
        if (IsHardBlocked()) {
            if (g_popupOpen.load()) g_popupOpen.store(false);
            return;
        }
        if (Slots::GetSlotCount() == 0) return;

        const bool inMagicMenu = IsInMagicMenu();
        if (inMagicMenu && Input::ConsumeHudToggle()) ToggleDetailPopup();
        if (!inMagicMenu && g_popupOpen.load()) g_popupOpen.store(false);

        if (EvaluateHudVisibility()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
            if (!IsSoftBlocked()) SlotDrawer::DrawSmallHUD(ImGui::GetIO());
            ImGui::PopStyleVar(2);
        }

        if (g_popupOpen.load()) PopupDrawer::DrawDetailPopup();
    }

    bool IsDetailPopupOpen() { return g_popupOpen.load(std::memory_order_relaxed); }

    void FeedMouseDelta(float dx, float dy) {
        const ImGuiIO& io = ImGui::GetIO();
        g_mousePos.x = std::clamp(g_mousePos.x + dx, 0.f, io.DisplaySize.x);
        g_mousePos.y = std::clamp(g_mousePos.y + dy, 0.f, io.DisplaySize.y);
    }

    void FeedMouseClick() { g_mouseClicked.store(true, std::memory_order_relaxed); }
    void FeedMouseRightClick() { g_mouseRightClicked.store(true, std::memory_order_relaxed); }

    void ToggleDetailPopup() {
        const bool willOpen = !g_popupOpen.load();
        g_popupOpen.store(willOpen);
        if (willOpen) g_popupJustOpened.store(true, std::memory_order_relaxed);
    }

    void CloseDetailPopup() { g_popupOpen.store(false); }
    bool IsHudVisible() { return g_hudVisible.load(std::memory_order_relaxed); }
    void SetHudVisible(bool v) { g_hudVisible.store(v, std::memory_order_relaxed); }

}