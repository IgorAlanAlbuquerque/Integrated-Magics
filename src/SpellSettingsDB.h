#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "MagicConfigPath.h"

namespace IntegratedMagic {

    struct TransparentStringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }

        std::size_t operator()(const std::string& s) const noexcept { return (*this)(std::string_view{s}); }

        std::size_t operator()(const char* s) const noexcept { return (*this)(std::string_view{s}); }
    };

    enum class ActivationMode : std::uint32_t { Hold = 0, Press = 1, Automatic = 2 };

    struct SpellSettings {
        ActivationMode mode{ActivationMode::Hold};
        bool autoAttack{false};
    };

    class SpellSettingsDB {
    public:
        static SpellSettingsDB& Get();

        void Load();
        void Save() const;

        SpellSettings GetOrCreate(std::uint32_t spellFormID);

        void Set(std::uint32_t spellFormID, const SpellSettings& s);

        bool IsDirty() const;
        void ClearDirty();

        static std::filesystem::path JsonPath();

    private:
        mutable std::mutex _mtx{};
        std::unordered_map<std::string, IntegratedMagic::SpellSettings, TransparentStringHash, std::equal_to<>>
            _byKey{};
        bool _dirty{false};

        static std::string MakeKey(std::uint32_t spellFormID);
    };
}