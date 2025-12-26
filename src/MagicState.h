#pragma once
#include "PCH.h"

namespace IntegratedMagic {
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

    class MagicState {
    public:
        static MagicState& Get();

        struct ExtraEquippedItem {
            RE::TESBoundObject* base{nullptr};
            RE::ExtraDataList* extra{nullptr};
        };

        std::vector<ExtraEquippedItem> _prevExtraEquipped;

        void TogglePress(int slot);
        bool IsActive() const;
        int ActiveSlot() const;

    private:
        void EnterPress(int slot);
        bool ApplyPress(int slot);
        void ExitPress();

        void CaptureSnapshot(RE::PlayerCharacter* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);
        void UpdatePrevExtraEquippedForOverlay(const std::function<void()>& equipFn);

        bool _active{false};
        int _activeSlot{-1};
        HandSnapshot _snap{};
    };

}
