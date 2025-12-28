#include "MagicInput.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

#include "MagicConfig.h"
#include "PCH.h"

#ifdef _WIN32
    #include <Windows.h>
#endif

namespace {
    constexpr int kSlots = 4;
    constexpr int kMaxCode = 256;

    struct SlotHotkeys {
        std::array<int, 3> kb{-1, -1, -1};
        std::array<int, 3> gp{-1, -1, -1};
    };

    struct CaptureState {
        std::atomic_bool captureRequested{false};
        std::atomic_int capturedEncoded{-1};
    };

    CaptureState& GetCaptureState() {
        static CaptureState st{};  // NOSONAR
        return st;
    }

    std::array<SlotHotkeys, kSlots> g_cache{};

    std::array<std::atomic_bool, kMaxCode> g_kbDown{};
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};

    std::array<std::atomic_bool, kSlots> g_slotDown{};

    std::atomic_uint8_t g_pressedMask{0};
    std::atomic_uint8_t g_releasedMask{0};

    bool AnyEnabled(const std::array<int, 3>& a) { return (a[0] != -1) || (a[1] != -1) || (a[2] != -1); }

    template <class DownArr>
    bool ComboDown(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) {
            return false;
        }

        for (int code : combo) {
            if (code == -1) {
                continue;
            }
            if (code < 0 || code >= kMaxCode) {
                return false;
            }
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) {
                return false;
            }
        }
        return true;
    }

    inline bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        if (ui->GameIsPaused()) {
            return true;
        }

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

        if (static const RE::BSFixedString mcm{"Mod Configuration Menu"};
            ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
            ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
            ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
            ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
            ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
            ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm)) {
            return true;
        }

        return false;
    }

    void HandleSlotPressed(int slot) {
        if (auto const& st = IntegratedMagic::MagicState::Get(); st.IsHoldActive()) {
            return;
        }

        if (slot < 0 || slot >= kSlots) {
            return;
        }

        if (auto const* player = RE::PlayerCharacter::GetSingleton(); !player) {
            return;
        }

        const std::uint32_t spellFormID = IntegratedMagic::MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) {
            return;
        }

        const auto ss = IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(spellFormID);

        if (ss.mode == IntegratedMagic::ActivationMode::Press) {
            IntegratedMagic::MagicState::Get().TogglePress(slot);
            return;
        }

        if (ss.mode == IntegratedMagic::ActivationMode::Hold) {
            IntegratedMagic::MagicState::Get().HoldDown(slot);
            return;
        }

        IntegratedMagic::MagicState::Get().TogglePress(slot);
    }

    void HandleSlotReleased(int slot) {
        if (slot < 0 || slot >= kSlots) {
            return;
        }

        const std::uint32_t spellFormID = IntegratedMagic::MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) {
            return;
        }

        const auto ss = IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(spellFormID);

        if (ss.mode != IntegratedMagic::ActivationMode::Hold) {
            return;
        }

        IntegratedMagic::MagicState::Get().HoldUp(slot);
    }

    bool SlotComboDown(int slot) {
        if (slot < 0 || slot >= kSlots) {
            return false;
        }

        const auto& hk = g_cache[static_cast<std::size_t>(slot)];
        const bool kb = ComboDown(hk.kb, g_kbDown);
        const bool gp = ComboDown(hk.gp, g_gpDown);
        return kb || gp;
    }

    void RecomputeSlotEdges() {
        for (int slot = 0; slot < kSlots; ++slot) {
            const bool now = SlotComboDown(slot);
            const bool prev = g_slotDown[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);

            if (now != prev) {
                g_slotDown[static_cast<std::size_t>(slot)].store(now, std::memory_order_relaxed);

                const auto bit = static_cast<uint8_t>(1u << slot);
                if (now) {
                    g_pressedMask.fetch_or(bit, std::memory_order_relaxed);
                } else {
                    g_releasedMask.fetch_or(bit, std::memory_order_relaxed);
                }
            }
        }
    }
    float CalculateDeltaTime() {
        using clock = std::chrono::steady_clock;
        static clock::time_point last = clock::now();

        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        if (dt < 0.0f || dt > 0.25f) {
            dt = 0.0f;
        }
        return dt;
    }

    std::optional<int> ConsumeBit(std::atomic_uint8_t& maskAtomic) {
        while (true) {
            uint8_t mask = maskAtomic.load(std::memory_order_relaxed);
            if (mask == 0) {
                return std::nullopt;
            }

            int idx = -1;
            for (int i = 0; i < kSlots; ++i) {
                if (mask & static_cast<uint8_t>(1u << i)) {
                    idx = i;
                    break;
                }
            }

            if (idx < 0) {
                return std::nullopt;
            }

            const auto desired = static_cast<uint8_t>(mask & ~static_cast<uint8_t>(1u << idx));
            if (maskAtomic.compare_exchange_weak(mask, desired, std::memory_order_relaxed)) {
                return idx;
            }
        }
    }

    void LoadHotkeyCache_FromConfig() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();

        for (auto& s : g_cache) {
            s.kb = {-1, -1, -1};
            s.gp = {-1, -1, -1};
        }

        auto fillFromInputConfig = [](SlotHotkeys& out, const auto& in) {
            out.kb[0] = in.KeyboardScanCode1.load(std::memory_order_relaxed);
            out.kb[1] = in.KeyboardScanCode2.load(std::memory_order_relaxed);
            out.kb[2] = in.KeyboardScanCode3.load(std::memory_order_relaxed);

            out.gp[0] = in.GamepadButton1.load(std::memory_order_relaxed);
            out.gp[1] = in.GamepadButton2.load(std::memory_order_relaxed);
            out.gp[2] = in.GamepadButton3.load(std::memory_order_relaxed);
        };

        fillFromInputConfig(g_cache[0], cfg.Magic1Input);
        fillFromInputConfig(g_cache[1], cfg.Magic2Input);
        fillFromInputConfig(g_cache[2], cfg.Magic3Input);
        fillFromInputConfig(g_cache[3], cfg.Magic4Input);
    }

    class IntegratedMagicInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static IntegratedMagicInputHandler* GetSingleton() {
            static IntegratedMagicInputHandler instance;
            return std::addressof(instance);
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
                                              RE::BSTEventSource<RE::InputEvent*>*) override {
            if (!a_events) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto& cap = GetCaptureState();
            const bool wantCapture = cap.captureRequested.load(std::memory_order_relaxed);

            for (auto e = *a_events; e; e = e->next) {
                const auto* btn = e->AsButtonEvent();
                if (!btn) {
                    continue;
                }

                if (!btn->IsDown() && !btn->IsUp()) {
                    continue;
                }

                const auto dev = btn->GetDevice();
                const auto code = static_cast<int>(btn->idCode);
                if (code < 0 || code >= kMaxCode) {
                    continue;
                }

                if (wantCapture && btn->IsDown()) {
                    if (!(dev == RE::INPUT_DEVICE::kKeyboard && code == 0x01)) {
                        int encoded = -1;
                        if (dev == RE::INPUT_DEVICE::kKeyboard) {
                            encoded = code;
                        } else if (dev == RE::INPUT_DEVICE::kGamepad) {
                            encoded = -(code + 1);
                        }

                        if (encoded != -1) {
                            cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
                            cap.captureRequested.store(false, std::memory_order_relaxed);
                        }
                    }
                }

                const bool downNow = btn->IsDown();
                if (dev == RE::INPUT_DEVICE::kKeyboard) {
                    g_kbDown[static_cast<std::size_t>(code)].store(downNow, std::memory_order_relaxed);
                } else if (dev == RE::INPUT_DEVICE::kGamepad) {
                    g_gpDown[static_cast<std::size_t>(code)].store(downNow, std::memory_order_relaxed);
                }
            }

            const float dt = CalculateDeltaTime();

            RecomputeSlotEdges();

            if (IsInputBlockedByMenus()) {
                while (MagicInput::ConsumePressedSlot()) {
                }
                while (auto slot = MagicInput::ConsumeReleasedSlot()) {
                    HandleSlotReleased(*slot);
                }

                return RE::BSEventNotifyControl::kContinue;
            }

            while (auto slot = MagicInput::ConsumePressedSlot()) {
                HandleSlotPressed(*slot);
            }

            while (auto slot = MagicInput::ConsumeReleasedSlot()) {
                HandleSlotReleased(*slot);
            }

            IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);

            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void MagicInput::RegisterInputHandler() {
    LoadHotkeyCache_FromConfig();

    if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
        mgr->AddEventSink(IntegratedMagicInputHandler::GetSingleton());
    }
}

