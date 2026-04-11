
#include "ConfigAdapter.h"

#include <utility>

namespace IntegratedMagic::Config {

    MagicConfigAdapter& MagicConfigAdapter::Get() {
        static MagicConfigAdapter inst;  // NOSONAR
        return inst;
    }

    MagicConfig& MagicConfigAdapter::Cfg() const { return GetMagicConfig(); }

    int MagicConfigAdapter::SlotCount() const { return static_cast<int>(Cfg().SlotCount()); }

    std::uint32_t MagicConfigAdapter::GetSpell(int slot, bool leftHand) const {
        if (slot < 0 || static_cast<std::uint32_t>(slot) >= Cfg().SlotCount()) return 0u;
        const auto idx = static_cast<std::size_t>(slot);
        const auto& arr = leftHand ? m_currentSlots.left : m_currentSlots.right;
        return idx < arr.size() ? arr[idx] : 0u;
    }

    std::uint32_t MagicConfigAdapter::GetShout(int slot) const {
        if (slot < 0 || static_cast<std::uint32_t>(slot) >= Cfg().SlotCount()) return 0u;
        const auto idx = static_cast<std::size_t>(slot);
        return idx < m_currentSlots.shout.size() ? m_currentSlots.shout[idx] : 0u;
    }

    SpellTypeDefaults MagicConfigAdapter::GetSpellDefaults(SpellType t) const {
        const auto& d = Cfg().spellTypeDefaults[static_cast<int>(std::to_underlying(t))];
        return {d.mode, d.autoAttack};
    }

    void MagicConfigAdapter::SetSpell(int slot, bool leftHand, std::uint32_t formID) {
        if (slot < 0 || static_cast<std::uint32_t>(slot) >= Cfg().SlotCount()) return;
        const auto idx = static_cast<std::size_t>(slot);
        const auto n = static_cast<std::size_t>(Cfg().SlotCount());

        if (m_currentSlots.left.size() < n) m_currentSlots.left.resize(n, 0u);
        if (m_currentSlots.right.size() < n) m_currentSlots.right.resize(n, 0u);
        if (m_currentSlots.shout.size() < n) m_currentSlots.shout.resize(n, 0u);

        auto& arr = leftHand ? m_currentSlots.left : m_currentSlots.right;
        arr[idx] = formID;
        if (formID != 0u) m_currentSlots.shout[idx] = 0u;
    }

    void MagicConfigAdapter::SetShout(int slot, std::uint32_t formID) {
        if (slot < 0 || static_cast<std::uint32_t>(slot) >= Cfg().SlotCount()) return;
        const auto idx = static_cast<std::size_t>(slot);
        const auto n = static_cast<std::size_t>(Cfg().SlotCount());

        if (m_currentSlots.left.size() < n) m_currentSlots.left.resize(n, 0u);
        if (m_currentSlots.right.size() < n) m_currentSlots.right.resize(n, 0u);
        if (m_currentSlots.shout.size() < n) m_currentSlots.shout.resize(n, 0u);

        m_currentSlots.shout[idx] = formID;
        if (formID != 0u) {
            m_currentSlots.left[idx] = 0u;
            m_currentSlots.right[idx] = 0u;
        }
    }

    void MagicConfigAdapter::ClearSlots() {
        const auto n = static_cast<std::size_t>(Cfg().SlotCount());
        m_currentSlots.left.assign(n, 0u);
        m_currentSlots.right.assign(n, 0u);
        m_currentSlots.shout.assign(n, 0u);
    }

    void MagicConfigAdapter::ReadAllSlots(std::vector<std::uint32_t>& outLeft, std::vector<std::uint32_t>& outRight,
                                          std::vector<std::uint32_t>& outShout) const {
        outLeft = m_currentSlots.left;
        outRight = m_currentSlots.right;
        outShout = m_currentSlots.shout;
    }

    void MagicConfigAdapter::ApplyAllSlots(const std::vector<std::uint32_t>& left,
                                           const std::vector<std::uint32_t>& right,
                                           const std::vector<std::uint32_t>& shout) {
        m_currentSlots.left = left;
        m_currentSlots.right = right;
        m_currentSlots.shout = shout;
    }

    void MagicConfigAdapter::Save() { Cfg().Save(); }

    SlotBinding MagicConfigAdapter::GetSlotBinding(int slot) const {
        if (slot < 0 || static_cast<std::uint32_t>(slot) >= Cfg().SlotCount()) return {};
        const auto& ic = Cfg().slotInput[static_cast<std::size_t>(slot)];
        return {
            {ic.KeyboardScanCode1.load(std::memory_order_relaxed), ic.KeyboardScanCode2.load(std::memory_order_relaxed),
             ic.KeyboardScanCode3.load(std::memory_order_relaxed)},
            {ic.GamepadButton1.load(std::memory_order_relaxed), ic.GamepadButton2.load(std::memory_order_relaxed),
             ic.GamepadButton3.load(std::memory_order_relaxed)}};
    }

    SlotBinding MagicConfigAdapter::GetHudToggleBinding() const {
        const auto& ic = Cfg().hudPopupInput;
        return {
            {ic.KeyboardScanCode1.load(std::memory_order_relaxed), ic.KeyboardScanCode2.load(std::memory_order_relaxed),
             ic.KeyboardScanCode3.load(std::memory_order_relaxed)},
            {ic.GamepadButton1.load(std::memory_order_relaxed), ic.GamepadButton2.load(std::memory_order_relaxed),
             ic.GamepadButton3.load(std::memory_order_relaxed)}};
    }

    int MagicConfigAdapter::ModifierKbPosition() const { return Cfg().modifierKeyboardPosition; }
    int MagicConfigAdapter::ModifierGpPosition() const { return Cfg().modifierGamepadPosition; }
    bool MagicConfigAdapter::RequireExclusiveHotkey() const { return Cfg().requireExclusiveHotkeyPatch; }
    bool MagicConfigAdapter::PressBothAtSame() const { return Cfg().pressBothAtSamePatch; }

    bool MagicConfigAdapter::FlagSet(HudVisibilityFlag f) const {
        return Cfg().HudFlagSet(static_cast<IntegratedMagic::HudVisibilityFlag>(f));
    }

    bool MagicConfigAdapter::SkipEquipAnimation() const { return Cfg().skipEquipAnimationPatch; }
    bool MagicConfigAdapter::SkipEquipAnimationOnReturn() const { return Cfg().skipEquipAnimationOnReturnPatch; }

}