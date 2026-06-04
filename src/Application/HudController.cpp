#include "Application/HudController.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "Application/InputController.h"
#include "Domain/SlotCooldownTracker.h"
#include "Domain/SlotCostUtil.h"
#include "Domain/State.h"
#include "PCH.h"
#include "Shared/InputIntents.h"
#include "UI/FontLoader.h"
#include "UI/HudFrameLogic.h"
#include "UI/HudManager.h"
#include "UI/HudState.h"
#include "UI/HudView.h"
#include "UI/TextureManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Application {

    HudController& HudController::Get() {
        static HudController inst;
        return inst;
    }

    namespace {
        bool ComputeHardBlocked() {
            if (!RE::PlayerCharacter::GetSingleton()) return true;
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return true;
            static const RE::BSFixedString mainMenu{"Main Menu"};
            static const RE::BSFixedString loadingMenu{"Loading Menu"};
            static const RE::BSFixedString faderMenu{"Fader Menu"};
            return ui->IsMenuOpen(mainMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(faderMenu);
        }

        bool ComputeSoftBlocked() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return true;
            static const RE::BSFixedString menus[] = {
                "MagicMenu"sv,
                "TweenMenu"sv,
                "InventoryMenu"sv,
                "StatsMenu"sv,
                "MapMenu"sv,
                "Journal Menu"sv,
                "ContainerMenu"sv,
                "BarterMenu"sv,
                "Crafting Menu"sv,
                "Lockpicking Menu"sv,
                "Sleep/Wait Menu"sv,
                "Dialogue Menu"sv,
                "Console"sv,
                "Mod Configuration Menu"sv,
                "BestiaryMenu"sv,
                "OstimSceneMenu"sv,
                "Dialogue Topic Menu"sv,
            };
            for (const auto& m : menus)
                if (ui->IsMenuOpen(m)) return true;
            return false;
        }

        bool ComputeInMagicMenu() {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return false;
            static const RE::BSFixedString magicMenu{"MagicMenu"};
            return ui->IsMenuOpen(magicMenu);
        }

        void SetMagicMenuVisible(bool visible) {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) return;
            auto menu = ui->GetMenu<RE::MagicMenu>();
            if (!menu || !menu->uiMovie) return;
            RE::GFxValue val(visible);
            menu->uiMovie->SetVariable("_root.Menu_mc._visible", val);
        }

        void HandleHudToggle() {
            using namespace IntegratedMagic::HUD;
            const bool willOpen = !g_popupOpen.load();
            g_popupOpen.store(willOpen);
            if (willOpen) {
                g_popupJustOpened.store(true, std::memory_order_relaxed);
                SetMagicMenuVisible(false);
            } else {
                SetMagicMenuVisible(true);
            }
        }
    }

    void HudController::OnFrame() {
        using namespace IntegratedMagic::HUD;
        auto& input = InputController::Get();

        const bool hardBlocked = ComputeHardBlocked();
        g_hardBlocked.store(hardBlocked);

        if (hardBlocked) {
            if (g_popupOpen.load()) g_popupOpen.store(false);
            Input::detail::g_popupOpenForInput.store(false, std::memory_order_relaxed);
            return;
        }

        IntegratedMagic::HUD::RefreshSlotCount();
        if (g_slotCount.load() == 0) {
            Input::detail::g_popupOpenForInput.store(false, std::memory_order_relaxed);
            return;
        }

        const bool inMagicMenu = ComputeInMagicMenu();
        g_inMagicMenu.store(inMagicMenu);

        if (inMagicMenu && input.ConsumeHudToggle()) HandleHudToggle();
        if (!inMagicMenu && g_popupOpen.load()) g_popupOpen.store(false);

        g_softBlocked.store(ComputeSoftBlocked());
        IntegratedMagic::HUD::EvaluateAndStoreHudVisibility(IntegratedMagic::MagicState::Get().IsActive());
        g_modifierHeld.store(input.IsModifierHeld());

        for (const auto& e : Input::detail::DrainPopupInputs()) {
            using K = Input::detail::PopupInputKind;
            switch (e.kind) {
                case K::MouseDelta:
                case K::StickDelta:
                    IntegratedMagic::HUD::FeedMouseDelta(e.dx, e.dy);
                    break;
                case K::Click:
                    IntegratedMagic::HUD::FeedMouseClick();
                    break;
                case K::RightClick:
                    IntegratedMagic::HUD::FeedMouseRightClick();
                    break;
                case K::Close:
                    g_popupOpen.store(false);
                    break;
            }
        }

        IntegratedMagic::HUD::ExecutePopupIntents();

        static bool s_lastPopupOpen = false;
        const bool nowPopupOpen = g_popupOpen.load();
        if (s_lastPopupOpen && !nowPopupOpen) SetMagicMenuVisible(true);
        s_lastPopupOpen = nowPopupOpen;

        const float dt = input.GetDeltaTime();
        IntegratedMagic::SlotCooldownTracker::Get().Update(dt);

        HudView v{};
        v.slotCount = g_slotCount.load();
        v.activeSlot = IntegratedMagic::MagicState::Get().ActiveSlot();
        v.spellSystemActive = IntegratedMagic::MagicState::Get().IsActive();
        v.modifierHeld = g_modifierHeld.load();
        const int n = std::min(v.slotCount, kMaxViewSlots);
        for (int i = 0; i < n; ++i) {
            const auto afford = IntegratedMagic::ComputeSlotAffordability(i);
            v.slots[i].hasSpells = afford.hasSpells;
            v.slots[i].canCast = afford.canCast;
            const auto cd = IntegratedMagic::SlotCooldownTracker::Get().GetSlotInfo(i);
            v.slots[i].onCooldown = cd.onCooldown;
            v.slots[i].justFinishedCooldown = cd.justFinished;
            v.slots[i].cooldownProgress = cd.progress;
        }
        IntegratedMagic::HUD::FillHudViewFromConfig(v);

        if (inMagicMenu && !nowPopupOpen) {
            static std::uint64_t s_wasDown = 0;
            std::uint64_t nowDown = 0;
            for (int i = 0; i < n; ++i) {
                if (input.IsSlotHotkeyDown(i)) nowDown |= (1uLL << static_cast<std::uint64_t>(i));
            }
            const std::uint64_t justPressed = nowDown & ~s_wasDown;
            s_wasDown = nowDown;
            IntegratedMagic::HUD::ExecuteHotkeyAssignment(justPressed, n);
        }

        Input::detail::g_popupOpenForInput.store(nowPopupOpen, std::memory_order_relaxed);
    }

    void HudController::InitializeGraphics() {
        IntegratedMagic::TextureManager::Init();
        FontLoader::LoadFontsFromConfig();
    }

    void HudController::RenderFrame(float backbufferW, float backbufferH) {
        IntegratedMagic::HUD::g_backbufferW.store(backbufferW, std::memory_order_relaxed);
        IntegratedMagic::HUD::g_backbufferH.store(backbufferH, std::memory_order_relaxed);
        ImGui::NewFrame();
        IntegratedMagic::HUD::DrawHudFrame();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void HudController::OnWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_KILLFOCUS) {
            auto& io = ImGui::GetIO();
            io.ClearInputCharacters();
            io.ClearInputKeys();
        }
        const bool popupOpen = IntegratedMagic::HUD::IsDetailPopupOpen();
        const bool isMouseMsg =
            (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP ||
             uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP || uMsg == WM_MOUSEMOVE || uMsg == WM_MOUSEWHEEL);
        if (!isMouseMsg || popupOpen) {
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        }
    }
}
