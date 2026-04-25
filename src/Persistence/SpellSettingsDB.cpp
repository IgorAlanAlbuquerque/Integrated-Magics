#include "Persistence/SpellSettingsDB.h"

#include <array>
#include <cstdio>
#include <format>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>

#include "Adapters/Inbound/SpellTypeDetector.h"
#include "PCH.h"

namespace {
    std::string_view MakeHex8View(std::uint32_t id, std::array<char, 9>& buf) {
        std::format_to_n(buf.begin(), 8, "{:08X}", id);
        buf[8] = '\0';
        return std::string_view{buf.data(), 8};
    }

    std::string NormalizePluginName(std::string s) {
        for (auto& c : s) {
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }
}

namespace IntegratedMagic {

    SpellSettingsDB& SpellSettingsDB::Get() {
        static SpellSettingsDB inst;  // NOSONAR
        return inst;
    }

    std::filesystem::path SpellSettingsDB::JsonPath() { return GetThisDllDir() / "IntegratedMagic_Spells.json"; }

    std::string SpellSettingsDB::MakeLegacyKey(std::uint32_t runtimeFormID) {
        return std::format("{:08X}", runtimeFormID);
    }

    std::string SpellSettingsDB::MakeStableKey(std::string_view pluginName, std::uint32_t localFormID) {
        if (pluginName.empty() || localFormID == 0u) {
            return {};
        }

        std::string plugin{pluginName};
        plugin = NormalizePluginName(std::move(plugin));

        return std::format("{}|{:08X}", plugin, localFormID);
    }

    std::string SpellSettingsDB::GetPluginName(const RE::TESForm* form) {
        if (!form) {
            return {};
        }

        const auto* file = form->GetFile(0);
        if (!file) {
            return {};
        }

        return file->fileName;
    }

    std::uint32_t SpellSettingsDB::GetLocalFormID(const RE::TESForm* form) {
        if (!form) {
            return 0u;
        }

        const auto fullID = form->GetFormID();
        const auto* file = form->GetFile(0);

        if (!file) {
            return fullID;
        }

        if (file->IsLight()) {
            return fullID & 0x00000FFFu;
        }

        return fullID & 0x00FFFFFFu;
    }

    std::string SpellSettingsDB::MakeStableKey(const RE::TESForm* form) {
        if (!form) {
            return {};
        }

        const auto plugin = GetPluginName(form);
        const auto localID = GetLocalFormID(form);

        return MakeStableKey(plugin, localID);
    }

    static const char* ModeToStr(ActivationMode m) {
        using enum ActivationMode;
        switch (m) {
            case Press:
                return "Press";
            case Automatic:
                return "Automatic";
            case Hold:
            default:
                return "Hold";
        }
    }

    static ActivationMode ModeFromStr(const std::string& s) {
        using enum ActivationMode;
        if (_stricmp(s.c_str(), "Press") == 0) {
            return Press;
        }
        if (_stricmp(s.c_str(), "Automatic") == 0) {
            return Automatic;
        }
        return Hold;
    }

    void SpellSettingsDB::Load() {
        const auto path = JsonPath();

        std::scoped_lock _{_mtx};
        _byKey.clear();
        _dirty = false;

        if (!std::filesystem::exists(path)) {
            return;
        }

        try {
            std::ifstream f(path);
            nlohmann::json j = nlohmann::json::parse(f);

            auto spells = j.value("spells", nlohmann::json::object());
            if (!spells.is_object()) {
                return;
            }

            for (auto it = spells.begin(); it != spells.end(); ++it) {
                std::string key = it.key();

                const auto& v = it.value();

                SpellSettings s{};
                s.mode = ModeFromStr(v.value("mode", "Hold"));
                s.autoAttack = v.value("autoAttack", true);

                _byKey.insert_or_assign(std::move(key), s);
            }
        } catch (const std::exception& e) {  // NOSONAR
            spdlog::error("[IMAGIC][SPELLCFG] Load failed: {}", e.what());
        }
    }

