#include "AnimEventAdapter.h"

#include "Application/SpellSystemController.h"
#include "Domain/Hand.h"
#include "PCH.h"

void AnimListener::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev) {
    if (!ev || !ev->holder) return;
    if (auto* actor = ev->holder->As<RE::Actor>(); actor != RE::PlayerCharacter::GetSingleton()) return;
    const std::string_view tag{ev->tag.c_str(), ev->tag.size()};
    Application::SpellSystemController::Get().NotifyAnimEvent(tag);
}