#include "MagicInput.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>

#include "MagicConfig.h"
#include "PCH.h"

#ifdef _WIN32
    #include <Windows.h>
#endif

namespace {
    constexpr int kSlots = 4;
    constexpr int kMaxCode = 355;

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

    std::array<SlotHotkeys, kSlots> g_cache{};            // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_kbDown{};    // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};    // NOSONAR
    std::array<std::atomic_bool, kSlots> g_slotDown{};    // NOSONAR
    std::atomic<std::byte> g_pressedMask{std::byte{0}};   // NOSONAR
    std::atomic<std::byte> g_releasedMask{std::byte{0}};  // NOSONAR
    std::array<bool, kSlots> g_prevRawKbDown{};           // NOSONAR
    std::array<bool, kSlots> g_prevRawGpDown{};           // NOSONAR

    bool AnyEnabled(const std::array<int, 3>& a) { return (a[0] != -1) || (a[1] != -1) || (a[2] != -1); }

    template <class DownArr>
    bool ComboDown(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) {
            return false;
        }

        const bool ok = std::ranges::all_of(combo, [&](int code) {
            if (code == -1) {
                return true;
            }

            if (code < 0 || code >= kMaxCode) {
                return false;
            }

            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });

        return ok;
    }

    inline bool ComboContains(const std::array<int, 3>& combo, int code) {
        return std::ranges::find(combo, code) != combo.end();
    }

    template <class DownArr>
    bool ComboExclusiveNow(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) {
            return false;
        }

        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) {
                continue;
            }
            if (ComboContains(combo, code)) {
                continue;
            }
            return false;
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

    inline void AtomicFetchOrByte(std::atomic<std::byte>& a, std::byte bits,
                                  std::memory_order order = std::memory_order_relaxed) {
        std::byte cur = a.load(order);
        while (!a.compare_exchange_weak(cur, (cur | bits), order, order)) {
            // cur é atualizado com o valor atual automaticamente quando falha
        }
    }

    void HandleSlotPressed(int slot) {
        if (auto const& st = IntegratedMagic::MagicState::Get(); st.IsHoldActive() || st.IsAutomaticActive()) {
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

        if (ss.mode == IntegratedMagic::ActivationMode::Automatic) {
            IntegratedMagic::MagicState::Get().ToggleAutomatic(slot);
            return;
        }
    }

    void HandleSlotReleased(int slot) {
        if (slot < 0 || slot >= kSlots) {
            return;
        }

        const std::uint32_t spellFormID = IntegratedMagic::MagicSlots::GetSlotSpell(slot);
        if (spellFormID == 0) {
            return;
        }

        if (const auto ss = IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(spellFormID);
            ss.mode != IntegratedMagic::ActivationMode::Hold) {
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

    bool ComputeAcceptedExclusive(int slot, const SlotHotkeys& hk, bool prevAccepted, bool kbNow, bool gpNow,
                                  bool rawNow) {
        const auto s = static_cast<std::size_t>(slot);

        const bool kbPrev = g_prevRawKbDown[s];
        const bool gpPrev = g_prevRawGpDown[s];

        const bool kbPressedEdge = kbNow && !kbPrev;
        const bool gpPressedEdge = gpNow && !gpPrev;

        g_prevRawKbDown[s] = kbNow;
        g_prevRawGpDown[s] = gpNow;

        if (!prevAccepted) {
            if (kbPressedEdge && ComboExclusiveNow(hk.kb, g_kbDown)) {
                return true;
            }
            if (gpPressedEdge && ComboExclusiveNow(hk.gp, g_gpDown)) {
                return true;
            }
            return false;
        }

        return rawNow;
    }

    void RecomputeSlotEdges() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();

        for (int slot = 0; slot < kSlots; ++slot) {
            const auto& hk = g_cache[static_cast<std::size_t>(slot)];

            const bool kbNow = ComboDown(hk.kb, g_kbDown);
            const bool gpNow = ComboDown(hk.gp, g_gpDown);
            const bool rawNow = kbNow || gpNow;

            const auto s = static_cast<std::size_t>(slot);
            const bool prevAccepted = g_slotDown[s].load(std::memory_order_relaxed);

            bool acceptedNow = false;

            if (cfg.requireExclusiveHotkeyPatch) {
                acceptedNow = ComputeAcceptedExclusive(slot, hk, prevAccepted, kbNow, gpNow, rawNow);
            } else {
                g_prevRawKbDown[s] = kbNow;
                g_prevRawGpDown[s] = gpNow;
                acceptedNow = rawNow;
            }

            if (acceptedNow == prevAccepted) {
                continue;
            }

            g_slotDown[s].store(acceptedNow, std::memory_order_relaxed);

            const auto bit = std::byte{static_cast<unsigned char>(1u << slot)};
            AtomicFetchOrByte(acceptedNow ? g_pressedMask : g_releasedMask, bit);
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

    std::optional<int> ConsumeBit(std::atomic<std::byte>& maskAtomic) {
        while (true) {
            std::byte mask = maskAtomic.load(std::memory_order_relaxed);
            if (mask == std::byte{0}) {
                return std::nullopt;
            }

            int idx = -1;
            for (int i = 0; i < kSlots; ++i) {
                const auto bit = std::byte{static_cast<unsigned char>(1u << i)};
                if (std::to_integer<unsigned>(mask & bit) != 0u) {
                    idx = i;
                    break;
                }
            }

            if (idx < 0) {
                return std::nullopt;
            }

            const auto bit = std::byte{static_cast<unsigned char>(1u << idx)};
            const std::byte desired = (mask & ~bit);
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
            bool wantCapture = cap.captureRequested.load(std::memory_order_relaxed);

            for (auto e = *a_events; e; e = e->next) {
                const auto* btn = e->AsButtonEvent();
                if (!btn || (!btn->IsDown() && !btn->IsUp())) {
                    continue;
                }

                const auto dev = btn->GetDevice();
                const auto code = static_cast<int>(btn->idCode);
                if (code < 0 || code >= kMaxCode) {
                    continue;
                }

                (void)TryHandleCapture(btn, cap, wantCapture);

                UpdateDownState(dev, code, btn->IsDown());
            }

            const float dt = CalculateDeltaTime();

            RecomputeSlotEdges();

            const bool blocked = IsInputBlockedByMenus();
            ConsumeAndHandleSlots(blocked);

            if (!blocked) {
                IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);
                IntegratedMagic::MagicState::Get().PumpAutomatic();
            }

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        bool TryHandleCapture(const RE::ButtonEvent* btn, CaptureState& cap, bool& wantCapture) const {
            if (!wantCapture || !btn->IsDown()) {
                return false;
            }

            const auto dev = btn->GetDevice();
            const auto code = static_cast<int>(btn->idCode);

            if (dev == RE::INPUT_DEVICE::kKeyboard && code == 0x01) {
                return false;
            }

            int encoded = -1;
            if (dev == RE::INPUT_DEVICE::kKeyboard) {
                encoded = code;
            } else if (dev == RE::INPUT_DEVICE::kGamepad) {
                encoded = -(code + 1);
            }

            if (encoded == -1) {
                return false;
            }

            cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
            cap.captureRequested.store(false, std::memory_order_relaxed);

            wantCapture = false;
            return true;
        }

        void UpdateDownState(RE::INPUT_DEVICE dev, int code, bool downNow) const {
            if (dev == RE::INPUT_DEVICE::kKeyboard) {
                g_kbDown[static_cast<std::size_t>(code)].store(downNow, std::memory_order_relaxed);
            } else if (dev == RE::INPUT_DEVICE::kGamepad) {
                g_gpDown[static_cast<std::size_t>(code)].store(downNow, std::memory_order_relaxed);
            }
        }

        void DrainWhenBlocked() const {
            for (auto slot = MagicInput::ConsumePressedSlot(); slot.has_value();
                 slot = MagicInput::ConsumePressedSlot()) {
                // Just use to clean the cache
            }

            for (auto slot = MagicInput::ConsumeReleasedSlot(); slot.has_value();
                 slot = MagicInput::ConsumeReleasedSlot()) {
                HandleSlotReleased(*slot);
            }
        }

        void ConsumeAndHandleSlots(bool blocked) const {
            if (blocked) {
                DrainWhenBlocked();
                return;
            }

            for (auto slot = MagicInput::ConsumePressedSlot(); slot.has_value();
                 slot = MagicInput::ConsumePressedSlot()) {
                HandleSlotPressed(*slot);
            }

            for (auto slot = MagicInput::ConsumeReleasedSlot(); slot.has_value();
                 slot = MagicInput::ConsumeReleasedSlot()) {
                HandleSlotReleased(*slot);
            }
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
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || actor != player) return;

    std::string_view tag{ev->tag.c_str(), ev->tag.size()};
    if (tag == "EnableBumper"sv) {
        IntegratedMagic::MagicState::Get().NotifyAttackEnabled();
    }
    if (tag == "CastStop"sv) {
        IntegratedMagic::MagicState::Get().AutoExit();
    }
}