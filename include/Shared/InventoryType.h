#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PCH.h"

namespace IntegratedMagic {
    struct InventoryIndex {
        std::unordered_map<RE::TESBoundObject*, std::vector<RE::ExtraDataList*>> extrasByBase;
        std::unordered_set<RE::TESBoundObject*> wornBases;
    };

    struct ObjSnapshot {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
        RE::FormID formID{0};
    };

    struct ExtraEquippedItem {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
    };
}