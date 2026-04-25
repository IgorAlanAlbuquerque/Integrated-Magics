#include "Adapters/Inbound/AnimEventAdapter.h"

#include "Application/SpellSystemController.h"
#include "PCH.h"
#include "Shared/Hand.h"

void AnimListener::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev) {
    if (!ev || !ev->holder) return;
    if (auto* actor = ev->holder->As<RE::Actor>(); actor != RE::PlayerCharacter::GetSingleton()) return;
    const std::string_view tag{ev->tag.c_str(), ev->tag.size()};
    Application::SpellSystemController::Get().NotifyAnimEvent(tag);
}