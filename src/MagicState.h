#pragma once

#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "MagicSlots.h"
#include "PCH.h"

namespace IntegratedMagic {

    namespace detail {
        constexpr std::uint32_t kRightAttackMouseId = 0;
        constexpr std::uint32_t kLeftAttackMouseId = 1;

        inline const RE::BSFixedString kRightAttackEvent{"Right Attack/Block"};
        inline const RE::BSFixedString kLeftAttackEvent{"Left Attack/Block"};

        inline const RE::BSFixedString& RightAttackEvent() { return kRightAttackEvent; }
        inline const RE::BSFixedString& LeftAttackEvent() { return kLeftAttackEvent; }

        struct SyntheticInputState {
            std::mutex mutex;
            std::queue<RE::ButtonEvent*> pending;
        };

        void EnqueueSyntheticAttack(RE::ButtonEvent* ev);
        RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);

        void DispatchAttack(IntegratedMagic::MagicSlots::Hand hand, float value, float heldSecs);
    }

    struct SpellSettings;

    struct ObjSnapshot {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
        RE::FormID formID{0};
    };

    struct HandSnapshot {
        ObjSnapshot rightObj{};
        ObjSnapshot leftObj{};
        RE::MagicItem* rightSpell{nullptr};
        RE::MagicItem* leftSpell{nullptr};
        bool valid{false};
    };

    struct InventoryIndex {
        std::unordered_map<RE::TESBoundObject*, std::vector<RE::ExtraDataList*>> extrasByBase;
        std::unordered_set<RE::TESBoundObject*> wornBases;
    };

    InventoryIndex BuildInventoryIndex(RE::PlayerCharacter* player);

    struct HandMode {
        IntegratedMagic::ActivationMode mode{IntegratedMagic::ActivationMode::Hold};
        bool wantAutoAttack{true};

        bool pressActive{false};
        bool holdActive{false};
        bool autoActive{false};

        bool waitingChargeComplete{false};
        bool chargeComplete{false};

        bool waitingAutoAfterEquip{false};
        bool holdFiredAndWaitingCastStop{false};

        bool finished{false};
        float waitingEnableBumperSecs{0.0f};
    };

    class MagicState {
    public:
        static MagicState& Get();

        struct ExtraEquippedItem {
            RE::TESBoundObject* base{nullptr};
            RE::ExtraDataList* extra{nullptr};
        };

        void OnSlotPressed(int slot);
        void OnSlotReleased(int slot);
        void OnCastStop();
        void TryFinalizeExit();

        bool IsActive() const noexcept { return _active; }
        int ActiveSlot() const noexcept { return _activeSlot; }

        void StartAutoAttack(IntegratedMagic::MagicSlots::Hand hand);
        void StopAutoAttack(IntegratedMagic::MagicSlots::Hand hand);
        void StopAllAutoAttack();
        void PumpAutoAttack(float dt);

        void NotifyAttackEnabled();

        void PumpAutomatic(float dt);

        const HandMode& LeftMode() const noexcept { return _left; }
        const HandMode& RightMode() const noexcept { return _right; }

    private:
        MagicState() = default;

        static inline RE::PlayerCharacter* GetPlayer() { return RE::PlayerCharacter::GetSingleton(); }
        HandMode& ModeFor(IntegratedMagic::MagicSlots::Hand hand) noexcept {
            return (hand == IntegratedMagic::MagicSlots::Hand::Left) ? _left : _right;
        }
        const HandMode& ModeFor(IntegratedMagic::MagicSlots::Hand hand) const noexcept {
            return (hand == IntegratedMagic::MagicSlots::Hand::Left) ? _left : _right;
        }

        void EnsureActiveWithSnapshot(RE::PlayerCharacter* player, int slot);
        void CaptureSnapshot(RE::PlayerCharacter const* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);
        bool HandIsRelevant(IntegratedMagic::MagicSlots::Hand h) const;
        bool AllRelevantHandsFinished() const;
        void ExitAllNow();
        bool CanOverwriteNow() const;
        void PrepareForOverwriteToSlot(int newSlot);
        void PumpAutoStartFallback(IntegratedMagic::MagicSlots::Hand hand, float dt);
        void DisableHand(IntegratedMagic::MagicSlots::Hand hand);

        struct SlotEntry {
            RE::PlayerCharacter* player{nullptr};
            std::uint32_t leftID{0};
            std::uint32_t rightID{0};
            RE::SpellItem* leftSpell{nullptr};
            RE::SpellItem* rightSpell{nullptr};
            SpellSettings leftSettings{};
            SpellSettings rightSettings{};
            bool hasLeft{false};
            bool hasRight{false};
        };

        bool PrepareSlotEntry(int slot, SlotEntry& out);

        void EnterHand(IntegratedMagic::MagicSlots::Hand hand, const SpellSettings& ss);
        void TogglePressHand(IntegratedMagic::MagicSlots::Hand hand, const SpellSettings& ss);
        void FinishHand(IntegratedMagic::MagicSlots::Hand hand);

        void PumpAutomaticHand(IntegratedMagic::MagicSlots::Hand hand);

        void SetModeSpellsFromHand(IntegratedMagic::MagicSlots::Hand hand, RE::SpellItem* spell);

        static inline bool IsLeft(IntegratedMagic::MagicSlots::Hand h) {
            return h == IntegratedMagic::MagicSlots::Hand::Left;
        }

        void MarkDirty(IntegratedMagic::MagicSlots::Hand h) {
            if (IsLeft(h))
                _dirtyLeft = true;
            else
                _dirtyRight = true;
        }

        template <class Fn>
        void UpdatePrevExtraEquippedForOverlay(Fn&& equipFn);

    private:
        std::vector<ExtraEquippedItem> _prevExtraEquipped;

        HandMode _left{};
        HandMode _right{};

        bool _active{false};
        int _activeSlot{-1};

        bool _aaHeldLeft{false};
        bool _aaHeldRight{false};
        float _aaSecsLeft{0.f};
        float _aaSecsRight{0.f};

        bool _attackEnabled{false};

        HandSnapshot _snap{};

        RE::SpellItem* _modeSpellLeft{nullptr};
        RE::SpellItem* _modeSpellRight{nullptr};
        bool _dirtyLeft{false};
        bool _dirtyRight{false};
    };

    template <class Fn>
    void MagicState::UpdatePrevExtraEquippedForOverlay(Fn&& equipFn) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        auto before = BuildInventoryIndex(player);

        std::forward<Fn>(equipFn)();

        auto after = BuildInventoryIndex(player);

        for (auto* base : before.wornBases) {
            if (after.wornBases.contains(base)) {
                continue;
            }

            ExtraEquippedItem item{base, nullptr};

            bool exists = false;
            for (auto const& e : _prevExtraEquipped) {
                if (e.base == item.base) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                _prevExtraEquipped.push_back(item);
            }
        }
    }
}