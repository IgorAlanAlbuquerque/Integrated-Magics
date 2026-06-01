#include "Input/InputFilter.h"

#include <ranges>

#include "Config/ConfigAdapter.h"
#include "Input/ExclusiveTracker.h"
#include "Input/HotkeyMatcher.h"
#include "Input/HudToggle.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "Shared/InputIntents.h"

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

        void UpdateDownState(RE::INPUT_DEVICE dev, int convertedCode, bool downNow, KeyStateStore& keys) {
            if (dev == RE::INPUT_DEVICE::kKeyboard)
                keys.kbDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
            else if (dev == RE::INPUT_DEVICE::kGamepad)
                keys.gpDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
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

            MAGIC_DEBUG_LOG("[Input] TryHandleCapture: captured dev={} code={} encoded={}", static_cast<int>(dev),
                            convertedCode, encoded);
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

        std::optional<int> ConsumeBitLocal(std::atomic<std::uint64_t>& mask, const SlotEdgeStore& slots) {
            while (true) {
                const int n = slots.ActiveSlots();
                const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);
                std::uint64_t curAll = mask.load(std::memory_order_relaxed);
                std::uint64_t cur = curAll & allowed;
                if (cur == 0uLL) {
                    if (curAll != 0uLL)
                        (void)mask.compare_exchange_weak(curAll, curAll & allowed, std::memory_order_relaxed);
                    return std::nullopt;
                }
                int idx = -1;
                for (int i = 0; i < n; ++i)
                    if (cur & (1uLL << i)) {
                        idx = i;
                        break;
                    }
                if (idx < 0) return std::nullopt;
                if (mask.compare_exchange_weak(curAll, curAll & ~(1uLL << idx), std::memory_order_relaxed)) return idx;
            }
        }

        void DrainWhenBlocked(const SlotEdgeStore& slots, std::atomic<std::uint64_t>& pressedMask,
                              std::atomic<std::uint64_t>& releasedMask) {
            int drained = 0;
            while (ConsumeBitLocal(pressedMask, slots).has_value()) ++drained;
            if (drained > 0) MAGIC_DEBUG_LOG("[Input] DrainWhenBlocked: discarded {} pressed slot(s)", drained);
            while (ConsumeBitLocal(releasedMask, slots).has_value());
        }

        bool ShouldFilterAndSave(RE::INPUT_DEVICE dev, int convertedCode, std::uint32_t rawIdCode,
                                 const RE::BSFixedString& userEvent, float value, float heldSecs,
                                 const KeyStateStore& keys, const SlotEdgeStore& slots, const HotkeyCacheStore& cache,
                                 ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained) {
            const int effectiveKbCode =
                (dev == RE::INPUT_DEVICE::kMouse) ? (kMouseButtonBase + convertedCode) : convertedCode;

            const int n = slots.ActiveSlots();
            for (int slot = 0; slot < n; ++slot) {
                const auto s = static_cast<std::size_t>(slot);
                const auto& hk = cache.slots[s];
                const bool inKb = (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kMouse) &&
                                  ComboContains(hk.kb, effectiveKbCode);
                const bool inGp = dev == RE::INPUT_DEVICE::kGamepad && ComboContains(hk.gp, convertedCode);
                if (!inKb && !inGp) continue;

                if (slots.slotDown[s].load(std::memory_order_relaxed)) return true;
                if (slots.slotWasAccepted[s]) {
                    MAGIC_DEBUG_LOG("[Input] ShouldFilterAndSave: slot={} FILTERED (wasAccepted)", slot);
                    return true;
                }
                if (inKb && ComboDown(hk.kb, keys.kbDown)) return true;
                if (inGp && ComboDown(hk.gp, keys.gpDown)) return true;

                if (ReplayMatchesEvent(s, dev, rawIdCode, userEvent, value, replay)) {
                    ResetReplayState(s, replay);
                    return false;
                }

                if (excl.deactivatedThisPress[s]) {
                    if (value < 0.5f) {
                        excl.deactivatedThisPress[s] = false;
                        return true;
                    }
                }

                if (!excl.filterWindowActive[s]) {
                    excl.filterWindowActive[s] = true;
                    excl.filterWindowTimer[s] = kFilterReplayDelaySec;
                }
                retained[s].push_back({dev, rawIdCode, userEvent, value, heldSecs});
                return true;
            }
            return false;
        }

    }

    bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return true;
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
        static const RE::BSFixedString dialogueMenu{"Dialogue Menu"};
        static const RE::BSFixedString dialogueTopicMenu{"Dialogue Topic Menu"};
        static const RE::BSFixedString bestiary{"BestiaryMenu"};
        static const RE::BSFixedString ostim{"OstimSceneMenu"};

        return ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
               ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
               ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
               ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
               ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
               ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) || ui->IsMenuOpen(tweenMenu) ||
               ui->IsMenuOpen(dialogueMenu) || ui->IsMenuOpen(dialogueTopicMenu) || ui->IsMenuOpen(bestiary) ||
               ui->IsMenuOpen(ostim);
    }

    ProcessButtonEventsResult ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap, bool& wantCapture,
                                                  KeyStateStore& keys) {
        ProcessButtonEventsResult result{};

        auto* player = RE::PlayerCharacter::GetSingleton();
        for (auto* e = *a_evns; e; e = e->next) {
            const auto* btn = e->AsButtonEvent();
            if (!btn || (!btn->IsDown() && !btn->IsUp())) continue;

            const auto dev = btn->GetDevice();
            auto code = static_cast<int>(btn->GetIDCode());
            if (dev == RE::INPUT_DEVICE::kGamepad) code = GamepadIdToIndex(code);

            if (dev == RE::INPUT_DEVICE::kMouse) {
                const int mouseCode = kMouseButtonBase + code;
                if (mouseCode >= 0 && mouseCode < kMaxCode) {
                    (void)TryHandleCapture(btn, cap, wantCapture, RE::INPUT_DEVICE::kMouse, mouseCode);
                    keys.kbDown[static_cast<std::size_t>(mouseCode)].store(btn->IsDown(), std::memory_order_relaxed);
                }
                continue;
            }

            if (code < 0 || code >= kMaxCode) continue;
            (void)TryHandleCapture(btn, cap, wantCapture, dev, code);
            UpdateDownState(dev, code, btn->IsDown(), keys);

            if (btn->IsDown() && player && btn->QUserEvent() == "Shout"sv) {
                if (IsTransformPowerEquipped(player)) {
                    MAGIC_DEBUG_LOG("[Input] Shout pressed with transform -> ForceExitNoRestore");
                    result.forceExit = true;
                }
            }
        }

        return result;
    }

    void FilterMouseForPopup(RE::InputEvent** a_evns) {
        if (!Input::detail::g_popupOpenForInput.load(std::memory_order_relaxed)) return;

        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;
        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (cur->eventType == RE::INPUT_EVENT_TYPE::kMouseMove) {
                auto const* mm = static_cast<RE::MouseMoveEvent*>(cur);
                Input::detail::PushPopupInput({Input::detail::PopupInputKind::MouseDelta,
                                               static_cast<float>(mm->mouseInputX),
                                               static_cast<float>(mm->mouseInputY)});
                remove = true;
            } else if (cur->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
                auto const* ts = static_cast<RE::ThumbstickEvent*>(cur);
                if (ts->IsLeft()) {
                    constexpr float kDeadzone = 0.15f;
                    constexpr float kSensitivity = 12.f;
                    const float ax = (std::abs(ts->xValue) > kDeadzone) ? ts->xValue : 0.f;
                    const float ay = (std::abs(ts->yValue) > kDeadzone) ? ts->yValue : 0.f;
                    if (ax != 0.f || ay != 0.f) {
                        Input::detail::PushPopupInput(
                            {Input::detail::PopupInputKind::StickDelta, ax * kSensitivity, -ay * kSensitivity});
                    }
                }
                remove = true;
            } else if (const auto* btn = cur->AsButtonEvent()) {
                using Key = RE::BSWin32GamepadDevice::Key;
                const auto btnDev = btn->GetDevice();
                const auto btnID = btn->GetIDCode();
                if (btnDev == RE::INPUT_DEVICE::kMouse && btnID == 0) {
                    if (btn->IsDown()) Input::detail::PushPopupInput({Input::detail::PopupInputKind::Click, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kMouse && btnID == 1) {
                    if (btn->IsDown())
                        Input::detail::PushPopupInput({Input::detail::PopupInputKind::RightClick, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kX)) {
                    if (btn->IsDown()) Input::detail::PushPopupInput({Input::detail::PopupInputKind::Click, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kB)) {
                    if (btn->IsDown())
                        Input::detail::PushPopupInput({Input::detail::PopupInputKind::RightClick, 0.f, 0.f});
                    remove = true;
                } else if (btnDev == RE::INPUT_DEVICE::kGamepad && btnID == static_cast<std::uint32_t>(Key::kY)) {
                    if (btn->IsDown()) Input::detail::PushPopupInput({Input::detail::PopupInputKind::Close, 0.f, 0.f});
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

    void FilterEvents(RE::InputEvent** a_evns, const KeyStateStore& keys, const SlotEdgeStore& slots,
                      const HotkeyCacheStore& cache, ExclusiveStore& excl, ReplayArr& replay, RetainedArr& retained) {
        RE::InputEvent* prev = nullptr;
        RE::InputEvent* cur = *a_evns;
        while (cur) {
            RE::InputEvent* next = cur->next;
            bool remove = false;

            if (const auto* btn = cur->AsButtonEvent()) {
                const auto dev = btn->GetDevice();
                auto code = static_cast<int>(btn->GetIDCode());
                const auto rawCode = btn->GetIDCode();
                if (dev == RE::INPUT_DEVICE::kGamepad) code = GamepadIdToIndex(code);

                if (code >= 0 && code < kMaxCode) {
                    remove = ShouldFilterAndSave(dev, code, rawCode, btn->QUserEvent(), btn->Value(),
                                                 btn->HeldDuration(), keys, slots, cache, excl, replay, retained) ||
                             ShouldFilterHudToggle(dev, code, cache);
#ifdef DEBUG
                    if (!remove && (dev == RE::INPUT_DEVICE::kMouse || dev == RE::INPUT_DEVICE::kKeyboard)) {
                        const int effCode = (dev == RE::INPUT_DEVICE::kMouse) ? kMouseButtonBase + code : code;
                        const int n = slots.ActiveSlots();
                        for (int slot = 0; slot < n; ++slot) {
                            if (ComboContains(cache.slots[static_cast<std::size_t>(slot)].kb, effCode)) {
                                MAGIC_DEBUG_LOG("[Input] FilterEvents: slot={} code={} PASSING", slot, effCode);
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

    void UpdateSlotsIfAllowed(bool blocked, float dt, SlotEdgeStore& slots, ExclusiveStore& excl,
                              const HotkeyCacheStore& cache, const KeyStateStore& keys, RetainedArr& retained,
                              DeferredVec& deferred, ReplayArr& replay, bool spellSystemActive, int activeSlot) {
        const auto& patches = IntegratedMagic::Config::MagicConfigAdapter::Get();
        if (!blocked)
            RecomputeSlotEdges(dt, slots, excl, cache, keys, patches, replay, retained, deferred, spellSystemActive,
                               activeSlot);
        else
            DrainWhenBlocked(slots, slots.pressedMask, slots.releasedMask);
    }

}