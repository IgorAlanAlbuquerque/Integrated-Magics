#pragma once

#include "PCH.h"

namespace IntegratedMagic::EquipSink {

    [[nodiscard]] RE::FormID GetLastEquippedMagicFormID();

    void ClearLastEquippedMagic();

    void RegisterEquipListener();
}