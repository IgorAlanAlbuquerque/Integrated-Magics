#include "Persistence/SaveSpellDB.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>

#include "PCH.h"
#include "Persistence/SpellSettingsDB.h"

namespace IntegratedMagic {

    namespace {
        constexpr std::size_t kJsonSlotsHardCap = 64;

        constexpr std::uint32_t kInvalidFormID = 0u;

        std::string NormalizePluginName(std::string s) {
            for (auto& c : s) {
                c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }

        std::uint32_t HexToU32(std::string_view text) {
            if (text.empty()) {
                return 0u;
            }

            std::uint32_t value = 0u;
            const auto* first = text.data();
            const auto* last = text.data() + text.size();

            const auto [ptr, ec] = std::from_chars(first, last, value, 16);
            if (ec != std::errc{} || ptr != last) {
                return 0u;
            }

            return value;
        }

        std::string U32ToHex8(std::uint32_t value) { return std::format("{:08X}", value); }

        std::uint32_t ToU32Clamped(const nlohmann::json& v) {
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

        std::size_t ClampedCount(std::size_t n) { return std::min<std::size_t>(n, kJsonSlotsHardCap); }

        void ResizeAll(SaveSpellSlots& s, std::size_t count) {
            s.left.resize(count, 0u);
            s.right.resize(count, 0u);
            s.shout.resize(count, 0u);
        }

        std::string GetPluginName(const RE::TESForm* form) {
            if (!form) {
                return {};
            }

            const auto* file = form->GetFile(0);
            if (!file) {
                return {};
            }

            return file->fileName;
        }

        std::uint32_t GetLocalFormID(const RE::TESForm* form) {
            if (!form) {
                return 0u;
            }

            const auto runtimeID = form->GetFormID();
            const auto* file = form->GetFile(0);

            if (!file) {
                return runtimeID;
            }

            if (file->IsLight()) {
                return runtimeID & 0x00000FFFu;
            }

            return runtimeID & 0x00FFFFFFu;
        }

        std::string MakeStableFormKey(const RE::TESForm* form) {
            if (!form) {
                return {};
            }

            auto plugin = GetPluginName(form);
            const auto localID = GetLocalFormID(form);

            if (plugin.empty() || localID == 0u) {
                return {};
            }

            plugin = NormalizePluginName(std::move(plugin));
            return std::format("{}|{:08X}", plugin, localID);
        }

        std::string MakeStableFormKeyFromRuntimeID(std::uint32_t runtimeFormID) {
            if (runtimeFormID == 0u) {
                return {};
            }

            const auto* form = RE::TESForm::LookupByID(runtimeFormID);
            if (!form) {
                return {};
            }

            return MakeStableFormKey(form);
        }

        std::uint32_t ResolveStableFormKey(std::string_view key) {
            if (key.empty()) {
                return 0u;
            }

            const auto sep = key.find('|');
            if (sep == std::string_view::npos) {
                return HexToU32(key);
            }

            const auto plugin = key.substr(0, sep);
            const auto localHex = key.substr(sep + 1);

            if (plugin.empty() || localHex.empty()) {
                return 0u;
            }

            const auto localFormID = HexToU32(localHex);
            if (localFormID == 0u) {
                return 0u;
            }

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                return 0u;
            }

            const std::string pluginName{plugin};

            if (const auto* form = dataHandler->LookupForm(localFormID, pluginName)) {
                return form->GetFormID();
            }

            MAGIC_DEBUG_LOG("[SaveSpellDB] ResolveStableFormKey: failed key='{}' plugin='{}' localFormID={:#010x}",
                            std::string{key}, pluginName, localFormID);

            return 0u;
        }

        std::uint32_t ParseFormRefToRuntimeID(const nlohmann::json& v) {
            if (v.is_null()) {
                return 0u;
            }

            if (v.is_string()) {
                const auto s = v.get<std::string>();
                if (s.empty()) {
                    return 0u;
                }

                return ResolveStableFormKey(s);
            }

            return ToU32Clamped(v);
        }

        nlohmann::json RuntimeIDToDiskRef(std::uint32_t runtimeFormID) {
            if (runtimeFormID == 0u) {
                return "";
            }

            if (const auto stable = MakeStableFormKeyFromRuntimeID(runtimeFormID); !stable.empty()) {
                return stable;
            }

            return U32ToHex8(runtimeFormID);
        }

        nlohmann::json BuildDiskArrayFromRuntimeIDs(const std::vector<std::uint32_t>& ids) {
            nlohmann::json arr = nlohmann::json::array();

            for (const auto id : ids) {
                arr.push_back(RuntimeIDToDiskRef(id));
            }

            return arr;
        }

        void ParseDiskArrayToRuntimeIDs(const nlohmann::json& arr, std::vector<std::uint32_t>& out, std::size_t count) {
            out.resize(count, 0u);

            if (!arr.is_array()) {
                return;
            }

            const auto n = std::min<std::size_t>(count, arr.size());

            for (std::size_t i = 0; i < n; ++i) {
                out[i] = ParseFormRefToRuntimeID(arr.at(i));
            }
        }

        SaveSpellSlots MigrateV2ArrayToLR(const nlohmann::json& arr) {
            SaveSpellSlots s{};

            const std::size_t count = ClampedCount(arr.size());
            ResizeAll(s, count);

            for (std::size_t i = 0; i < count; ++i) {
                const std::uint32_t id = ParseFormRefToRuntimeID(arr.at(i));

                if (id == 0u) {
                    continue;
                }

                s.left[i] = id;
                s.right[i] = id;
            }

            return s;
        }

        SaveSpellSlots ParseObjectToLR(const nlohmann::json& obj) {
            SaveSpellSlots s{};

            const auto itL = obj.find("left");
            const auto itR = obj.find("right");
            const auto itS = obj.find("shout");

            if (itL == obj.end() || itR == obj.end() || !itL->is_array() || !itR->is_array()) {
                return s;
            }

            const std::size_t shoutSize = (itS != obj.end() && itS->is_array()) ? itS->size() : 0;
            const std::size_t count = ClampedCount(std::max({itL->size(), itR->size(), shoutSize}));

            ResizeAll(s, count);

            ParseDiskArrayToRuntimeIDs(*itL, s.left, count);
            ParseDiskArrayToRuntimeIDs(*itR, s.right, count);

            if (itS != obj.end() && itS->is_array()) {
                ParseDiskArrayToRuntimeIDs(*itS, s.shout, count);
            }

            return s;
        }

        nlohmann::json BuildJsonV4_NoLock(
            const std::unordered_map<std::string, SaveSpellSlots, TransparentSaveKeyHash, std::equal_to<>>& bySave) {
            nlohmann::json j;

            j["version"] = 4;
            j["keyFormat"] = "plugin|localFormID";

            nlohmann::json saves = nlohmann::json::object();

            for (auto const& [key, slots] : bySave) {
                nlohmann::json obj;

                obj["left"] = BuildDiskArrayFromRuntimeIDs(slots.left);
                obj["right"] = BuildDiskArrayFromRuntimeIDs(slots.right);
                obj["shout"] = BuildDiskArrayFromRuntimeIDs(slots.shout);

                saves[key] = std::move(obj);
            }

            j["saves"] = std::move(saves);
            return j;
        }

        void WriteJsonToDisk(const std::filesystem::path& path, const nlohmann::json& j) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            std::ofstream out(path);
            out << j.dump(2);
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

        MAGIC_DEBUG_LOG("[SaveSpellDB] LoadFromDisk: path='{}'", path.string());

        std::ifstream in(path);
        if (!in.good()) {
            spdlog::warn("[SaveSpellDB] LoadFromDisk: file not found or not readable");
            return;
        }

        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded()) {
            spdlog::error("[SaveSpellDB] LoadFromDisk: JSON parse failed");
            return;
        }

