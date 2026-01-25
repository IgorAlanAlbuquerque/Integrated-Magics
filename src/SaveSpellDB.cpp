#include "SaveSpellDB.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "PCH.h"
#include "SpellSettingsDB.h"

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

        std::size_t _clampedCount(std::size_t n) { return std::min<std::size_t>(n, kJsonSlotsHardCap); }

        void _resizeBoth(IntegratedMagic::SaveSpellSlots& s, std::size_t count) {
            s.left.resize(count, 0u);
            s.right.resize(count, 0u);
        }

        IntegratedMagic::SaveSpellSlots _migrateV2ArrayToLR(const nlohmann::json& arr) {
            IntegratedMagic::SaveSpellSlots s{};
            const std::size_t count = _clampedCount(arr.size());
            _resizeBoth(s, count);

            for (std::size_t i = 0; i < count; ++i) {
                const std::uint32_t id = _toU32Clamped(arr.at(i));
                if (id == 0u) {
                    continue;
                }

                auto settings = IntegratedMagic::SpellSettingsDB::Get().GetOrCreate(id);

                switch (settings.hand) {
                    using enum IntegratedMagic::EquipHand;
                    case Left:
                        s.left[i] = id;
                        break;
                    case Right:
                        s.right[i] = id;
                        break;
                    case Both:
                        s.left[i] = id;
                        s.right[i] = id;
                        break;
                    default:
                        s.right[i] = id;
                        break;
                }
            }

            return s;
        }

        IntegratedMagic::SaveSpellSlots _parseV3ObjectToLR(const nlohmann::json& obj) {
            IntegratedMagic::SaveSpellSlots s{};

            const auto itL = obj.find("left");
            const auto itR = obj.find("right");
            if (itL == obj.end() || itR == obj.end() || !itL->is_array() || !itR->is_array()) {
                return s;
            }

            const std::size_t count = _clampedCount(std::max(itL->size(), itR->size()));
            _resizeBoth(s, count);

            for (std::size_t i = 0; i < count; ++i) {
                if (i < itL->size()) s.left[i] = _toU32Clamped(itL->at(i));
                if (i < itR->size()) s.right[i] = _toU32Clamped(itR->at(i));
            }

            return s;
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

                const auto& v = it.value();

                SaveSpellSlots slots{};

                if (v.is_object()) {
                    slots = _parseV3ObjectToLR(v);
                } else if (v.is_array()) {
                    slots = _migrateV2ArrayToLR(v);
                } else {
                    continue;
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
        j["version"] = 3;

        nlohmann::json saves = nlohmann::json::object();
        for (auto const& [key, slots] : _bySave) {
            nlohmann::json obj;
            obj["left"] = slots.left;
            obj["right"] = slots.right;
            saves[key] = std::move(obj);
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