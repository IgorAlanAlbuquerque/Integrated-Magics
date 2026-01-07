#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace IntegratedMagic {
    struct TransparentSaveKeyHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
        std::size_t operator()(const std::string& s) const noexcept { return (*this)(std::string_view{s}); }
        std::size_t operator()(const char* s) const noexcept { return (*this)(std::string_view{s}); }
    };

    struct SaveSpellSlots {
        std::vector<std::uint32_t> slotSpellFormID;
    };

    class SaveSpellDB {
    public:
        static SaveSpellDB& Get();

        void LoadFromDisk();
        void SaveToDisk();

        void Upsert(std::string_view saveKey, const SaveSpellSlots& slots);
        bool TryGet(std::string_view saveKey, SaveSpellSlots& out) const;
        void Erase(std::string_view saveKey);

        static std::filesystem::path JsonPath();
        static std::string NormalizeKey(std::string key);

    private:
        SaveSpellDB() = default;

        mutable std::mutex _mtx;
        std::unordered_map<std::string, SaveSpellSlots, TransparentSaveKeyHash, std::equal_to<>> _bySave;
    };
}