        auto savesIt = j.find("saves");
        if (savesIt == j.end() || !savesIt->is_object()) {
            spdlog::error("[SaveSpellDB] LoadFromDisk: 'saves' key missing or not object");
            return;
        }

        bool migratedAny = false;

        for (auto it = savesIt->begin(); it != savesIt->end(); ++it) {
            try {
                const std::string rawKey = it.key();
                const std::string key = NormalizeKey(rawKey);
                const auto& v = it.value();

                SaveSpellSlots slots{};

                if (v.is_object()) {
                    slots = ParseObjectToLR(v);

                    if (slots.Size() == 0) {
                        spdlog::warn("[SaveSpellDB] LoadFromDisk: key='{}' parsed to empty slots, skipping", key);
                        continue;
                    }

                    const auto version = j.value("version", 0);
                    if (version < 4) {
                        migratedAny = true;
                    }
                } else if (v.is_array()) {
                    slots = MigrateV2ArrayToLR(v);
                    migratedAny = true;

                    MAGIC_DEBUG_LOG("[SaveSpellDB] LoadFromDisk: key='{}' migrated from legacy array", key);
                } else {
                    spdlog::warn("[SaveSpellDB] LoadFromDisk: key='{}' unexpected value type, skipping", key);
                    continue;
                }

                _bySave.insert_or_assign(key, std::move(slots));
            } catch (const nlohmann::json::exception& e) {
                spdlog::error("[SaveSpellDB] LoadFromDisk: JSON exception key='{}': {}", it.key(), e.what());
            } catch (const std::exception& e) {
                spdlog::error("[SaveSpellDB] LoadFromDisk: exception key='{}': {}", it.key(), e.what());
            }
        }

