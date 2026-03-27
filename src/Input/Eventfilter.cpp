#include "EventFilter.h"

#include <ranges>

#include "Config/Config.h"
#include "ExclusivePending.h"
#include "HotkeyCache.h"
#include "HudToggle.h"
#include "Input.h"
#include "PCH.h"
#include "ReplaySystem.h"
#include "SKSEMenuFramework.h"
#include "State/State.h"
#include "UI/HUD.h"

namespace Input::detail {

    namespace {

        int GamepadIdToIndex(int idCode) {
            using Key = RE::BSWin32GamepadDevice::Key;
            switch (static_cast<Key>(idCode)) {
                case Key::kUp:
                    return 0;
                case Key::kDown:
                    return 1;
                case Key::kLeft:
                    return 2;
                case Key::kRight:
                    return 3;
                case Key::kStart:
                    return 4;
                case Key::kBack:
                    return 5;
                case Key::kLeftThumb:
                    return 6;
                case Key::kRightThumb:
                    return 7;
                case Key::kLeftShoulder:
                    return 8;
                case Key::kRightShoulder:
                    return 9;
                case Key::kA:
                    return 10;
                case Key::kB:
                    return 11;
                case Key::kX:
                    return 12;
                case Key::kY:
                    return 13;
                case Key::kLeftTrigger:
                    return 14;
                case Key::kRightTrigger:
                    return 15;
                default:
                    return -1;
            }
        }

        void UpdateDownState(RE::INPUT_DEVICE dev, int convertedCode, bool downNow) {
            if (dev == RE::INPUT_DEVICE::kKeyboard)
                g_kbDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
            else if (dev == RE::INPUT_DEVICE::kGamepad)
                g_gpDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
        }

        bool TryHandleCapture(const RE::ButtonEvent* btn, CaptureState& cap, bool& wantCapture, RE::INPUT_DEVICE dev,
                              int convertedCode) {
            if (!wantCapture || !btn->IsDown()) return false;
            if (dev == RE::INPUT_DEVICE::kKeyboard && convertedCode == kDIK_Escape) return false;

            int encoded = -1;
            if (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kMouse)
                encoded = convertedCode;
            else if (dev == RE::INPUT_DEVICE::kGamepad)
                encoded = -(convertedCode + 2);
            if (encoded == -1) return false;

#ifdef DEBUG
            spdlog::info("[Input] TryHandleCapture: captured dev={} code={} encoded={}", static_cast<int>(dev),
                         convertedCode, encoded);
#endif
            cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
            cap.captureRequested.store(false, std::memory_order_relaxed);
            wantCapture = false;
            return true;
        }

        bool HasTransformArchetype(const RE::MagicItem* item) {
            if (!item) return false;
            using ArchetypeID = RE::EffectArchetypes::ArchetypeID;
            return std::ranges::any_of(item->effects, [](const auto* effect) {
                if (!effect || !effect->baseEffect) return false;
                const auto arch = effect->baseEffect->GetArchetype();
                return arch == ArchetypeID::kWerewolf || arch == ArchetypeID::kVampireLord;
            });
        }

        bool IsTransformPowerEquipped(RE::PlayerCharacter* pc) {
            if (!pc) return false;
            const auto& rd = pc->GetActorRuntimeData();
            if (!rd.selectedPower) return false;
            return HasTransformArchetype(rd.selectedPower->As<RE::MagicItem>());
        }

        void HandleSlotPressed(int slot) {
            if (slot < 0 || slot >= ActiveSlots()) return;
            if (!RE::PlayerCharacter::GetSingleton()) return;
#ifdef DEBUG
            spdlog::info("[Input] HandleSlotPressed: slot={}", slot);
#endif
            IntegratedMagic::MagicState::Get().OnSlotPressed(slot);
        }

        void HandleSlotReleased(int slot) {
            if (slot < 0 || slot >= ActiveSlots()) return;
#ifdef DEBUG
            spdlog::info("[Input] HandleSlotReleased: slot={}", slot);
#endif
            IntegratedMagic::MagicState::Get().OnSlotReleased(slot);
        }

        void DispatchSlots() {
            for (auto s = Input::ConsumePressedSlot(); s.has_value(); s = Input::ConsumePressedSlot())
                HandleSlotPressed(*s);
            for (auto s = Input::ConsumeReleasedSlot(); s.has_value(); s = Input::ConsumeReleasedSlot())
                HandleSlotReleased(*s);
        }

        void DrainWhenBlocked() {
#ifdef DEBUG
            int drained = 0;
#endif
            for (auto s = Input::ConsumePressedSlot(); s.has_value(); s = Input::ConsumePressedSlot())
#ifdef DEBUG
                ++drained;
            if (drained > 0)
                spdlog::info("[Input] DrainWhenBlocked: discarded {} pressed slot(s) (input blocked)", drained);
#endif
            for (auto ss = Input::ConsumeReleasedSlot(); ss.has_value(); ss = Input::ConsumeReleasedSlot()) {
#ifdef DEBUG
                spdlog::info("[Input] DrainWhenBlocked: releasing slot={} while blocked", *ss);
#endif
                HandleSlotReleased(*ss);
            }
        }

