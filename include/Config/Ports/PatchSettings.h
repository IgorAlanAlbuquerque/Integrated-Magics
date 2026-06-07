#pragma once

namespace IntegratedMagic::Config {

    class IPatchSettings {
    public:
        virtual ~IPatchSettings() = default;

        [[nodiscard]] virtual bool SkipEquipAnimation() const = 0;
        [[nodiscard]] virtual bool SkipEquipAnimationOnReturn() const = 0;
        [[nodiscard]] virtual bool RequireExclusiveHotkey() const = 0;
        [[nodiscard]] virtual bool PressBothAtSame() const = 0;
        [[nodiscard]] virtual bool SkipChanneling() const = 0;
    };
}