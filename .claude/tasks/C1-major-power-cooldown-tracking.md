# C1 — Cooldown tracking para Major Powers (kPower)

## Contexto

O `SlotCooldownTracker` já rastreia o cooldown de shouts via `player->GetVoiceRecoveryTime()`.
Major Powers (SpellType::kPower) usam o **mesmo voiceTimer global** — quando um major power é
usado, o jogo seta o timer para ~4320 segundos reais (86400s de jogo / timescale padrão 20 ≈
72 min reais). O tracker atual ignora completamente poderes porque
`FindSlotForCurrentlyEquippedShout()` só olha `selectedPower->As<RE::TESShout>()`.

## Por que não existe `SpellItem::recoveryTime`

`SpellItem::Data` (SPIT record) **não tem campo de recovery time**:
```
costOverride | flags | spellType | chargeTime | castingType | delivery | castDuration | range | castingPerk
```
O `chargeTime` refere-se ao charge-up de spells de concentração — não ao cooldown de poderes.
Para powers, o cooldown total só pode ser obtido capturando `GetVoiceRecoveryTime()` no exato
momento em que ele transita de 0 → não-zero (`justStarted`), pois é quando o jogo acabou de
setá-lo. Esse valor capturado é usado como `totalCooldown`.

## Mecanismo de detecção

O `SlotCooldownTracker::Update()` detecta a transição:
```cpp
bool justStarted = !wasCoolingDown && isCoolingDown;  // voiceTimer 0 → > 0
```
Nesse momento, `player->GetActorRuntimeData().selectedPower` ainda é o poder usado (o restore
via `kPowerRestoreDelaySec = 0.05f` só ocorre 50ms depois, e o Update roda no present hook,
ou seja, dentro de 1 frame ≈ 16-33ms após o cast).

## Arquivos a modificar

| Arquivo | Alteração |
|---|---|
| `include/Domain/SlotCooldownTracker.h` | Adicionar `bool isPower` em `SlotState` e `SlotCooldownInfo` |
| `src/Domain/SlotCooldownTracker.cpp` | Refatorar `FindSlotForCurrentlyEquippedShout` para também detectar `SpellItem` powers |

## Implementação

### 1. `SlotCooldownInfo` e `SlotState` — adicionar `isPower`

```cpp
// include/Domain/SlotCooldownTracker.h
struct SlotCooldownInfo {
    bool onCooldown{false};
    float progress{1.0f};
    bool justFinished{false};
    RE::FormID formID{0};
    int variationIndex{-1};
    float totalCooldown{0.0f};
    float remainingCooldown{0.0f};
    bool isPower{false};   // ← novo: true para kPower/kLesserPower
};
```

```cpp
// SlotState (privado)
struct SlotState {
    RE::FormID trackedFormID{0};
    int variationIndex{-1};
    float totalCooldown{0.0f};
    float remainingCooldown{0.0f};
    bool onCooldown{false};
    bool justFinished{false};
    bool isPower{false};   // ← novo
};
```

### 2. Refatorar `FindSlotForCurrentlyEquippedShout`

Renomear para `FindEquippedVoiceSlot` e retornar tanto o slot index quanto se é power:

```cpp
struct VoiceSlotResult {
    int slot{-1};
    RE::FormID formID{0};
    bool isPower{false};
};

VoiceSlotResult FindEquippedVoiceSlot() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return {};

    RE::FormID equippedID = 0;
    bool isPower = false;

    const auto& rd = player->GetActorRuntimeData();
    if (auto* selected = rd.selectedPower; selected) {
        if (auto* shout = selected->As<RE::TESShout>()) {
            equippedID = shout->GetFormID();
            isPower = false;
        } else if (auto* power = selected->As<RE::SpellItem>()) {
            using ST = RE::MagicSystem::SpellType;
            const auto t = power->GetSpellType();
            if (t == ST::kPower || t == ST::kLesserPower) {
                equippedID = power->GetFormID();
                isPower = true;
            }
        }
    }

    if (!equippedID) return {};

    const int slotCount = static_cast<int>(Slots::GetSlotCount());
    for (int i = 0; i < slotCount; ++i) {
        if (Slots::GetSlotShout(i) == equippedID)
            return {i, equippedID, isPower};
    }

    return {};
}
```

### 3. Lógica `justStarted` — path de power vs shout

```cpp
if (justStarted) {
    const auto found = FindEquippedVoiceSlot();
    if (found.slot >= 0 && found.slot < kMaxTrackedSlots) {
        auto& st = _slots[found.slot];
        st.trackedFormID = found.formID;
        st.isPower = found.isPower;
        st.onCooldown = true;
        st.justFinished = false;

        if (found.isPower) {
            // Para powers: total = valor atual (game acabou de setar o timer)
            st.variationIndex = -1;
            st.totalCooldown = currentRemaining;
            st.remainingCooldown = currentRemaining;
            MAGIC_DEBUG_LOG("[Cooldown] power start: slot={} formID={:#010x} total={:.1f}s",
                            found.slot, found.formID, currentRemaining);
        } else {
            // Shout: inferir variação pelo recoveryTime (comportamento existente)
            const auto used = InferUsedVariation(found.formID, currentRemaining);
            st.variationIndex = used.index;
            st.totalCooldown = used.totalCooldown;
            st.remainingCooldown = currentRemaining;
            MAGIC_DEBUG_LOG("[Cooldown] shout start: slot={} formID={:#010x} remaining={:.3f} "
                            "variation={} total={:.3f}",
                            found.slot, found.formID, currentRemaining,
                            used.index, used.totalCooldown);
        }
    }
}
```

### 4. GetSlotInfo — propagar `isPower`

```cpp
SlotCooldownInfo SlotCooldownTracker::GetSlotInfo(int slot) const {
    ...
    out.isPower = st.isPower;
    ...
}
```

## Comportamento esperado

- Major power usado → voiceTimer setado para ~4320s reais (72min ao timescale 20)
- Tracker detecta `justStarted`, identifica SpellItem, captura 4320s como `totalCooldown`
- Arc de cooldown no HUD começa em 0% e avança conforme o tempo passa
- Ao fim dos 72min (ou menos com Blessing of Talos etc.), arc chega a 100% → `justFinished`
- O campo `isPower` em `SlotCooldownInfo` permite ao HUD usar cor/estilo diferente se desejado

## Considerações de timescale

`GetVoiceRecoveryTime()` retorna **segundos reais**, não segundos de jogo. Se o timescale for
alterado enquanto o poder está em cooldown, o valor retornado reflete a escala atual. O
`totalCooldown` capturado no `justStarted` ficará levemente errado se o timescale mudar.
Para o HUD isso é aceitável — o progress fica visualmente consistente com o tempo real restante.

## Teste

1. Equipar um major power no slot de voice de um slot
2. Usar o poder
3. Verificar no log `[Cooldown] power start:` com `total` ≈ 4320s (default timescale)
4. Verificar arc de cooldown no HUD decrementando
5. Usar "Blessing of Talos" (shoutRecoveryMult) e verificar que o total capturado é menor
6. Aguardar cooldown zerar e verificar `justFinishedCooldown` no HUD