#include "MagicConfig.h"
#include "MagicHooks.h"
#include "MagicInput.h"
#include "MagicStrings.h"
#include "PCH.h"
#include "SaveSpellDB.h"
#include "SpellSettingsDB.h"
#include "UI_IntegratedMagic.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

namespace {
    static std::string g_pendingEssPath;  // NOSONAR
    static std::string g_currentEssPath;  // NOSONAR
    static bool g_dbLoaded = false;       // NOSONAR

    void EnsureSaveSpellDBLoaded() {
        if (!g_dbLoaded) {
            IntegratedMagic::SaveSpellDB::Get().LoadFromDisk();
            g_dbLoaded = true;
        }
    }

    IntegratedMagic::SaveSpellSlots ReadSlotsFromConfig() {
        auto const& cfg = IntegratedMagic::GetMagicConfig();

        IntegratedMagic::SaveSpellSlots s{};

        s.slotSpellFormID[0] = cfg.slotSpellFormID1.load(std::memory_order_relaxed);
        s.slotSpellFormID[1] = cfg.slotSpellFormID2.load(std::memory_order_relaxed);
        s.slotSpellFormID[2] = cfg.slotSpellFormID3.load(std::memory_order_relaxed);
        s.slotSpellFormID[3] = cfg.slotSpellFormID4.load(std::memory_order_relaxed);
        return s;
    }

    void ApplySlotsToConfig(const IntegratedMagic::SaveSpellSlots& s) {
        auto& cfg = IntegratedMagic::GetMagicConfig();

        cfg.slotSpellFormID1.store(s.slotSpellFormID[0], std::memory_order_relaxed);
        cfg.slotSpellFormID2.store(s.slotSpellFormID[1], std::memory_order_relaxed);
        cfg.slotSpellFormID3.store(s.slotSpellFormID[2], std::memory_order_relaxed);
        cfg.slotSpellFormID4.store(s.slotSpellFormID[3], std::memory_order_relaxed);
    }

    std::string ExtractKey(std::string s) {
        if (auto pos = s.find_last_of("\\/"); pos != std::string::npos) s = s.substr(pos + 1);

        if (s.size() >= 4) {
            auto tail = s.substr(s.size() - 4);
            for (auto& c : tail) c = (char)std::tolower((unsigned char)c);
            if (tail == ".ess") s.resize(s.size() - 4);
        }

        return s;
    }

    std::string GetSaveKeyFromMsg(const SKSE::MessagingInterface::Message* msg) {
        if (!msg || !msg->data || msg->dataLen <= 0) return {};

        auto* p = reinterpret_cast<const char*>(msg->data);

        std::size_t n = 0;
        while (n < (std::size_t)msg->dataLen && p[n] != '\0') ++n;

        std::string raw(p, n);
        std::string key = ExtractKey(std::move(raw));
        return IntegratedMagic::SaveSpellDB::NormalizeKey(std::move(key));
    }

    bool ReadPostLoadOk(const SKSE::MessagingInterface::Message* message) {
        if (!message) {
            return true;
        }

        if (const auto raw = reinterpret_cast<std::uintptr_t>(message->data); raw == 0u || raw == 1u) {
            return raw != 0u;
        }

        if (message->data && message->dataLen == sizeof(bool)) {
            return *reinterpret_cast<const bool*>(message->data);  // NOSONAR
        }

        return message->data != nullptr;
    }

    void InitializeLogger() {
        if (auto path = SKSE::log::log_directory()) {
            *path /= "IntegratedMagic.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto logger = std::make_shared<spdlog::logger>("global", sink);
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("Logger iniciado.");
        }
    }

    void GlobalMessageHandler(SKSE::MessagingInterface::Message* message) {
        if (!message) return;

        switch (message->type) {
            case SKSE::MessagingInterface::kPreLoadGame: {
                g_pendingEssPath = GetSaveKeyFromMsg(message);
                break;
            }
            case SKSE::MessagingInterface::kInputLoaded: {
                MagicInput::RegisterInputHandler();
                break;
            }
            case SKSE::MessagingInterface::kDataLoaded: {
                IntegratedMagic::Strings::Load();
                IntegratedMagic::GetMagicConfig().Load();
                IntegratedMagic::SpellSettingsDB::Get().Load();
                IntegratedMagic_UI::Register();
                MagicInput::OnConfigChanged();
                IntegratedMagic::Hooks::Install_Hooks();
                break;
            }
            case SKSE::MessagingInterface::kPostLoadGame: {
                if (const bool ok = ReadPostLoadOk(message); ok && !g_pendingEssPath.empty()) {
                    EnsureSaveSpellDBLoaded();
                    g_currentEssPath = g_pendingEssPath;

                    IntegratedMagic::SaveSpellSlots slots{};
                    if (IntegratedMagic::SaveSpellDB::Get().TryGet(g_currentEssPath, slots)) {
                        ApplySlotsToConfig(slots);
                    } else {
                        ApplySlotsToConfig(IntegratedMagic::SaveSpellSlots{});
                    }
                }

                g_pendingEssPath.clear();
                break;
            }
            case SKSE::MessagingInterface::kSaveGame: {
                std::string key = GetSaveKeyFromMsg(message);
                if (key.empty()) {
                    key = g_currentEssPath;
                }

                if (!key.empty()) {
                    EnsureSaveSpellDBLoaded();
                    IntegratedMagic::SaveSpellDB::Get().Upsert(key, ReadSlotsFromConfig());
                    IntegratedMagic::SaveSpellDB::Get().SaveToDisk();
                }
                break;
            }

            case SKSE::MessagingInterface::kDeleteGame: {
                std::string key = GetSaveKeyFromMsg(message);
                if (key.empty()) {
                    key = g_currentEssPath;
                }
                if (!key.empty()) {
                    EnsureSaveSpellDBLoaded();
                    IntegratedMagic::SaveSpellDB::Get().Erase(key);
                    IntegratedMagic::SaveSpellDB::Get().SaveToDisk();
                    if (key == g_currentEssPath) {
                        g_currentEssPath.clear();
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(GlobalMessageHandler);
    }

    return true;
}