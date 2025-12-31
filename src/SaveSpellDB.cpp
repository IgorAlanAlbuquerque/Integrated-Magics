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

                if (!arr.is_array()) {
                    continue;
                }
                if (arr.size() != 4) {
                    continue;
                }

                SaveSpellSlots slots{};

                for (std::size_t i = 0; i < 4; ++i) {
                    slots.slotSpellFormID[i] = arr[i].get<std::uint32_t>();
                }

                _bySave[key] = slots;

            } catch (const std::exception& e) {
                spdlog::error("[IMAGIC][SaveSpellDB] Exception while reading entry: {}", e.what());

            } catch (...) {
                spdlog::error("[IMAGIC][SaveSpellDB] Unknown exception while reading entry");
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

    void SaveSpellDB::Upsert(const std::string& saveKey, const SaveSpellSlots& slots) {
        std::scoped_lock lk(_mtx);
        _bySave[NormalizeKey(saveKey)] = slots;
    }

    bool SaveSpellDB::TryGet(const std::string& saveKey, SaveSpellSlots& out) const {
        std::scoped_lock lk(_mtx);
        auto it = _bySave.find(NormalizeKey(saveKey));
        if (it == _bySave.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    void SaveSpellDB::Erase(const std::string& saveKey) {
        std::scoped_lock lk(_mtx);
        _bySave.erase(NormalizeKey(saveKey));
    }
}
