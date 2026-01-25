#include "MagicStrings.h"

#include <fstream>
#include <unordered_map>

#include "MagicConfigPath.h"
#include "PCH.h"

namespace IntegratedMagic::Strings {
    namespace {
        std::unordered_map<std::string, std::string> g_strings;  // NOSONAR
        bool g_loaded = false;                                   // NOSONAR

        std::filesystem::path StringsPath() { return GetThisDllDir() / "IntegratedMagic_Strings.txt"; }

        void _ensureLoaded() {
            if (g_loaded) return;

            g_loaded = true;
            g_strings.clear();

            auto path = StringsPath();
            std::ifstream in(path);
            if (!in.is_open()) {
                return;
            }

            auto trim = [](std::string& s) {
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                std::size_t i = 0;
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                if (i > 0) s.erase(0, i);
            };

            std::string line;
            while (std::getline(in, line)) {
                if (auto posComment = line.find('#'); posComment != std::string::npos) {
                    line.erase(posComment);
                }

                trim(line);
                if (line.empty()) continue;

                auto posEq = line.find('=');
                if (posEq == std::string::npos) continue;

                std::string key = line.substr(0, posEq);
                std::string value = line.substr(posEq + 1);
                trim(key);
                trim(value);

                if (!key.empty()) {
                    g_strings[std::move(key)] = std::move(value);
                }
            }
        }
    }

    void Load() {
        g_loaded = false;
        _ensureLoaded();
    }

    std::string Get(std::string_view key, std::string_view fallback) {
        _ensureLoaded();
        if (auto it = g_strings.find(std::string{key}); it != g_strings.end()) return it->second;
        return std::string{fallback};
    }
}