    void SpellSettingsDB::Save() const {
        const auto path = JsonPath();

        std::scoped_lock _{_mtx};

        try {
            std::filesystem::create_directories(path.parent_path());

            nlohmann::json spells = nlohmann::json::object();

            for (const auto& [key, s] : _byKey) {
                spells[key] = {{"mode", ModeToStr(s.mode)}, {"autoAttack", s.autoAttack}};
            }

            nlohmann::json j;
            j["version"] = 3;
            j["keyFormat"] = "plugin|localFormID";
            j["spells"] = std::move(spells);

            std::ofstream o(path);
            o << j.dump(2);
        } catch (const std::exception& e) {  // NOSONAR
            spdlog::error("[IMAGIC][SPELLCFG] Save failed: {}", e.what());
        }
    }

    SpellSettings SpellSettingsDB::GetOrCreate(std::uint32_t spellFormID, const RE::TESForm* form,
                                               const Config::ISlotAssignments* assignments) {
        std::scoped_lock _{_mtx};

        const std::string stableKey = MakeStableKey(form);
        const std::string legacyKey = MakeLegacyKey(spellFormID);

        if (!stableKey.empty()) {
            if (auto it = _byKey.find(stableKey); it != _byKey.end()) {
                return it->second;
            }
        }

        if (auto it = _byKey.find(legacyKey); it != _byKey.end()) {
            const auto settings = it->second;

            if (!stableKey.empty()) {
                _byKey.insert_or_assign(stableKey, settings);
                _byKey.erase(it);
                _dirty = true;

                MAGIC_DEBUG_LOG("[IMAGIC][SPELLCFG] Migrated legacy key {} -> {}", legacyKey, stableKey);
            }

            return settings;
        }

        SpellSettings s{};

        if (form && assignments) {
            const auto type = Adapters::DetectSpellType(form);
            const auto d = assignments->GetSpellDefaults(type);
            s.mode = d.mode;
            s.autoAttack = d.autoAttack;
        }

        const std::string& keyToUse = !stableKey.empty() ? stableKey : legacyKey;

        _byKey.try_emplace(keyToUse, s);
        _dirty = true;

        return s;
    }

    std::optional<SpellSettings> SpellSettingsDB::GetNoLock(std::uint32_t spellFormID) const {
        const auto* form = RE::TESForm::LookupByID(spellFormID);

        const std::string stableKey = MakeStableKey(form);
        const std::string legacyKey = MakeLegacyKey(spellFormID);

        if (!stableKey.empty()) {
            if (auto it = _byKey.find(stableKey); it != _byKey.end()) {
                return it->second;
            }
        }

        if (auto it = _byKey.find(legacyKey); it != _byKey.end()) {
            return it->second;
        }

        return std::nullopt;
    }

    std::optional<SpellSettings> SpellSettingsDB::Get(std::uint32_t spellFormID) const {
        std::scoped_lock _{_mtx};
        return GetNoLock(spellFormID);
    }

    void SpellSettingsDB::Set(std::uint32_t spellFormID, const SpellSettings& s) {
        std::scoped_lock _{_mtx};

        const auto* form = RE::TESForm::LookupByID(spellFormID);

        const std::string stableKey = MakeStableKey(form);
        const std::string legacyKey = MakeLegacyKey(spellFormID);
        const std::string& keyToUse = !stableKey.empty() ? stableKey : legacyKey;

        _byKey.insert_or_assign(keyToUse, s);

        if (!stableKey.empty()) {
            _byKey.erase(legacyKey);
        }

        _dirty = true;
    }

    bool SpellSettingsDB::IsDirty() const {
        std::scoped_lock _{_mtx};
        return _dirty;
    }

    void SpellSettingsDB::ClearDirty() {
        std::scoped_lock _{_mtx};
        _dirty = false;
    }
}