#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace IntegratedMagic {
    struct SaveSpellSlots {
        std::array<std::uint32_t, 4> slotSpellFormID{{0, 0, 0, 0}};
    };

    class SaveSpellDB {
    public:
        static SaveSpellDB& Get();

        void LoadFromDisk();
        void SaveToDisk();

        void Upsert(const std::string& saveKey, const SaveSpellSlots& slots);
        bool TryGet(const std::string& saveKey, SaveSpellSlots& out) const;
        void Erase(const std::string& saveKey);

        static std::filesystem::path JsonPath();
        static std::string NormalizeKey(std::string key);

    private:
        SaveSpellDB() = default;

        mutable std::mutex _mtx;
        std::unordered_map<std::string, SaveSpellSlots> _bySave;
    };
}
