#include "Adapters/Inbound/EquipEventAdapter.h"
#include "Adapters/Inbound/GameEventAdapter.h"
#include "Application/InputController.h"
#include "Config/ConfigAdapter.h"
#include "Config/StyleConfig.h"
#include "Hooks.h"
#include "PCH.h"
#include "Persistence/SaveSpellDB.h"
#include "Persistence/SpellSettingsDB.h"
#include "UI/Menu.h"
#include "UI/Strings.h"
#include "UI/TextureManager.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

namespace {
    static std::string g_pendingEssPath;
    static std::string g_currentEssPath;
    static bool g_dbLoaded = false;

    void EnsureSaveSpellDBLoaded() {
        if (!g_dbLoaded) {
            IntegratedMagic::SaveSpellDB::Get().LoadFromDisk();
            g_dbLoaded = true;
        }
    }

    IntegratedMagic::SaveSpellSlots ReadSlotsFromConfig() {
        auto& adapter = IntegratedMagic::Config::MagicConfigAdapter::Get();
        IntegratedMagic::SaveSpellSlots s{};
        adapter.ReadAllSlots(s.left, s.right, s.shout);
        return s;
    }

    void ApplySlotsToConfig(const IntegratedMagic::SaveSpellSlots& s) {
        IntegratedMagic::Config::MagicConfigAdapter::Get().ApplyAllSlots(s.left, s.right, s.shout);
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
            return *reinterpret_cast<const bool*>(message->data);
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

                MAGIC_DEBUG_LOG("[SaveLoad] kPreLoadGame: raw key='{}'", g_pendingEssPath);

                break;
            }
            case SKSE::MessagingInterface::kDataLoaded: {
                IntegratedMagic::Strings::Load();
                IntegratedMagic::GetMagicConfig().Load();
                IntegratedMagic::SpellSettingsDB::Get().Load();
                IntegratedMagic::MENU::Register();
                Application::InputController::Get().OnConfigChanged();

                CastGuardEvents::Get().Register();
                IntegratedMagic::EquipSink::RegisterEquipListener();
                break;
            }
            case SKSE::MessagingInterface::kPostLoadGame: {
                const bool ok = ReadPostLoadOk(message);

                MAGIC_DEBUG_LOG("[SaveLoad] kPostLoadGame: ok={} pendingEssPath='{}'", ok, g_pendingEssPath);

                if (ok && !g_pendingEssPath.empty()) {
                    EnsureSaveSpellDBLoaded();
                    g_currentEssPath = g_pendingEssPath;
                    IntegratedMagic::SaveSpellSlots slots{};
                    const bool found = IntegratedMagic::SaveSpellDB::Get().TryGet(g_currentEssPath, slots);

                    MAGIC_DEBUG_LOG("[SaveLoad] TryGet key='{}' found={}", g_currentEssPath, found);

                    if (found) {
                        MAGIC_DEBUG_LOG("[SaveLoad] slots size: left={} right={} shout={}", slots.left.size(),
                                        slots.right.size(), slots.shout.size());
                        for (std::size_t i = 0; i < slots.left.size(); ++i)
                            MAGIC_DEBUG_LOG("[SaveLoad]   slot[{}] left={:#010x} right={:#010x} shout={:#010x}", i,
                                            slots.left[i], slots.right[i],
                                            i < slots.shout.size() ? slots.shout[i] : 0u);

                        ApplySlotsToConfig(slots);
                    } else {
                        MAGIC_DEBUG_LOG("[SaveLoad] key not found in DB, clearing slots");

                        ApplySlotsToConfig(IntegratedMagic::SaveSpellSlots{});
                    }
                }

                else {
                    MAGIC_DEBUG_LOG("[SaveLoad] kPostLoadGame: skipped (ok={} pendingEmpty={})", ok,
                                    g_pendingEssPath.empty());
                }

                g_pendingEssPath.clear();
                break;
            }
            case SKSE::MessagingInterface::kSaveGame: {
                std::string key = GetSaveKeyFromMsg(message);

                MAGIC_DEBUG_LOG("[SaveLoad] kSaveGame: raw key='{}'", key);

                if (key.empty()) key = g_currentEssPath;

                MAGIC_DEBUG_LOG("[SaveLoad] kSaveGame: final key='{}'", key);

                if (!key.empty()) {
                    EnsureSaveSpellDBLoaded();
                    const auto slots = ReadSlotsFromConfig();

                    MAGIC_DEBUG_LOG("[SaveLoad] saving slots size: left={} right={} shout={}", slots.left.size(),
                                    slots.right.size(), slots.shout.size());

                    IntegratedMagic::SaveSpellDB::Get().Upsert(key, slots);
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
    IntegratedMagic::StyleConfig::Get().Load();
    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(GlobalMessageHandler);
    }
    IntegratedMagic::Hooks::Install_Hooks();
    return true;
}