void MagicInput::OnConfigChanged() {
    LoadHotkeyCache_FromConfig();
    RecomputeSlotEdges();
}

std::optional<int> MagicInput::GetDownSlotForSelection() {
    for (int slot = 0; slot < kSlots; ++slot) {
        const bool down = MagicInput::IsSlotHotkeyDown(slot);
        if (down) {
            return slot;
        }
    }
    return std::nullopt;
}

bool MagicInput::IsSlotHotkeyDown(int slot) { return SlotComboDown(slot); }

void MagicInput::RequestHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(true, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

int MagicInput::PollCapturedHotkey() {
    auto& cap = GetCaptureState();

    if (int v = cap.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        cap.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }

    return -1;
}

std::optional<int> MagicInput::ConsumePressedSlot() { return ConsumeBit(g_pressedMask); }

std::optional<int> MagicInput::ConsumeReleasedSlot() { return ConsumeBit(g_releasedMask); }

void MagicInput::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!ev || !ev->holder) return;

    auto* actor = ev->holder->As<RE::Actor>();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!actor || actor != player) return;

    std::string_view tag{ev->tag.c_str(), ev->tag.size()};
    if (tag == "EnableBumper"sv) {
        IntegratedMagic::MagicState::Get().NotifyAttackEnabled();
    }
}