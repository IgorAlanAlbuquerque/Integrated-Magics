#include "MagicConfig.h"
#include "MagicHooks.h"
#include "MagicInput.h"
#include "MagicStrings.h"
#include "PCH.h"
#include "SpellSettingsDB.h"
#include "UI_IntegratedMagic.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

namespace {
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
            case SKSE::MessagingInterface::kInputLoaded:
                MagicInput::RegisterInputHandler();
                break;
            case SKSE::MessagingInterface::kDataLoaded:
                IntegratedMagic::Strings::Load();
                IntegratedMagic::GetMagicConfig().Load();
                IntegratedMagic::SpellSettingsDB::Get().Load();
                IntegratedMagic_UI::Register();
                MagicInput::OnConfigChanged();
                IntegratedMagic::Hooks::Install_Hooks();
                break;
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