        MAGIC_DEBUG_LOG("[SaveSpellDB] LoadFromDisk: total keys loaded={}", _bySave.size());

        if (migratedAny) {
            try {
                const auto outJson = BuildJsonV4_NoLock(_bySave);
                WriteJsonToDisk(path, outJson);
                MAGIC_DEBUG_LOG("[SaveSpellDB] LoadFromDisk: migrated file written as v4");
            } catch (const std::exception& e) {
                spdlog::error("[IMAGIC][SaveSpellDB] Failed to write migrated v4 JSON: {}", e.what());
            }
        }
    }

    void SaveSpellDB::SaveToDisk() {
        std::scoped_lock lk(_mtx);

        const auto path = JsonPath();

        MAGIC_DEBUG_LOG("[SaveSpellDB] SaveToDisk: path='{}' entries={}", path.string(), _bySave.size());

        const auto j = BuildJsonV4_NoLock(_bySave);
        WriteJsonToDisk(path, j);

        MAGIC_DEBUG_LOG("[SaveSpellDB] SaveToDisk: done");
    }

    bool SaveSpellDB::TryGet(std::string_view saveKey, SaveSpellSlots& out) const {
        const auto key = NormalizeKeyCopy(saveKey);
        return TryGetNormalized(key, out);
    }

    void SaveSpellDB::Erase(std::string_view saveKey) {
        const auto key = NormalizeKeyCopy(saveKey);
        EraseNormalized(key);
    }

    void SaveSpellDB::Upsert(std::string_view saveKey, const SaveSpellSlots& slots) {
        std::scoped_lock lk(_mtx);

        auto key = NormalizeKeyCopy(saveKey);

        MAGIC_DEBUG_LOG("[SaveSpellDB] Upsert: key='{}' slots={}", key, slots.Size());

        _bySave.insert_or_assign(std::move(key), slots);
    }

    std::string SaveSpellDB::NormalizeKeyCopy(std::string_view key) {
        std::string s(key);
        return NormalizeKey(std::move(s));
    }

    bool SaveSpellDB::TryGetNormalized(std::string_view normalizedKey, SaveSpellSlots& out) const {
        std::scoped_lock lk(_mtx);

        auto it = _bySave.find(normalizedKey);
        if (it == _bySave.end()) {
            MAGIC_DEBUG_LOG("[SaveSpellDB] TryGet: key='{}' NOT FOUND (total keys={})", normalizedKey, _bySave.size());
            return false;
        }

        MAGIC_DEBUG_LOG("[SaveSpellDB] TryGet: key='{}' FOUND slots={}", normalizedKey, it->second.Size());

        out = it->second;
        return true;
    }

    void SaveSpellDB::EraseNormalized(std::string_view normalizedKey) {
        std::scoped_lock lk(_mtx);
        _bySave.erase(normalizedKey);
    }
}