        bool ShouldFilterAndSave(RE::INPUT_DEVICE dev, int convertedCode, std::uint32_t rawIdCode,
                                 const RE::BSFixedString& userEvent, float value, float heldSecs) {
            const int effectiveKbCode =
                (dev == RE::INPUT_DEVICE::kMouse) ? (kMouseButtonBase + convertedCode) : convertedCode;

            const int n = ActiveSlots();
            for (int slot = 0; slot < n; ++slot) {
                const auto s = static_cast<std::size_t>(slot);
                const auto& hk = g_cache[s];
                const bool inKb = (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kMouse) &&
                                  ComboContains(hk.kb, effectiveKbCode);
                const bool inGp = dev == RE::INPUT_DEVICE::kGamepad && ComboContains(hk.gp, convertedCode);
                if (!inKb && !inGp) continue;

                if (g_slotDown[s].load(std::memory_order_relaxed)) return true;

                if (g_slotWasAccepted[s]) {
#ifdef DEBUG
                    spdlog::info("[Input] ShouldFilterAndSave: slot={} code={} dev={} FILTERED (wasAccepted)", slot,
                                 effectiveKbCode, static_cast<int>(dev));
#endif
                    return true;
                }

                if (inKb && ComboDown(hk.kb, g_kbDown)) return true;
                if (inGp && ComboDown(hk.gp, g_gpDown)) return true;

                if (ReplayMatchesEvent(s, dev, rawIdCode, userEvent, value)) {
#ifdef DEBUG
                    spdlog::info("[Input] ShouldFilterAndSave: slot={} code={} replay PASS-THROUGH", slot,
                                 effectiveKbCode);
#endif
                    ResetReplayState(s);
                    return false;
                }

                if (g_slotIsMultiKey[s] && !HasExclusivePending(s)) {
                    const bool simPatch = IntegratedMagic::GetMagicConfig().pressBothAtSamePatch && g_slotIsMultiKey[s];
                    const bool replayInProgress = g_replay[s].armed || HasDeferredReplayForSlot(s);
                    if ((simPatch && !g_simWindowActive[s]) || replayInProgress) continue;

                    bool sharedWithActiveSlot = false;
                    for (int other = 0; other < n; ++other) {
                        if (other == slot) continue;
                        if (!g_slotDown[static_cast<std::size_t>(other)].load(std::memory_order_relaxed)) continue;
                        const auto& otherHk = g_cache[static_cast<std::size_t>(other)];
                        if (inKb && ComboContains(otherHk.kb, effectiveKbCode)) {
                            sharedWithActiveSlot = true;
                            break;
                        }
                        if (inGp && ComboContains(otherHk.gp, convertedCode)) {
                            sharedWithActiveSlot = true;
                            break;
                        }
                    }
                    if (sharedWithActiveSlot) return true;

#ifdef DEBUG
                    spdlog::info(
                        "[Input] ShouldFilterAndSave: slot={} code={} starting exclusive pending (multiKey, no pending "
                        "yet)",
                        slot, effectiveKbCode);
#endif
                    g_exclusivePendingSrc[s] = inGp ? PendingSrc::Gp : PendingSrc::Kb;
                    g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
                }

                if (HasExclusivePending(s)) {
#ifdef DEBUG
                    spdlog::info(
                        "[Input] ShouldFilterAndSave: slot={} code={} RETAINED (exclusive pending, value={:.2f}, "
                        "heldSecs={:.3f})",
                        slot, effectiveKbCode, value, heldSecs);
#endif
                    g_retainedEvents[s].emplace_back(RetainedEvent{dev, rawIdCode, userEvent, value, heldSecs});
                    return true;
                }
            }
            return false;
        }

    }

    bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        if (ui->GameIsPaused()) return true;
        if (SKSEMenuFramework::IsAnyBlockingWindowOpened()) return true;
        if (g_captureModeActive.load(std::memory_order_relaxed)) return true;

        static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        static const RE::BSFixedString statsMenu{"StatsMenu"};
        static const RE::BSFixedString mapMenu{"MapMenu"};
        static const RE::BSFixedString journalMenu{"Journal Menu"};
        static const RE::BSFixedString favoritesMenu{"FavoritesMenu"};
        static const RE::BSFixedString containerMenu{"ContainerMenu"};
        static const RE::BSFixedString barterMenu{"BarterMenu"};
        static const RE::BSFixedString trainingMenu{"Training Menu"};
        static const RE::BSFixedString craftingMenu{"Crafting Menu"};
        static const RE::BSFixedString giftMenu{"GiftMenu"};
        static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
        static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
        static const RE::BSFixedString loadingMenu{"Loading Menu"};
        static const RE::BSFixedString mainMenu{"Main Menu"};
        static const RE::BSFixedString console{"Console"};
        static const RE::BSFixedString mcm{"Mod Configuration Menu"};
        static const RE::BSFixedString tweenMenu{"Tween Menu"};

