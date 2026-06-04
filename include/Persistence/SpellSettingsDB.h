#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Shared/ConfigPath.h"
#include "Shared/SpellSettings.h"
#include "Shared/SpellType.h"

namespace IntegratedMagic {

    struct TransparentStringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }

        std::size_t operator()(const std::string& s) const noexcept { return (*this)(std::string_view{s}); }

        std::size_t operator()(const char* s) const noexcept { return (*this)(std::string_view{s}); }
    };

    class SpellSettingsDB {
    public:
        static SpellSettingsDB& Get();

        void Load();
        void Save() const;

        SpellSettings GetOrCreate(std::uint32_t spellFormID, const RE::TESForm* form,
                                  const SpellTypeDefaults* defaults = nullptr);
        [[nodiscard]] std::optional<SpellSettings> Get(std::uint32_t spellFormID) const;
        void Set(std::uint32_t spellFormID, const SpellSettings& s);
        [[nodiscard]] bool IsDirty() const;
        void ClearDirty();

        static std::filesystem::path JsonPath();

    private:
        mutable std::mutex _mtx{};

        std::unordered_map<std::string, IntegratedMagic::SpellSettings, TransparentStringHash, std::equal_to<>>
            _byKey{};

        bool _dirty{false};

        static std::string MakeLegacyKey(std::uint32_t runtimeFormID);
        static std::string MakeStableKey(const RE::TESForm* form);
        static std::string MakeStableKey(std::string_view pluginName, std::uint32_t localFormID);

        static std::uint32_t GetLocalFormID(const RE::TESForm* form);
        static std::string GetPluginName(const RE::TESForm* form);

        [[nodiscard]] std::optional<SpellSettings> GetNoLock(std::uint32_t spellFormID) const;
    };
}