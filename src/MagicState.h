#pragma once
#include "PCH.h"

namespace IntegratedMagic {

    struct HandSnapshot {
        RE::TESForm* rightObject{nullptr};
        RE::TESForm* leftObject{nullptr};
        RE::MagicItem* rightSpell{nullptr};
        RE::MagicItem* leftSpell{nullptr};
        bool valid{false};
    };

    class MagicState {
    public:
        static MagicState& Get();

        void TogglePress(int slot);
        bool IsActive() const;
        int ActiveSlot() const;

    private:
        void EnterPress(int slot);
        void ExitPress();

        void CaptureSnapshot(RE::PlayerCharacter* player);
        void RestoreSnapshot(RE::PlayerCharacter* player);

        bool _active{false};
        int _activeSlot{-1};
        HandSnapshot _snap{};
    };

}
