#include "Config/Notifications.h"

#include <atomic>

namespace {
    std::atomic<bool> g_configSaved{false};
}

void IntegratedMagic::Config::NotifyConfigSaved() {
    g_configSaved.store(true, std::memory_order_relaxed);
}

bool IntegratedMagic::Config::ConsumeConfigSaved() {
    return g_configSaved.exchange(false, std::memory_order_relaxed);
}
