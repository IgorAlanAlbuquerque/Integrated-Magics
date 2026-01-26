#include "MagicInput.h"

#include <algorithm>
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
    constexpr int kMaxSlots = static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots);
    static_assert(kMaxSlots <= 64, "MagicInput mask uses uint64_t, keep max slots <= 64.");

    std::atomic<int> g_slotCount{4};  // NOSONAR
    inline int ActiveSlots() {
        int n = g_slotCount.load(std::memory_order_relaxed);
        if (n < 1) n = 1;
        if (n > kMaxSlots) n = kMaxSlots;
        return n;
    }

    constexpr int kMaxCode = 400;
    constexpr float kExclusiveConfirmDelaySec = 0.10f;
    constexpr int kDIK_W = 0x11;
    constexpr int kDIK_A = 0x1E;
    constexpr int kDIK_S = 0x1F;
    constexpr int kDIK_D = 0x20;

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };

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

    std::array<std::atomic_bool, kMaxCode> g_kbDown{};          // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};          // NOSONAR
    std::array<SlotHotkeys, kMaxSlots> g_cache{};               // NOSONAR
    std::array<std::atomic_bool, kMaxSlots> g_slotDown{};       // NOSONAR
    std::atomic<std::uint64_t> g_pressedMask{0ull};             // NOSONAR
    std::atomic<std::uint64_t> g_releasedMask{0ull};            // NOSONAR
    std::array<bool, kMaxSlots> g_prevRawKbDown{};              // NOSONAR
    std::array<bool, kMaxSlots> g_prevRawGpDown{};              // NOSONAR
    std::array<float, kMaxSlots> g_exclusivePendingTimer{};     // NOSONAR
    std::array<PendingSrc, kMaxSlots> g_exclusivePendingSrc{};  // NOSONAR

    inline void ClearExclusivePending(std::size_t s) {
        g_exclusivePendingSrc[s] = PendingSrc::None;
        g_exclusivePendingTimer[s] = 0.0f;
    }

    inline bool IsAllowedExtra_Keyboard_MoveOrCamera(int code) {
        switch (code) {
            case kDIK_W:
            case kDIK_A:
            case kDIK_S:
            case kDIK_D:
                return true;
            default:
                return false;
        }
    }

    inline bool IsAllowedExtra_Gamepad_MoveOrCamera(int) { return false; }

    inline bool HasExclusivePending(std::size_t s) { return g_exclusivePendingSrc[s] != PendingSrc::None; }

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

    template <class DownArr, class AllowedFn>
    bool ComboExclusiveNow(const std::array<int, 3>& combo, const DownArr& down, AllowedFn isAllowedExtra) {
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
            if (isAllowedExtra(code)) {
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

    inline void AtomicFetchOrU64(std::atomic<std::uint64_t>& a, std::uint64_t bits,
                                 std::memory_order order = std::memory_order_relaxed) {
        std::uint64_t cur = a.load(order);
        while (!a.compare_exchange_weak(cur, (cur | bits), order, order)) {
            // cur é atualizado automaticamente
        }
    }

    void ClearEdgeStateOnly() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_slotDown[s].store(false, std::memory_order_relaxed);
            g_prevRawKbDown[s] = false;
            g_prevRawGpDown[s] = false;
            ClearExclusivePending(s);
        }
        g_pressedMask.store(0uLL, std::memory_order_relaxed);
        g_releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    template <class DownArr, class KeepArr, class CodesArr>
    inline void MarkKeepIfDown(const CodesArr& codes, const DownArr& down, KeepArr& keep) {
        for (int code : codes) {
            if (code < 0 || code >= kMaxCode) {
                continue;
            }
            const auto idx = static_cast<std::size_t>(code);
            if (down[idx].load(std::memory_order_relaxed)) {
                keep[idx] = true;
            }
        }
    }

    void ClearLikelyStuckKeysAfterMenuClose() {
        std::array<bool, kMaxCode> keepKb{};
        std::array<bool, kMaxCode> keepGp{};

        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = g_cache[static_cast<std::size_t>(slot)];

            MarkKeepIfDown(hk.kb, g_kbDown, keepKb);
            MarkKeepIfDown(hk.gp, g_gpDown, keepGp);
        }

        for (int i = 0; i < kMaxCode; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            if (!keepKb[idx]) {
                g_kbDown[idx].store(false, std::memory_order_relaxed);
            }
            if (!keepGp[idx]) {
                g_gpDown[idx].store(false, std::memory_order_relaxed);
            }
        }

        constexpr int kDIK_Escape = 0x01;
        g_kbDown[static_cast<std::size_t>(kDIK_Escape)].store(false, std::memory_order_relaxed);

        ClearEdgeStateOnly();
    }

    void ResetExclusiveState() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_prevRawKbDown[s] = false;
            g_prevRawGpDown[s] = false;
            ClearExclusivePending(s);
            g_slotDown[s].store(false, std::memory_order_relaxed);
        }

        g_pressedMask.store(0uLL, std::memory_order_relaxed);
        g_releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void HandleSlotPressed(int slot) {
        if (const int n = ActiveSlots(); slot < 0 || slot >= n) {
            return;
        }
        if (auto const* player = RE::PlayerCharacter::GetSingleton(); !player) {
            return;
        }

        IntegratedMagic::MagicState::Get().OnSlotPressed(slot);
    }

    void HandleSlotReleased(int slot) {
        if (const int n = ActiveSlots(); slot < 0 || slot >= n) {
            return;
        }
        IntegratedMagic::MagicState::Get().OnSlotReleased(slot);
    }

    bool SlotComboDown(int slot) {
        if (const int n = ActiveSlots(); slot < 0 || slot >= n) {
            return false;
        }

        const auto& hk = g_cache[static_cast<std::size_t>(slot)];
        const bool kb = ComboDown(hk.kb, g_kbDown);
        const bool gp = ComboDown(hk.gp, g_gpDown);
        return kb || gp;
    }

    bool ComputeAcceptedExclusive(int slot, const SlotHotkeys& hk, bool prevAccepted, bool kbNow, bool gpNow,
                                  bool rawNow, float dt) {
        const auto s = static_cast<std::size_t>(slot);

        const bool kbPrev = g_prevRawKbDown[s];
        const bool gpPrev = g_prevRawGpDown[s];

        const bool kbPressedEdge = kbNow && !kbPrev;
        const bool gpPressedEdge = gpNow && !gpPrev;

        g_prevRawKbDown[s] = kbNow;
        g_prevRawGpDown[s] = gpNow;

        if (prevAccepted) {
            ClearExclusivePending(s);
            return rawNow;
        }

        if (HasExclusivePending(s)) {
            const auto src = g_exclusivePendingSrc[s];

            const bool stillDown = (src == PendingSrc::Kb) ? kbNow : gpNow;

            if (const bool stillExclusive =
                    (src == PendingSrc::Kb) ? ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                                            : ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                !stillExclusive) {
                ClearExclusivePending(s);
                return false;
            }

            if (!stillDown) {
                ClearExclusivePending(s);
                return true;
            }

            g_exclusivePendingTimer[s] -= dt;
            if (g_exclusivePendingTimer[s] <= 0.0f) {
                ClearExclusivePending(s);
                return true;
            }

            return false;
        }

        if (kbPressedEdge && ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)) {
            g_exclusivePendingSrc[s] = PendingSrc::Kb;
            g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            return false;
        }

        if (gpPressedEdge && ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera)) {
            g_exclusivePendingSrc[s] = PendingSrc::Gp;
            g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            return false;
        }

        return false;
    }

    void RecomputeSlotEdges(float dt) {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const int n = ActiveSlots();

        for (int slot = 0; slot < n; ++slot) {
            const auto& hk = g_cache[static_cast<std::size_t>(slot)];

            const bool kbNow = ComboDown(hk.kb, g_kbDown);
            const bool gpNow = ComboDown(hk.gp, g_gpDown);
            const bool rawNow = kbNow || gpNow;

            const auto s = static_cast<std::size_t>(slot);
            const bool prevAccepted = g_slotDown[s].load(std::memory_order_relaxed);

            bool acceptedNow = false;

            if (cfg.requireExclusiveHotkeyPatch) {
                acceptedNow = ComputeAcceptedExclusive(slot, hk, prevAccepted, kbNow, gpNow, rawNow, dt);
            } else {
                g_prevRawKbDown[s] = kbNow;
                g_prevRawGpDown[s] = gpNow;
                ClearExclusivePending(s);
                acceptedNow = rawNow;
            }

            if (acceptedNow == prevAccepted) {
                continue;
            }

            g_slotDown[s].store(acceptedNow, std::memory_order_relaxed);

            const std::uint64_t bit = (1uLL << slot);
            AtomicFetchOrU64(acceptedNow ? g_pressedMask : g_releasedMask, bit);
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

    std::optional<int> ConsumeBit(std::atomic<std::uint64_t>& maskAtomic) {
        while (true) {
            const int n = ActiveSlots();
            const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);

            std::uint64_t curAll = maskAtomic.load(std::memory_order_relaxed);
            std::uint64_t cur = (curAll & allowed);

            if (cur == 0uLL) {
                if (curAll != 0uLL) {
                    (void)maskAtomic.compare_exchange_weak(curAll, (curAll & allowed), std::memory_order_relaxed);
                    continue;
                }
                return std::nullopt;
            }

            int idx = -1;
            for (int i = 0; i < n; ++i) {
                const std::uint64_t bit = (1uLL << i);
                if (cur & bit) {
                    idx = i;
                    break;
                }
            }

            if (idx < 0) {
                return std::nullopt;
            }

            const std::uint64_t bit = (1uLL << idx);
            const std::uint64_t desired = (curAll & ~bit);
            if (maskAtomic.compare_exchange_weak(curAll, desired, std::memory_order_relaxed)) {
                return idx;
            }
        }
    }

    void LoadHotkeyCache_FromConfig() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const auto n = static_cast<int>(cfg.SlotCount());
        g_slotCount.store(n, std::memory_order_relaxed);

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
        const int slots = ActiveSlots();
        const int m = std::min(n, slots);
        for (int i = 0; i < m; ++i) {
            fillFromInputConfig(g_cache[static_cast<std::size_t>(i)], cfg.slotInput[static_cast<std::size_t>(i)]);
        }
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

            const bool blocked = IsInputBlockedByMenus();

            if (_prevBlocked && !blocked) {
                ClearLikelyStuckKeysAfterMenuClose();
            }
            _prevBlocked = blocked;

            if (blocked) {
                DrainWhenBlocked();
                return RE::BSEventNotifyControl::kContinue;
            }

            RecomputeSlotEdges(dt);
            ConsumeAndHandleSlots(blocked);

            if (!blocked) {
                IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);
                IntegratedMagic::MagicState::Get().PumpAutomatic();
            }

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        bool _prevBlocked{false};

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
    ResetExclusiveState();
}

std::optional<int> MagicInput::GetDownSlotForSelection() {
    const int n = ActiveSlots();
    for (int slot = 0; slot < n; ++slot) {
        if (MagicInput::IsSlotHotkeyDown(slot)) {
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
        IntegratedMagic::MagicState::Get().OnCastStop();
    }
}