        return ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
               ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
               ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
               ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
               ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
               ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) || ui->IsMenuOpen(tweenMenu);
    }

    void ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap, bool& wantCapture) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        for (auto* e = *a_evns; e; e = e->next) {
            const auto* btn = e->AsButtonEvent();
            if (!btn || (!btn->IsDown() && !btn->IsUp())) continue;

            const auto dev = btn->GetDevice();
            auto code = static_cast<int>(btn->idCode);

            if (dev == RE::INPUT_DEVICE::kGamepad) code = GamepadIdToIndex(code);

            if (dev == RE::INPUT_DEVICE::kMouse) {
                const int mouseCode = kMouseButtonBase + code;
                if (mouseCode >= 0 && mouseCode < kMaxCode) {
                    (void)TryHandleCapture(btn, cap, wantCapture, RE::INPUT_DEVICE::kMouse, mouseCode);
                    g_kbDown[static_cast<std::size_t>(mouseCode)].store(btn->IsDown(), std::memory_order_relaxed);
                }
                continue;
            }

            if (code < 0 || code >= kMaxCode) continue;

            (void)TryHandleCapture(btn, cap, wantCapture, dev, code);
            UpdateDownState(dev, code, btn->IsDown());

            if (btn->IsDown() && player && btn->QUserEvent() == "Shout"sv) {
                if (IsTransformPowerEquipped(player)) {
#ifdef DEBUG
                    spdlog::info(
                        "[Input] ProcessButtonEvents: Shout pressed with transform power equipped -> "
                        "ForceExitNoRestore");
#endif
                    IntegratedMagic::MagicState::Get().ForceExitNoRestore();
                }
            }
        }
    }

    void FilterMouseForPopup(RE::InputEvent** a_evns) {
        if (!IntegratedMagic::HUD::IsDetailPopupOpen()) return;

        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;

        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (cur->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                auto const* mm = static_cast<RE::MouseMoveEvent*>(cur);
                IntegratedMagic::HUD::FeedMouseDelta(static_cast<float>(mm->mouseInputX),
                                                     static_cast<float>(mm->mouseInputY));
                remove = true;

            } else if (cur->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto const* ts = static_cast<RE::ThumbstickEvent*>(cur);
                if (ts->IsLeft()) {
                    constexpr float kDeadzone = 0.15f;
                    constexpr float kSensitivity = 12.f;
                    const float ax = (std::abs(ts->xValue) > kDeadzone) ? ts->xValue : 0.f;
                    const float ay = (std::abs(ts->yValue) > kDeadzone) ? ts->yValue : 0.f;
                    if (ax != 0.f || ay != 0.f)
                        IntegratedMagic::HUD::FeedMouseDelta(ax * kSensitivity, -ay * kSensitivity);
                }
                remove = true;

            } else if (const auto* btn = cur->AsButtonEvent()) {
                using Key = RE::BSWin32GamepadDevice::Key;
                const auto btnDev = btn->GetDevice();
                const auto btnID = btn->GetIDCode();

                if (btnDev == RE::INPUT_DEVICE::kMouse && btnID == 0) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseClick();
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kMouse && btnID == 1) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseRightClick();
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kX)) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseClick();
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kB)) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseRightClick();
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kY)) {
                    if (btn->IsDown()) IntegratedMagic::HUD::CloseDetailPopup();
                    remove = true;
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }

    void FilterEvents(RE::InputEvent** a_evns) {
        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;

        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (const auto* btn = cur->AsButtonEvent()) {
                const auto dev = btn->GetDevice();
                auto code = static_cast<int>(btn->idCode);
                const auto rawCode = btn->idCode;

                if (dev == RE::INPUT_DEVICE::kGamepad) code = GamepadIdToIndex(code);

                if (code >= 0 && code < kMaxCode) {
                    remove =
                        ShouldFilterAndSave(dev, code, rawCode, btn->QUserEvent(), btn->Value(), btn->HeldDuration()) ||
                        ShouldFilterHudToggle(dev, code);
#ifdef DEBUG
                    if (!remove && (dev == RE::INPUT_DEVICE::kMouse || dev == RE::INPUT_DEVICE::kKeyboard)) {
                        const int effCode = (dev == RE::INPUT_DEVICE::kMouse) ? kMouseButtonBase + code : code;
                        const int n = ActiveSlots();
                        for (int slot = 0; slot < n; ++slot) {
                            if (ComboContains(g_cache[static_cast<std::size_t>(slot)].kb, effCode)) {
                                spdlog::info(
                                    "[Input] FilterEvents: slot={} code={} dev={} value={:.2f} PASSING TO ENGINE", slot,
                                    effCode, static_cast<int>(dev), btn->Value());
                                break;
                            }
                        }
                    }
#endif
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    *a_evns = next;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }

    void UpdateSlotsIfAllowed(bool blocked, float dt) {
        if (!blocked)
            RecomputeSlotEdges(dt);
        else
            DrainWhenBlocked();
    }

    void DispatchIfAllowed(bool blocked, float dt) {
        if (!blocked) {
            DispatchSlots();
            IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);
            IntegratedMagic::MagicState::Get().PumpAutomatic(dt);
        }
    }

}