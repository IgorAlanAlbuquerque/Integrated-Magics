#include "SaveSpellDB.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "PCH.h"

namespace IntegratedMagic {

    namespace {

        constexpr std::size_t kJsonSlotsHardCap = 256;

        std::uint32_t _toU32Clamped(const nlohmann::json& v) {
            if (v.is_number_unsigned()) {
                const auto x = v.get<std::uint64_t>();
                return (x > 0xFFFFFFFFuLL) ? 0xFFFFFFFFu : static_cast<std::uint32_t>(x);
            }
            if (v.is_number_integer()) {
                const auto x = v.get<std::int64_t>();
                if (x <= 0) {
                    return 0u;
                }
                return (x > 0xFFFFFFFFLL) ? 0xFFFFFFFFu : static_cast<std::uint32_t>(x);
            }
            return 0u;
        }
    }

    SaveSpellDB& SaveSpellDB::Get() {
        static SaveSpellDB g;  // NOSONAR
        return g;
    }

    std::filesystem::path SaveSpellDB::JsonPath() { return GetThisDllDir() / "SaveSpells.json"; }

    std::string SaveSpellDB::NormalizeKey(std::string key) {
        for (auto& c : key) {
            if (c == '/') {
                c = '\\';
            }
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        }
        return key;
    }

    void SaveSpellDB::LoadFromDisk() {
        std::scoped_lock lk(_mtx);
        _bySave.clear();

        const auto path = JsonPath();
        std::ifstream in(path);
        if (!in.good()) {
            return;
        }

        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded()) {
            return;
        }

        auto savesIt = j.find("saves");
        if (savesIt == j.end() || !savesIt->is_object()) {
            return;
        }

        for (auto it = savesIt->begin(); it != savesIt->end(); ++it) {
            try {
                const std::string rawKey = it.key();
                const std::string key = NormalizeKey(rawKey);

                const auto& arr = it.value();
                if (!arr.is_array()) {
                    continue;
                }

                SaveSpellSlots slots{};
                const std::size_t count = std::min<std::size_t>(arr.size(), kJsonSlotsHardCap);
                slots.slotSpellFormID.resize(count, 0u);

                for (std::size_t i = 0; i < count; ++i) {
                    slots.slotSpellFormID[i] = _toU32Clamped(arr.at(i));
                }

                _bySave.insert_or_assign(key, std::move(slots));

            } catch (const nlohmann::json::exception& e) {
                spdlog::error("[IMAGIC][SaveSpellDB] JSON exception for key='{}': {}", it.key(), e.what());
            } catch (const std::exception& e) {  // NOSONAR
                spdlog::error("[IMAGIC][SaveSpellDB] Std exception while reading entry: {}", e.what());
            }
        }
    }

    void SaveSpellDB::SaveToDisk() {
        std::scoped_lock lk(_mtx);

        nlohmann::json j;
        j["version"] = 2;

        nlohmann::json saves = nlohmann::json::object();
        for (auto const& [key, slots] : _bySave) {
            saves[key] = slots.slotSpellFormID;
        }

        j["saves"] = std::move(saves);

        const auto path = JsonPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream out(path);
        out << j.dump(2);
    }

    void SaveSpellDB::Upsert(std::string_view saveKey, const SaveSpellSlots& slots) {
        std::scoped_lock lk(_mtx);
        auto key = NormalizeKey(std::string(saveKey));
        _bySave.insert_or_assign(std::move(key), slots);
    }

    bool SaveSpellDB::TryGet(std::string_view saveKey, SaveSpellSlots& out) const {
        std::scoped_lock lk(_mtx);
        auto key = NormalizeKey(std::string(saveKey));
        auto it = _bySave.find(key);
        if (it == _bySave.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    void SaveSpellDB::Erase(std::string_view saveKey) {
        std::scoped_lock lk(_mtx);
        auto key = NormalizeKey(std::string(saveKey));
        _bySave.erase(key);
    }
}