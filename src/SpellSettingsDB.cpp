#include "SpellSettingsDB.h"

#include <array>
#include <cstdio>
#include <format>
#include <iomanip>
#include <nlohmann/json.hpp>

#include "PCH.h"

namespace {
    std::string_view MakeKeyView(std::uint32_t id, std::array<char, 9>& buf) {
        std::format_to_n(buf.begin(), 8, "{:08X}", id);
        buf[8] = '\0';
        return std::string_view{buf.data(), 8};
    }
}

namespace IntegratedMagic {
    SpellSettingsDB& SpellSettingsDB::Get() {
        static SpellSettingsDB inst;  // NOSONAR
        return inst;
    }

    std::filesystem::path SpellSettingsDB::JsonPath() { return GetThisDllDir() / "IntegratedMagic_Spells.json"; }

    std::string SpellSettingsDB::MakeKey(std::uint32_t spellFormID) { return std::format("{:08X}", spellFormID); }

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
        if (_stricmp(s.c_str(), "Press") == 0) return Press;
        if (_stricmp(s.c_str(), "Automatic") == 0) return Automatic;
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
            if (!spells.is_object()) return;
            for (auto it = spells.begin(); it != spells.end(); ++it) {
                const auto& key = it.key();
                const auto& v = it.value();
                SpellSettings s{};
                s.mode = ModeFromStr(v.value("mode", "Hold"));
                s.autoAttack = v.value("autoAttack", true);
                _byKey.insert_or_assign(key, s);
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
            j["version"] = 2;
            j["spells"] = std::move(spells);
            std::ofstream o(path);
            o << j.dump(2);
        } catch (const std::exception& e) {  // NOSONAR
            spdlog::error("[IMAGIC][SPELLCFG] Save failed: {}", e.what());
        }
    }

    SpellSettings SpellSettingsDB::GetOrCreate(std::uint32_t spellFormID) {
        std::scoped_lock _{_mtx};
        std::array<char, 9> buf{};
        const std::string_view keysv = MakeKeyView(spellFormID, buf);
        if (auto it = _byKey.find(keysv); it != _byKey.end()) {
            return it->second;
        }
        SpellSettings s{};
        _byKey.try_emplace(std::string(keysv), s);
        _dirty = true;
        return s;
    }

    void SpellSettingsDB::Set(std::uint32_t spellFormID, const SpellSettings& s) {
        std::scoped_lock _{_mtx};
        std::array<char, 9> buf{};
        const std::string_view keysv = MakeKeyView(spellFormID, buf);
        if (auto it = _byKey.find(keysv); it != _byKey.end()) {
            it->second = s;
        } else {
            _byKey.try_emplace(std::string(keysv), s);
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