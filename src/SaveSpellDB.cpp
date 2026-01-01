#include "SaveSpellDB.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "PCH.h"

namespace IntegratedMagic {
    SaveSpellDB& SaveSpellDB::Get() {
        static SaveSpellDB g;  // NOSONAR
        return g;
    }

    std::filesystem::path SaveSpellDB::JsonPath() { return GetThisDllDir() / "SaveSpells.json"; }

    std::string SaveSpellDB::NormalizeKey(std::string key) {
        for (auto& c : key) {
            if (c == '/') c = '\\';
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
        if (savesIt == j.end()) {
            return;
        }
        if (!savesIt->is_object()) {
            return;
        }

        for (auto it = savesIt->begin(); it != savesIt->end(); ++it) {
            try {
                const std::string rawKey = it.key();
                const std::string key = NormalizeKey(rawKey);

                const auto& arr = it.value();

                if (!arr.is_array() || arr.size() != 4) {
                    continue;
                }

                SaveSpellSlots slots{};
                for (std::size_t i = 0; i < 4; ++i) {
                    slots.slotSpellFormID[i] = arr.at(i).get<std::uint32_t>();
                }

                _bySave.insert_or_assign(key, slots);

            } catch (const nlohmann::json::type_error& e) {
                spdlog::error("[IMAGIC][SaveSpellDB] Type error for key='{}': {}", it.key(), e.what());
            } catch (const nlohmann::json::out_of_range& e) {
                spdlog::error("[IMAGIC][SaveSpellDB] Out of range for key='{}': {}", it.key(), e.what());
            } catch (const nlohmann::json::exception& e) {
                spdlog::error("[IMAGIC][SaveSpellDB] JSON exception for key='{}': {}", it.key(), e.what());
            }
        }
    }

    void SaveSpellDB::SaveToDisk() {
        std::scoped_lock lk(_mtx);

        nlohmann::json j;
        j["version"] = 1;

        nlohmann::json saves = nlohmann::json::object();
        for (auto const& [key, slots] : _bySave) {
            saves[key] = nlohmann::json::array({slots.slotSpellFormID[0], slots.slotSpellFormID[1],
                                                slots.slotSpellFormID[2], slots.slotSpellFormID[3]});
        }
        j["saves"] = std::move(saves);

        const auto path = JsonPath();
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream out(path);
        out << j.dump(2);
    }

    void SaveSpellDB::Upsert(std::string_view saveKey, const SaveSpellSlots& slots) {
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
