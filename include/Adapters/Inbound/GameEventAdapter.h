#pragma once
#include "Application/SpellSystemController.h"
#include "PCH.h"

class CastGuardEvents : public RE::BSTEventSink<RE::TESDeathEvent>,
                        public RE::BSTEventSink<RE::TESLoadGameEvent>,
                        public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    static CastGuardEvents& Get() {
        static CastGuardEvents instance;  // NOSONAR
        return instance;
    }

    void Register() {
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) return;
        holder->AddEventSink<RE::TESDeathEvent>(this);
        holder->AddEventSink<RE::TESLoadGameEvent>(this);
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
    }

protected:
    RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* ev,
                                          RE::BSTEventSource<RE::TESDeathEvent>*) override {
        if (auto const* pc = RE::PlayerCharacter::GetSingleton(); ev && ev->actorDying && ev->actorDying.get() == pc)
            Application::SpellSystemController::Get().NotifyPlayerDeath();
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*,
                                          RE::BSTEventSource<RE::TESLoadGameEvent>*) override {
        Application::SpellSystemController::Get().NotifyLoadGame();
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* ev,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        if (ev && ev->opening) Application::SpellSystemController::Get().NotifyMenuOpen(ev->menuName.c_str());
        return RE::BSEventNotifyControl::kContinue;
    }
};