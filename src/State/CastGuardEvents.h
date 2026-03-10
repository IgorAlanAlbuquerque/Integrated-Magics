#pragma once
#include "State.h"
#include "PCH.h"

class CastGuardEvents :
    public RE::BSTEventSink<RE::TESDeathEvent>,
    public RE::BSTEventSink<RE::TESLoadGameEvent>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    static CastGuardEvents& Get() {
        static CastGuardEvents instance;
        return instance;
    }

    void Register() {
        auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!holder) return;
        holder->AddEventSink<RE::TESDeathEvent>(this);
        holder->AddEventSink<RE::TESLoadGameEvent>(this);

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
        }
    }

protected:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESDeathEvent* ev,
        RE::BSTEventSource<RE::TESDeathEvent>*) override
    {
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (ev && ev->actorDying && ev->actorDying.get() == pc) {
            IntegratedMagic::MagicState::Get().ForceExit();
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESLoadGameEvent*,
        RE::BSTEventSource<RE::TESLoadGameEvent>*) override
    {
        IntegratedMagic::MagicState::Get().ForceExit();
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* ev,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        static constexpr std::array kInterruptMenus = {
            "ContainerMenu"sv, "InventoryMenu"sv,
            "MagicMenu"sv,     "MapMenu"sv,
            "Journal Menu"sv,  "Dialogue Menu"sv,
        };
        if (ev && ev->opening) {
            for (auto m : kInterruptMenus) {
                if (ev->menuName == m) {
                    IntegratedMagic::MagicState::Get().ForceExit();
                    break;
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};