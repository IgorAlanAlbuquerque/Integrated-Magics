#include "UI/HudFrameLogic.h"

#include "Config/ConfigAdapter.h"
#include "Config/Slots.h"
#include "PCH.h"
#include "Shared/AssignService.h"
#include "Shared/Hand.h"
#include "Shared/HoveredFormState.h"
#include "Shared/HudIntents.h"
#include "Shared/SlotMutation.h"
#include "Shared/SpellClassify.h"
#include "UI/HudState.h"
#include "UI/HudView.h"

namespace IntegratedMagic::HUD {

    namespace {
        void ApplySlotMutation(const SlotMutation& m) {
            if (m.leftSpell) Slots::SetSlotSpell(m.slot, Hand::Left, *m.leftSpell, true);
            if (m.rightSpell) Slots::SetSlotSpell(m.slot, Hand::Right, *m.rightSpell, true);
            if (m.shout) Slots::SetSlotShout(m.slot, *m.shout, true);
        }
    }

    void RefreshSlotCount() { g_slotCount.store(static_cast<int>(Slots::GetSlotCount())); }

    void EvaluateAndStoreHudVisibility(bool isSlotActive) {
        const auto& hud = Config::MagicConfigAdapter::Get();
        using enum Config::HudVisibilityFlag;

        if (hud.FlagSet(Always)) {
            g_hudShouldDraw.store(true);
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            g_hudShouldDraw.store(false);
            return;
        }

        bool should = false;
        if (hud.FlagSet(SlotActive) && isSlotActive) should = true;
        if (!should && hud.FlagSet(InCombat) && player->IsInCombat()) should = true;
        if (!should && hud.FlagSet(WeaponDrawn)) {
            using enum RE::WEAPON_STATE;
            const auto ws = player->AsActorState()->GetWeaponState();
            if (ws == kDrawn || ws == kWantToDraw || ws == kDrawing) should = true;
        }
        g_hudShouldDraw.store(should);
    }

    void ExecutePopupIntents() {
        for (const auto& it : DrainIntents()) {
            using HM = HoveredForm::MagicType;
            switch (it.kind) {
                case SlotIntentKind::AssignHovered: {
                    const auto t = HoveredForm::GetHoveredMagicType();
                    std::optional<SlotMutation> mutation;
                    if (t == HM::Shout || t == HM::Power) {
                        mutation = MagicAssign::ComputeShoutAssignment(it.slot);
                    } else if (t == HM::TwoHandedSpell) {
                        mutation = MagicAssign::ComputeSpellAssignment(it.slot, Hand::Left, 0u);
                    } else if (t == HM::RightOnlySpell) {
                        const auto existingLeft = Slots::GetSlotSpell(it.slot, Hand::Left);
                        mutation = MagicAssign::ComputeSpellAssignment(it.slot, Hand::Right, existingLeft);
                    } else if (t == HM::LeftOnlySpell) {
                        mutation = MagicAssign::ComputeSpellAssignment(it.slot, Hand::Left, 0u);
                    } else {
                        const auto hand = it.hoverRight ? Hand::Right : Hand::Left;
                        const auto existingLeft = (hand == Hand::Right) ? Slots::GetSlotSpell(it.slot, Hand::Left) : 0u;
                        mutation = MagicAssign::ComputeSpellAssignment(it.slot, hand, existingLeft);
                    }
                    if (mutation) ApplySlotMutation(*mutation);
                    break;
                }
                case SlotIntentKind::ClearSlot: {
                    const auto shID = Slots::GetSlotShout(it.slot);
                    const auto rID = Slots::GetSlotSpell(it.slot, Hand::Right);
                    const auto lID = Slots::GetSlotSpell(it.slot, Hand::Left);
                    const bool slotIs2H = !shID && !rID && lID &&
                                          SpellClassify::IsTwoHandedSpell(RE::TESForm::LookupByID<RE::SpellItem>(lID));

                    if (shID) {
                        ApplySlotMutation(MagicAssign::ComputeClearShout(it.slot));
                    } else if (slotIs2H) {
                        ApplySlotMutation(MagicAssign::ComputeClearHand(it.slot, Hand::Right));
                        ApplySlotMutation(MagicAssign::ComputeClearHand(it.slot, Hand::Left));
                    } else {
                        const auto hand = it.hoverRight ? Hand::Right : Hand::Left;
                        ApplySlotMutation(MagicAssign::ComputeClearHand(it.slot, hand));
                    }
                    break;
                }
                case SlotIntentKind::ClosePopup:
                    Config::MagicConfigAdapter::Get().FlushSpellSettingsIfDirty();
                    g_popupOpen.store(false);
                    break;
            }
        }
    }

