#pragma once

#include <cstdint>
#include <optional>

#include "Shared/SpellSettings.h"

namespace RE {
    class TESForm;
}

namespace IntegratedMagic::Config {

    class ISpellSettings {
    public:
        virtual ~ISpellSettings() = default;

        [[nodiscard]] virtual std::optional<SpellSettings> GetSpellSettings(std::uint32_t formID) const = 0;
        virtual SpellSettings GetOrCreateSpellSettings(std::uint32_t formID, const RE::TESForm* form) = 0;
        virtual void SetSpellSettings(std::uint32_t formID, const SpellSettings& s) = 0;
        virtual void FlushSpellSettingsIfDirty() = 0;
    };

}