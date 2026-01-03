#pragma once

#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>

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
    }

    struct SpellSettings;
    namespace detail {
        void EnqueueSyntheticAttack(RE::ButtonEvent* ev);
        RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);
        void DispatchAttack(EquipHand hand, float value, float heldSecs);
    }
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

    class MagicState {
    public:
        static MagicState& Get();

        struct ExtraEquippedItem {
            RE::TESBoundObject* base{nullptr};
            RE::ExtraDataList* extra{nullptr};
        };

        void TogglePress(int slot);
        void ToggleAutomatic(int slot);
        void HoldDown(int slot);
        void HoldUp(int slot);
        bool IsHoldActive() const { return _holdActive; }
        int HoldSlot() const { return _holdSlot; }
        bool IsAutomaticActive() const { return _autoActive; }
        int AutomaticSlot() const { return _autoSlot; }
        void StartAutoAttack(EquipHand hand);
        void StopAutoAttack();
        void PumpAutoAttack(float dt);
        void NotifyAttackEnabled();
        void RequestAutoAttackStart(EquipHand hand);
        void TryStartWaitingAutoAttack();
        void PumpAutomatic();
        void AutoExit();

    private:
        void EnterPress(int slot);
        bool ApplyPress(int slot);
        void EnterHold(int slot);
        void EnterAutomatic(int slot);
        void ExitAll();
        void SetModeSpellsFromHand(EquipHand hand, RE::SpellItem* spell);
        void EnsureActiveWithSnapshot(RE::PlayerCharacter* player, int slot);
        bool PrepareSlotSpell(int slot, bool checkMagicka, RE::PlayerCharacter*& player, RE::SpellItem*& spell,
                              SpellSettings& outSettings);

        void CaptureSnapshot(RE::PlayerCharacter* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);
        template <class Fn>
        void UpdatePrevExtraEquippedForOverlay(Fn&& equipFn);

        std::vector<ExtraEquippedItem> _prevExtraEquipped;
        bool _active{false};
        int _activeSlot{-1};
        bool _holdActive{false};
        int _holdSlot{-1};
        bool _autoActive{false};
        int _autoSlot{-1};
        EquipHand _autoHand{EquipHand::Right};
        bool _autoWaitingChargeComplete{false};
        bool _autoChargeComplete{false};
        HandSnapshot _snap{};
        bool _autoAttackHeld{false};
        EquipHand _autoAttackHand{EquipHand::Right};
        float _autoAttackSecs{0.0f};
        bool _attackEnabled{false};
        bool _waitingAutoAfterEquip{false};
        EquipHand _waitingAutoHand{EquipHand::Right};
        RE::SpellItem* _modeSpellLeft{nullptr};
        RE::SpellItem* _modeSpellRight{nullptr};
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