    void ExecuteHotkeyAssignment(std::uint64_t justPressedMask, int slotCount) {
        if (!justPressedMask) return;
        const int n = std::min(slotCount, kMaxViewSlots);
        const auto t = HoveredForm::GetHoveredMagicType();
        using HM = HoveredForm::MagicType;
        for (int i = 0; i < n; ++i) {
            if (!(justPressedMask & (1uLL << static_cast<std::uint64_t>(i)))) continue;
            std::optional<SlotMutation> mutation;
            if (t == HM::Shout || t == HM::Power) {
                mutation = MagicAssign::ComputeShoutAssignment(i);
            } else if (t == HM::TwoHandedSpell || t == HM::LeftOnlySpell) {
                mutation = MagicAssign::ComputeSpellAssignment(i, Hand::Left, 0u);
            } else if (t == HM::RightOnlySpell) {
                const auto existingLeft = Slots::GetSlotSpell(i, Hand::Left);
                mutation = MagicAssign::ComputeSpellAssignment(i, Hand::Right, existingLeft);
            } else if (t != HM::None) {
                mutation = MagicAssign::ComputeSpellAssignment(i, Hand::Left, 0u);
            }
            if (mutation) ApplySlotMutation(*mutation);
        }
    }

    void FillHudViewFromConfig(HudView& v) {
        const auto& cfg = Config::MagicConfigAdapter::Get();
        v.modifierKbPos = cfg.ModifierKbPosition();
        v.modifierGpPos = cfg.ModifierGpPosition();

        if (v.slotCount > 0) {
            const auto bind0 = cfg.GetSlotBinding(0);
            if (v.modifierKbPos > 0) v.modifierKbCode = bind0.kb[v.modifierKbPos - 1];
            if (v.modifierGpPos > 0) v.modifierGpCode = bind0.gp[v.modifierGpPos - 1];
        }

        const int n = std::min(v.slotCount, kMaxViewSlots);
        for (int i = 0; i < n; ++i) {
            auto& s = v.slots[i];

            const auto rID = Slots::GetSlotSpell(i, Hand::Right);
            const auto lID = Slots::GetSlotSpell(i, Hand::Left);
            const auto shID = Slots::GetSlotShout(i);

            s.rightSpellID = rID;
            s.leftSpellID = lID;
            s.shoutFormID = shID;
            s.rightSpell = rID ? RE::TESForm::LookupByID<RE::SpellItem>(rID) : nullptr;
            s.leftSpell = lID ? RE::TESForm::LookupByID<RE::SpellItem>(lID) : nullptr;
            s.isTwoHanded = !shID && !rID && s.leftSpell && SpellClassify::IsTwoHandedSpell(s.leftSpell);

            if (shID) {
                s.labelForm = RE::TESForm::LookupByID(shID);
            } else if (s.isTwoHanded) {
                s.labelForm = s.leftSpell;
            }

            const auto b = cfg.GetSlotBinding(i);
            s.kbCodes = {b.kb[0], b.kb[1], b.kb[2]};
            s.gpCodes = {b.gp[0], b.gp[1], b.gp[2]};
        }

        StoreHudView(v);
    }

}
