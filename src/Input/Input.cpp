#include "Input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <vector>

#include "Config/Config.h"
#include "PCH.h"
#include "State/State.h"
#include "UI/HUD.h"

namespace {

    constexpr int kMaxSlots = static_cast<int>(IntegratedMagic::MagicConfig::kMaxSlots);
    constexpr int kMaxCode = 400;
    constexpr float kExclusiveConfirmDelaySec = 0.10f;
    constexpr int kDIK_W = 0x11;
    constexpr int kDIK_A = 0x1E;
    constexpr int kDIK_S = 0x1F;
    constexpr int kDIK_D = 0x20;
    constexpr int kDIK_Escape = 0x01;
    static_assert(kMaxSlots <= 64, "Input mask uses uint64_t, keep max slots <= 64.");

    std::atomic<int> g_slotCount{4};                         // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_kbDown{};       // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};       // NOSONAR
    std::array<std::atomic_bool, kMaxSlots> g_slotDown{};    // NOSONAR
    std::array<bool, kMaxSlots> g_slotWasAccepted{};         // NOSONAR — filtra releases após combo aceito
    std::atomic<std::uint64_t> g_pressedMask{0ull};          // NOSONAR
    std::atomic<std::uint64_t> g_releasedMask{0ull};         // NOSONAR
    std::array<bool, kMaxSlots> g_prevRawKbDown{};           // NOSONAR
    std::array<bool, kMaxSlots> g_slotIsMultiKey{};          // NOSONAR — combo com 2+ teclas
    std::array<bool, kMaxSlots> g_slotFullComboSeen{};       // NOSONAR — combo completo visto no pending atual
    std::array<bool, kMaxSlots> g_prevRawGpDown{};           // NOSONAR
    std::array<float, kMaxSlots> g_exclusivePendingTimer{};  // NOSONAR

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };
    std::array<PendingSrc, kMaxSlots> g_exclusivePendingSrc{};  // NOSONAR

    struct SlotHotkeys {
        std::array<int, 3> kb{-1, -1, -1};
        std::array<int, 3> gp{-1, -1, -1};
    };
    std::array<SlotHotkeys, kMaxSlots> g_cache{};  // NOSONAR
    SlotHotkeys g_hudCache{};                      // NOSONAR — hotkeys do popup HUD
    std::atomic_bool g_hudTogglePending{false};    // NOSONAR — edge detectado, aguardando consumo

    struct RetainedEvent {
        RE::INPUT_DEVICE dev;
        std::uint32_t rawIdCode;
        RE::BSFixedString userEvent;
        float value;
        float heldSecs;
    };
    std::array<std::vector<RetainedEvent>, kMaxSlots> g_retainedEvents{};  // NOSONAR

    struct CaptureState {
        std::atomic_bool captureRequested{false};
        std::atomic_int capturedEncoded{-1};
    };
    CaptureState& GetCaptureState() {
        static CaptureState st{};  // NOSONAR
        return st;
    }

    inline int ActiveSlots() {
        int n = g_slotCount.load(std::memory_order_relaxed);
        if (n < 1) n = 1;
        if (n > kMaxSlots) n = kMaxSlots;
        return n;
    }

    inline bool HasExclusivePending(std::size_t s) { return g_exclusivePendingSrc[s] != PendingSrc::None; }

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

    bool AnyEnabled(const std::array<int, 3>& a) { return (a[0] != -1) || (a[1] != -1) || (a[2] != -1); }

    template <class DownArr>
    bool ComboDown(const std::array<int, 3>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) return false;
        return std::ranges::all_of(combo, [&](int code) {
            if (code == -1) return true;
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    inline bool ComboContains(const std::array<int, 3>& combo, int code) {
        return std::ranges::find(combo, code) != combo.end();
    }

    template <class DownArr, class AllowedFn>
    bool ComboExclusiveNow(const std::array<int, 3>& combo, const DownArr& down, AllowedFn isAllowedExtra) {
        if (!AnyEnabled(combo)) return false;
        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) continue;
            if (ComboContains(combo, code)) continue;
            if (isAllowedExtra(code)) continue;
            return false;
        }
        return true;
    }

    inline bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        if (ui->GameIsPaused()) return true;
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

    inline void AtomicFetchOrU64(std::atomic<std::uint64_t>& a, std::uint64_t bits,
                                 std::memory_order order = std::memory_order_relaxed) {
        std::uint64_t cur = a.load(order);
        while (!a.compare_exchange_weak(cur, (cur | bits), order, order));
    }

    enum class ClearReason { Success, Timeout, Cancelled };

    void ClearExclusivePending(std::size_t s, ClearReason reason) {
        if (reason != ClearReason::Success) {
            for (auto const& ev : g_retainedEvents[s]) {
                IntegratedMagic::detail::EnqueueRetainedEvent(ev.dev, ev.rawIdCode, ev.userEvent, ev.value,
                                                              ev.heldSecs);
            }
        }
        g_retainedEvents[s].clear();
        g_exclusivePendingSrc[s] = PendingSrc::None;
        g_exclusivePendingTimer[s] = 0.0f;
        g_slotFullComboSeen[s] = false;
    }

    void DiscardExclusivePending(std::size_t s) {
        g_retainedEvents[s].clear();
        g_exclusivePendingSrc[s] = PendingSrc::None;
        g_exclusivePendingTimer[s] = 0.0f;
        g_slotFullComboSeen[s] = false;
    }

    void ClearEdgeStateOnly() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_slotDown[s].store(false, std::memory_order_relaxed);
            g_slotWasAccepted[s] = false;
            g_slotFullComboSeen[s] = false;
            g_slotFullComboSeen[s] = false;
            g_prevRawKbDown[s] = false;
            g_prevRawGpDown[s] = false;
            DiscardExclusivePending(s);
        }
        g_pressedMask.store(0uLL, std::memory_order_relaxed);
        g_releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    template <class DownArr, class KeepArr, class CodesArr>
    inline void MarkKeepIfDown(const CodesArr& codes, const DownArr& down, KeepArr& keep) {
        for (int code : codes) {
            if (code < 0 || code >= kMaxCode) continue;
            const auto idx = static_cast<std::size_t>(code);
            if (down[idx].load(std::memory_order_relaxed)) keep[idx] = true;
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
            if (!keepKb[idx]) g_kbDown[idx].store(false, std::memory_order_relaxed);
            if (!keepGp[idx]) g_gpDown[idx].store(false, std::memory_order_relaxed);
        }
        g_kbDown[static_cast<std::size_t>(kDIK_Escape)].store(false, std::memory_order_relaxed);
        ClearEdgeStateOnly();
    }

    void ResetExclusiveState() {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            g_prevRawKbDown[s] = false;
            g_prevRawGpDown[s] = false;
            DiscardExclusivePending(s);
            g_slotDown[s].store(false, std::memory_order_relaxed);
            g_slotWasAccepted[s] = false;
        }
        g_pressedMask.store(0uLL, std::memory_order_relaxed);
        g_releasedMask.store(0uLL, std::memory_order_relaxed);
    }

    void HandleSlotPressed(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return;
        if (!RE::PlayerCharacter::GetSingleton()) return;
        IntegratedMagic::MagicState::Get().OnSlotPressed(slot);
    }

    void HandleSlotReleased(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return;
        IntegratedMagic::MagicState::Get().OnSlotReleased(slot);
    }

    bool SlotComboDown(int slot) {
        if (slot < 0 || slot >= ActiveSlots()) return false;
        const auto& hk = g_cache[static_cast<std::size_t>(slot)];
        return ComboDown(hk.kb, g_kbDown) || ComboDown(hk.gp, g_gpDown);
    }

    // Verifica se alguma tecla do combo esta pressionada (qualquer uma, nao todas)
    template <class DownArr>
    bool AnyComboKeyDown(const std::array<int, 3>& combo, const DownArr& down) {
        return std::ranges::any_of(combo, [&](int code) {
            if (code < 0 || code >= kMaxCode) return false;
            return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
        });
    }

    bool ComputeAcceptedExclusive(int slot, const SlotHotkeys& hk, bool prevAccepted, bool kbNow, bool gpNow,
                                  bool rawNow, float dt) {
        const auto s = static_cast<std::size_t>(slot);
        const bool isMulti = g_slotIsMultiKey[s];

        // Ler prev ANTES de sobrescrever — a deteccao de edge usa esses valores
        const bool kbPrev = g_prevRawKbDown[s];
        const bool gpPrev = g_prevRawGpDown[s];
        g_prevRawKbDown[s] = kbNow;
        g_prevRawGpDown[s] = gpNow;

        if (prevAccepted) {
            DiscardExclusivePending(s);
            return rawNow;
        }

        if (HasExclusivePending(s)) {
            const auto src = g_exclusivePendingSrc[s];
            const bool stillDown = (src == PendingSrc::Kb) ? kbNow : gpNow;

            // Verifica exclusividade — cancela se tecla nao pertencente ao combo foi pressionada
            const bool stillExcl = (src == PendingSrc::Kb)
                                       ? ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                                       : ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
            if (!stillExcl) {
                ClearExclusivePending(s, ClearReason::Cancelled);
                return false;
            }

            if (isMulti) {
                // Combo completo detectado pela primeira vez — inicia janela de exclusividade
                if (stillDown && !g_slotFullComboSeen[s]) {
                    g_slotFullComboSeen[s] = true;
                    g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
                }

                if (!stillDown) {
                    // Combo completo ja foi visto → ativa (usuario soltou uma tecla apos combo)
                    if (g_slotFullComboSeen[s]) {
                        DiscardExclusivePending(s);
                        return true;
                    }
                    // Combo completo nunca visto — verifica se ainda parcialmente pressionado
                    const bool anyHeld =
                        (src == PendingSrc::Gp) ? AnyComboKeyDown(hk.gp, g_gpDown) : AnyComboKeyDown(hk.kb, g_kbDown);
                    if (anyHeld) {
                        // Parcial: decrementa timeout de espera pela segunda tecla
                        g_exclusivePendingTimer[s] -= dt;
                        if (g_exclusivePendingTimer[s] <= 0.0f) {
                            // Tempo esgotado sem combo completo → cancela, repassa teclas retidas
                            ClearExclusivePending(s, ClearReason::Cancelled);
                        }
                        return false;
                    }
                    // Todas as teclas soltas sem combo completo → cancela
                    ClearExclusivePending(s, ClearReason::Cancelled);
                    return false;
                }

                // stillDown=true (combo completo pressionado): decrementa janela de exclusividade
                g_exclusivePendingTimer[s] -= dt;
                if (g_exclusivePendingTimer[s] <= 0.0f) {
                    // Janela passou sem tecla estranha → ativa, descarta teclas retidas
                    ClearExclusivePending(s, ClearReason::Success);
                    return true;
                }
                return false;

            } else {
                // Single-key: logica original
                if (!stillDown) {
                    DiscardExclusivePending(s);
                    return true;
                }
                g_exclusivePendingTimer[s] -= dt;
                if (g_exclusivePendingTimer[s] <= 0.0f) {
                    ClearExclusivePending(s, ClearReason::Success);
                    return true;
                }
                return false;
            }
        }

        // Sem pending ativo — detecta edge do combo completo (single-key ou multi-key juntos)
        const bool kbEdge = kbNow && !kbPrev;
        const bool gpEdge = gpNow && !gpPrev;

        if (kbEdge && ComboExclusiveNow(hk.kb, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)) {
            g_exclusivePendingSrc[s] = PendingSrc::Kb;
            g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            if (isMulti) g_slotFullComboSeen[s] = true;  // combo completo chegou de uma vez
            return false;
        }
        if (gpEdge && ComboExclusiveNow(hk.gp, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera)) {
            g_exclusivePendingSrc[s] = PendingSrc::Gp;
            g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;
            if (isMulti) g_slotFullComboSeen[s] = true;  // combo completo chegou de uma vez
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
            const bool prevAcc = g_slotDown[s].load(std::memory_order_relaxed);
            bool accNow = false;
            if (cfg.requireExclusiveHotkeyPatch || g_slotIsMultiKey[s]) {
                // Multi-key sempre usa o modo exclusivo para evitar vazamento de teclas modificadoras
                accNow = ComputeAcceptedExclusive(slot, hk, prevAcc, kbNow, gpNow, rawNow, dt);
            } else {
                g_prevRawKbDown[s] = kbNow;
                g_prevRawGpDown[s] = gpNow;
                DiscardExclusivePending(s);
                accNow = rawNow;
            }
            if (accNow != prevAcc) {
                g_slotDown[s].store(accNow, std::memory_order_relaxed);
                AtomicFetchOrU64(accNow ? g_pressedMask : g_releasedMask, (1uLL << slot));
            }
            // wasAccepted permanece true ate rawNow=false (todos os botoes soltos)
            if (accNow)
                g_slotWasAccepted[s] = true;
            else if (!rawNow)
                g_slotWasAccepted[s] = false;
        }
    }

    float CalculateDeltaTime() {
        using clock = std::chrono::steady_clock;
        static clock::time_point last = clock::now();
        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
        return dt;
    }

    std::optional<int> ConsumeBit(std::atomic<std::uint64_t>& maskAtomic) {
        while (true) {
            const int n = ActiveSlots();
            const std::uint64_t allowed = (n >= 64) ? ~0uLL : ((1uLL << n) - 1uLL);
            std::uint64_t curAll = maskAtomic.load(std::memory_order_relaxed);
            std::uint64_t cur = (curAll & allowed);
            if (cur == 0uLL) {
                if (curAll != 0uLL)
                    (void)maskAtomic.compare_exchange_weak(curAll, (curAll & allowed), std::memory_order_relaxed);
                return std::nullopt;
            }
            int idx = -1;
            for (int i = 0; i < n; ++i) {
                if (cur & (1uLL << i)) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) return std::nullopt;
            if (maskAtomic.compare_exchange_weak(curAll, (curAll & ~(1uLL << idx)), std::memory_order_relaxed))
                return idx;
        }
    }

    bool TryHandleCapture(const RE::ButtonEvent* btn, CaptureState& cap, bool& wantCapture, RE::INPUT_DEVICE dev,
                          int convertedCode) {
        if (!wantCapture || !btn->IsDown()) return false;
        if (dev == RE::INPUT_DEVICE::kKeyboard && convertedCode == kDIK_Escape) return false;
        int encoded = -1;
        if (dev == RE::INPUT_DEVICE::kKeyboard)
            encoded = convertedCode;
        else if (dev == RE::INPUT_DEVICE::kGamepad)
            encoded = -(convertedCode + 1);
        if (encoded == -1) return false;
        cap.capturedEncoded.store(encoded, std::memory_order_relaxed);
        cap.captureRequested.store(false, std::memory_order_relaxed);
        wantCapture = false;
        return true;
    }

    void UpdateDownState(RE::INPUT_DEVICE dev, int convertedCode, bool downNow) {
        if (dev == RE::INPUT_DEVICE::kKeyboard)
            g_kbDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
        else if (dev == RE::INPUT_DEVICE::kGamepad)
            g_gpDown[static_cast<std::size_t>(convertedCode)].store(downNow, std::memory_order_relaxed);
    }

    void DrainWhenBlocked() {
        for (auto s = Input::ConsumePressedSlot(); s.has_value(); s = Input::ConsumePressedSlot());
        for (auto s = Input::ConsumeReleasedSlot(); s.has_value(); s = Input::ConsumeReleasedSlot())
            HandleSlotReleased(*s);
    }

    void DispatchSlots() {
        for (auto s = Input::ConsumePressedSlot(); s.has_value(); s = Input::ConsumePressedSlot())
            HandleSlotPressed(*s);
        for (auto s = Input::ConsumeReleasedSlot(); s.has_value(); s = Input::ConsumeReleasedSlot())
            HandleSlotReleased(*s);
    }

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

    bool ShouldFilterAndSave(RE::INPUT_DEVICE dev, int convertedCode, std::uint32_t rawIdCode,
                             const RE::BSFixedString& userEvent, float value, float heldSecs) {
        const int n = ActiveSlots();
        for (int slot = 0; slot < n; ++slot) {
            const auto s = static_cast<std::size_t>(slot);
            const auto& hk = g_cache[s];
            const bool inKb = dev == RE::INPUT_DEVICE::kKeyboard && ComboContains(hk.kb, convertedCode);
            const bool inGp = dev == RE::INPUT_DEVICE::kGamepad && ComboContains(hk.gp, convertedCode);
            if (!inKb && !inGp) continue;

            const bool accepted = g_slotDown[s].load(std::memory_order_relaxed);

            if (accepted) {
                return true;
            }

            // Filtra enquanto wasAccepted=true (teclas ainda pressionadas apos combo ativo)
            if (g_slotWasAccepted[s]) return true;

            // Race guard: combo completo ja esta down mas accepted ainda nao propagou neste frame
            if (inKb && ComboDown(hk.kb, g_kbDown)) return true;
            if (inGp && ComboDown(hk.gp, g_gpDown)) return true;

            // Multi-key: inicia pending na PRIMEIRA tecla do combo para evitar que o modificador
            // (ex: R2) vaze para o jogo antes do combo completo ser detectado.
            if (g_slotIsMultiKey[s] && !HasExclusivePending(s)) {
                const PendingSrc src = inGp ? PendingSrc::Gp : PendingSrc::Kb;
                g_exclusivePendingSrc[s] = src;
                g_exclusivePendingTimer[s] = kExclusiveConfirmDelaySec;  // timeout parcial
                // Cai no bloco HasExclusivePending abaixo para reter o evento
            }

            // Re-verifica pending (pode ter sido iniciado acima)
            if (HasExclusivePending(s)) {
                if (const bool isHeld = (value > 0.5f && heldSecs > 0.0f); !isHeld) {
                    g_retainedEvents[s].emplace_back(dev, rawIdCode, userEvent, value, heldSecs);
                }
                return true;
            }
        }
        return false;
    }

    inline bool IsHudToggleCombo(RE::INPUT_DEVICE dev, int code) {
        if (dev == RE::INPUT_DEVICE::kKeyboard) return ComboContains(g_hudCache.kb, code);
        if (dev == RE::INPUT_DEVICE::kGamepad) return ComboContains(g_hudCache.gp, code);
        return false;
    }

    bool IsHudComboDown() { return ComboDown(g_hudCache.kb, g_kbDown) || ComboDown(g_hudCache.gp, g_gpDown); }

    bool ShouldFilterHudToggle(RE::INPUT_DEVICE dev, int convertedCode) { return IsHudToggleCombo(dev, convertedCode); }

    void LoadHotkeyCache_FromConfig() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();
        const auto n = static_cast<int>(cfg.SlotCount());
        g_slotCount.store(n, std::memory_order_relaxed);
        for (auto& s : g_cache) {
            s.kb = {-1, -1, -1};
            s.gp = {-1, -1, -1};
        }
        auto fill = [](SlotHotkeys& out, const auto& in) {
            out.kb[0] = in.KeyboardScanCode1.load(std::memory_order_relaxed);
            out.kb[1] = in.KeyboardScanCode2.load(std::memory_order_relaxed);
            out.kb[2] = in.KeyboardScanCode3.load(std::memory_order_relaxed);
            out.gp[0] = in.GamepadButton1.load(std::memory_order_relaxed);
            out.gp[1] = in.GamepadButton2.load(std::memory_order_relaxed);
            out.gp[2] = in.GamepadButton3.load(std::memory_order_relaxed);
        };
        const int m = std::min(n, kMaxSlots);
        for (int i = 0; i < m; ++i) {
            const auto s = static_cast<std::size_t>(i);
            fill(g_cache[s], cfg.slotInput[s]);
            const auto& hk = g_cache[s];
            const int kbKeys = std::count_if(hk.kb.begin(), hk.kb.end(), [](int c) { return c != -1; });
            const int gpKeys = std::count_if(hk.gp.begin(), hk.gp.end(), [](int c) { return c != -1; });
            g_slotIsMultiKey[s] = (kbKeys > 1) || (gpKeys > 1);
        }

        g_hudCache = {};
        fill(g_hudCache, cfg.hudPopupInput);
    }

    void ProcessButtonEvents(RE::InputEvent** a_evns, CaptureState& cap, bool& wantCapture) {
        for (auto* e = *a_evns; e; e = e->next) {
            const auto* btn = e->AsButtonEvent();
            if (!btn || (!btn->IsDown() && !btn->IsUp())) continue;

            const auto dev = btn->GetDevice();
            auto code = static_cast<int>(btn->idCode);

            if (dev == RE::INPUT_DEVICE::kGamepad) {
                code = GamepadIdToIndex(code);
            }

            if (code < 0 || code >= kMaxCode) continue;

            (void)TryHandleCapture(btn, cap, wantCapture, dev, code);
            UpdateDownState(dev, code, btn->IsDown());
        }
    }

    void UpdateHudToggleState() {
        static bool prevHudDown = false;  // NOSONAR

        const bool hudDown = IsHudComboDown();
        if (hudDown && !prevHudDown) {
            g_hudTogglePending.store(true, std::memory_order_relaxed);
        }

        prevHudDown = hudDown;
    }

    void UpdateSlotsIfAllowed(bool blocked, float dt) {
        if (!blocked) {
            RecomputeSlotEdges(dt);
        } else {
            DrainWhenBlocked();
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
            } else if (const auto* btn = cur->AsButtonEvent()) {
                if (btn->GetDevice() == RE::INPUT_DEVICE::kMouse && btn->GetIDCode() == 0) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseClick();
                    remove = true;
                } else if (btn->GetDevice() == RE::INPUT_DEVICE::kMouse && btn->GetIDCode() == 1) {
                    if (btn->IsDown()) IntegratedMagic::HUD::FeedMouseRightClick();
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

                if (dev == RE::INPUT_DEVICE::kGamepad) {
                    code = GamepadIdToIndex(code);
                }

                if (code >= 0 && code < kMaxCode) {
                    remove =
                        ShouldFilterAndSave(dev, code, rawCode, btn->QUserEvent(), btn->Value(), btn->HeldDuration()) ||
                        ShouldFilterHudToggle(dev, code);
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

    void DispatchIfAllowed(bool blocked, float dt) {
        if (!blocked) {
            DispatchSlots();
            IntegratedMagic::MagicState::Get().PumpAutoAttack(dt);
            IntegratedMagic::MagicState::Get().PumpAutomatic(dt);
        }
    }
}

void Input::ProcessAndFilter(RE::InputEvent** a_evns) {
    if (!a_evns) return;

    static bool prevBlocked = false;  // NOSONAR

    auto& cap = GetCaptureState();
    bool wantCapture = cap.captureRequested.load(std::memory_order_relaxed);

    const float dt = CalculateDeltaTime();
    const bool blocked = IsInputBlockedByMenus();

    if (prevBlocked && !blocked) {
        ClearLikelyStuckKeysAfterMenuClose();
    }

    prevBlocked = blocked;

    ProcessButtonEvents(a_evns, cap, wantCapture);

    UpdateHudToggleState();

    UpdateSlotsIfAllowed(blocked, dt);

    FilterMouseForPopup(a_evns);

    if (!wantCapture) {
        FilterEvents(a_evns);
    }

    DispatchIfAllowed(blocked, dt);
}

void Input::OnConfigChanged() {
    LoadHotkeyCache_FromConfig();
    ResetExclusiveState();
}

std::optional<int> Input::GetDownSlotForSelection() {
    const int n = ActiveSlots();
    for (int slot = 0; slot < n; ++slot)
        if (SlotComboDown(slot)) return slot;
    return std::nullopt;
}

bool Input::IsSlotHotkeyDown(int slot) { return SlotComboDown(slot); }

void Input::RequestHotkeyCapture() {
    auto& cap = GetCaptureState();
    cap.captureRequested.store(true, std::memory_order_relaxed);
    cap.capturedEncoded.store(-1, std::memory_order_relaxed);
}

int Input::PollCapturedHotkey() {
    auto& cap = GetCaptureState();
    if (const int v = cap.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        cap.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }
    return -1;
}

std::optional<int> Input::ConsumePressedSlot() { return ConsumeBit(g_pressedMask); }
std::optional<int> Input::ConsumeReleasedSlot() { return ConsumeBit(g_releasedMask); }

bool Input::ConsumeHudToggle() { return g_hudTogglePending.exchange(false, std::memory_order_relaxed); }