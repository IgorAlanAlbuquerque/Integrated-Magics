# P1 — Patch: Skip Channeling (cast imediato via hotkey)

## Contexto

Atualmente, quando um slot com magia é ativado via hotkey, o fluxo é:

1. `EquipSpellInHand` — magia equipada na mão → animação de levantar as mãos
2. `DispatchAttack(DOWN)` — input sintético "ataque pressionado"
3. `PumpCastPhase` aguarda `castIsStable` → `Casting`
4. Aguarda `IsChargeComplete` → `WaitingChargeRelease`
5. `DispatchAttack(UP)` — magia dispara
6. `OnCastStop` → restore do equipamento anterior

O efeito visual entre os passos 1–5 é o **channeling**: a magia aparece na mão com o brilho de carregamento antes de ser lançada. Para magias do tipo `Cast` (Fire-and-Forget, modo `Automatic`), esse efeito visual pode durar ~2–6 frames — suficiente para ser perceptível.

O patch **SkipChannelingPatch** elimina esse efeito ao chamar `RE::MagicCaster::CastSpellImmediate` diretamente, sem passar pelo ciclo equip → attack-dispatch → castStop.

---

## Escopo de aplicação

| Modo | Tipo | Aplica? | Razão |
|---|---|---|---|
| `Automatic` | `Cast` | ✅ Sim | Fire-and-forget; disparo único |
| `Automatic` | `Power` | ❌ Não | Powers têm seu próprio caminho (`ShoutState`) |
| `Hold` | `Concentration` | ❌ Não | Precisa do hold contínuo |
| `Press` | `Bound` | ❌ Não | Equip visual é intencional (invocação) |

Só se aplica quando **ambas** as condições são verdadeiras:
- `ss.mode == ActivationMode::Automatic`
- O `SpellType` do slot é `SpellType::Cast` (determinado no `PrepareSlotEntry`)

---

## API disponível

```cpp
// extern/CommonLibVR-ng/include/RE/M/MagicCaster.h, virtual 01
void RE::MagicCaster::CastSpellImmediate(
    RE::MagicItem*    a_spell,
    bool              a_noHitEffectArt,    // false = manter efeitos visuais do hit
    RE::TESObjectREFR* a_target,           // nullptr = sem alvo fixo
    float             a_effectiveness,    // 1.0f
    bool              a_hostileEffectivenessOnly, // false
    float             a_magnitudeOverride,        // -1.0f = sem override
    RE::Actor*        a_blameActor               // player
);

// Acesso ao caster de uma mão:
auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand);
//                                                               ou kRightHand
```

`CastSpellImmediate` não requer que a magia esteja equipada na mão — dispara direto a partir do `MagicCaster` da fonte especificada, usando a direção de look do jogador.

---

## Arquivos a modificar

| Arquivo | Alteração |
|---|---|
| `include/Config/Config.h` | Adicionar `bool skipChannelingPatch{false}` em `MagicConfig` |
| `include/Config/Ports/PatchSettings.h` | Adicionar `SkipChanneling()` em `IPatchSettings` |
| `src/Config/ConfigAdapter.cpp` | Implementar `SkipChanneling()` + load/save no INI |
| `include/Shared/SlotPressAction.h` | Adicionar `bool skipChanneling{false}` em `SlotPressAction` |
| `include/Domain/State.h` | Adicionar `bool immediateDispatch{false}` em `HandMode` |
| `src/Domain/MagicStateSlot.cpp` | `EnterHand`: se patch ativo e modo Automatic, setar `immediateDispatch` |
| `src/Domain/MagicStatePump.cpp` | `PumpCastPhase`: skip total quando `immediateDispatch` |
| `include/Adapters/Outbound/MagicEquip.h` | Declarar `CastHandImmediate` |
| `src/Adapters/Outbound/MagicEquip.cpp` | Implementar `CastHandImmediate` |
| `src/Application/SpellSystemController.cpp` | `DispatchSlotEvents`: chamar `CastHandImmediate` no lugar do DispatchAttack |

---

## Implementação

### 1. Config — novo flag

```cpp
// include/Config/Config.h
struct MagicConfig {
    bool skipEquipAnimationPatch{false};
    bool skipEquipAnimationOnReturnPatch{false};
    bool requireExclusiveHotkeyPatch{false};
    bool pressBothAtSamePatch{false};
    bool skipChannelingPatch{false};  // ← novo
};
```

```cpp
// include/Config/Ports/PatchSettings.h
class IPatchSettings {
    [[nodiscard]] virtual bool SkipEquipAnimation() const = 0;
    [[nodiscard]] virtual bool SkipEquipAnimationOnReturn() const = 0;
    [[nodiscard]] virtual bool RequireExclusiveHotkey() const = 0;
    [[nodiscard]] virtual bool PressBothAtSame() const = 0;
    [[nodiscard]] virtual bool SkipChanneling() const = 0;  // ← novo
};
```

```cpp
// src/Config/ConfigAdapter.cpp
// Load:
skipChannelingPatch = _getBool(ini, "Patches", "SkipChannelingPatch", false);
// Save:
ini.SetBoolValue("Patches", "SkipChannelingPatch", skipChannelingPatch);
// Implementação:
bool MagicConfigAdapter::SkipChanneling() const { return Cfg().skipChannelingPatch; }
```

---

### 2. `HandMode` — flag de disparo imediato

```cpp
// include/Domain/State.h, struct HandMode
bool immediateDispatch{false};  // set by EnterHand when skipChannelingPatch active
```

---

### 3. `SlotPressAction` — propagar decisão

```cpp
// include/Shared/SlotPressAction.h
struct SlotPressAction {
    // ... campos existentes ...
    bool skipChanneling{false};  // ← novo: indica que as mãos com Automatic devem usar CastImmediate
};
```

---

### 4. `EnterHand` — desviar para imediato

Em `src/Domain/MagicStateSlot.cpp`, dentro do `case Automatic:` de `EnterHand`, adicionar a verificação:

```cpp
case Automatic:
    hm.autoActive = true;
    if (Config::MagicConfigAdapter::Get().SkipChanneling()) {
        // Bypass do ciclo ataque → castStop; SpellSystemController chamará CastHandImmediate
        hm.immediateDispatch = true;
        hm.wantAutoAttack = false;
        hm.autoCastPhase = AutoCastPhase::Idle;  // nada para o PumpCastPhase fazer
        MAGIC_DEBUG_LOG("[State] EnterHand: hand={} mode=Automatic IMMEDIATE", handStr);
    } else {
        hm.wantAutoAttack = true;
        hm.autoCastPhase = AutoCastPhase::StartRequested;
        MAGIC_DEBUG_LOG("[State] EnterHand: hand={} mode=Automatic", handStr);
    }
    break;
```

Em `OnSlotPressed` (que já lê `action.skipAnim`), propagar o flag:

```cpp
// Em algum ponto de OnSlotPressed ou PrepareSlotEntry antes de retornar a action:
action.skipChanneling = Config::MagicConfigAdapter::Get().SkipChanneling();
```

---

### 5. `PumpCastPhase` — skip para mãos imediatas

No início de `PumpCastPhase` (após o guard `!_session.active`), adicionar:

```cpp
if (hm.immediateDispatch) {
    // Aguarda SpellSystemController chamar CastHandImmediate e finalizar a mão
    return result;
}
```

Isso evita que o pump tente avançar o `AutoCastPhase` para uma mão que vai ser resolvida externamente.

---

### 6. Novo adaptador outbound: `CastHandImmediate`

```cpp
// include/Adapters/Outbound/MagicEquip.h
void CastHandImmediate(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand);
```

```cpp
// src/Adapters/Outbound/MagicEquip.cpp
void CastHandImmediate(RE::PlayerCharacter* player, RE::SpellItem* spell, Hand hand) {
    if (!player || !spell) return;
    const auto src = (hand == Hand::Left) ? RE::MagicSystem::CastingSource::kLeftHand
                                          : RE::MagicSystem::CastingSource::kRightHand;
    auto* caster = player->GetMagicCaster(src);
    if (!caster) return;
    MAGIC_DEBUG_LOG("[Action] CastHandImmediate: hand={} spellID={:#010x} name='{}'",
                    (hand == Hand::Left) ? "Left" : "Right",
                    spell->GetFormID(),
                    spell->GetFullName() ? spell->GetFullName() : "<null>");
    caster->CastSpellImmediate(spell, false, nullptr, 1.0f, false, -1.0f, player);
}
```

---

### 7. `SpellSystemController::DispatchSlotEvents` — substituir DispatchAttack

Na seção que processa `action.spellsToEquip` e chama `OnEquipComplete()`, adicionar o branch de cast imediato:

```cpp
if (!action.spellsToEquip.empty()) {
    const auto eqResult = state.OnEquipComplete();
    MAGIC_DEBUG_LOG("[SpellSystem] DispatchSlotEvents: OnEquipComplete → dispatchL={} dispatchR={}",
                    eqResult.dispatchLeft, eqResult.dispatchRight);

    const bool skipCh = action.skipChanneling;
    const bool leftImmediate  = skipCh && _left.immediateDispatch;   // verificar via state getter
    const bool rightImmediate = skipCh && _right.immediateDispatch;  // idem

    if (leftImmediate || rightImmediate) {
        // Encontrar o spell para cada mão no action.spellsToEquip
        for (const auto& intent : action.spellsToEquip) {
            const bool isImm = (intent.hand == Hand::Left ? leftImmediate : rightImmediate);
            if (isImm) {
                IntegratedMagic::MagicAction::CastHandImmediate(player, intent.spell, intent.hand);
                // Marcar mão como concluída no state machine
                state.OnImmediateCastComplete(intent.hand);
            }
        }
        // Processar resultado do finalize (pode gerar restorePlan)
        // similar ao if (action.finalizeAfterController)
    }

    if (!leftImmediate && eqResult.dispatchLeft) {
        MAGIC_DEBUG_LOG("[SpellSystem] DispatchSlotEvents: → DispatchAttack Left 1.0 (equip)");
        IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Left, 1.0f, 0.0f);
    }
    if (!rightImmediate && eqResult.dispatchRight) {
        MAGIC_DEBUG_LOG("[SpellSystem] DispatchSlotEvents: → DispatchAttack Right 1.0 (equip)");
        IntegratedMagic::detail::DispatchAttack(IntegratedMagic::Hand::Right, 1.0f, 0.0f);
    }
}
```

> **Nota de implementação**: `SpellSystemController` não tem acesso direto a `_left/_right` de `MagicState`.
> Adicionar um getter público em `MagicState`:
> ```cpp
> bool IsImmediateDispatch(Hand hand) const;
> ```

---

### 8. `MagicState::OnImmediateCastComplete` — novo método público

```cpp
// include/Domain/State.h — declaração
StateExitResult OnImmediateCastComplete(Hand hand);

// src/Domain/MagicStateSlot.cpp (ou MagicStatePump.cpp) — implementação
StateExitResult MagicState::OnImmediateCastComplete(Hand hand) {
    auto& hm = ModeFor(hand);
    if (!hm.immediateDispatch) return {};
    hm.immediateDispatch = false;
    hm.autoActive = false;
    hm.autoCastPhase = AutoCastPhase::Done;
    hm.finished = true;
    MAGIC_DEBUG_LOG("[State] OnImmediateCastComplete: hand={}", IsLeft(hand) ? "Left" : "Right");
    return TryFinalizeExit();
}
```

`TryFinalizeExit` verifica se `allFinished` e agenda o restore. O resultado é processado por `SpellSystemController` da mesma forma que `autoResult.restorePlan`.

---

## Comportamento esperado

Com `SkipChannelingPatch = true`:

1. Hotkey ativada → spell equipada na mão (sem levantar mãos se `wasHandsDown=false`)
2. `CastHandImmediate` chamado imediatamente → spell dispara sem aparecer na mão
3. `OnImmediateCastComplete` → `TryFinalizeExit` → restore agendado
4. Equipamento anterior restaurado como de costume

Visualmente: nenhuma animação de carregamento. A magia simplesmente dispara ao apertar o hotkey.

---

## Considerações importantes

### castStopsToSkip
Ao usar `CastSpellImmediate`, o ciclo `attack DOWN → castIsStable → attack UP → castStop` não ocorre. Logo, o contador `castStopsToSkip` (usado para ignorar `castStop` espúrios do skipEquipAnimation mod) **não deve ser incrementado** para mãos em modo imediato. Verificar se `EnterHand` ou `castStopsToSkip` precisa ser guardado contra o path imediato.

### `dirtyLeft / dirtyRight`
A magia ainda é equipada via `EquipSpellInHand` antes do cast (para fins de direção e restore). Portanto `dirtyLeft/Right` são setados normalmente e o restore acontece. Se desejado, o equip pode ser suprimido para spells com `delivery == kSelf` (magias self-targeted) já que a direção não importa.

### Dual cast
Se ambas as mãos têm uma spell Cast em modo Automatic, ambas devem receber `immediateDispatch = true`. `OnImmediateCastComplete` para a segunda mão dispara `TryFinalizeExit` com `allFinished = true`.

### `noHitEffectArt = false`
Passar `false` preserva os efeitos visuais e sonoros do projétil/impacto. Apenas o channeling de carregamento é suprimido.

### Teste de `CastSpellImmediate` com spell não equipada
Verificar se `CastSpellImmediate` funciona corretamente sem a spell já na mão do caster. Caso contrário, manter o `EquipSpellInHand` antes de chamar o immediate cast (comportamento já descrito acima).

---

## Teste

1. Ativar `SkipChannelingPatch = true` no INI
2. Atribuir uma magia Fire-and-Forget (ex: `Fireball`, `Ice Spike`) a um slot
3. Pressionar o hotkey → magia deve disparar instantaneamente sem aparecer na mão
4. Verificar no log:
   - `[Action] CastHandImmediate: hand=Right spellID=...`
   - `[State] OnImmediateCastComplete: hand=Right`
   - Restore do equipamento anterior ocorrendo normalmente
5. Verificar que com patch desativado o comportamento anterior é preservado
6. Testar slot com dual spell (left + right) → ambas disparam imediatamente
7. Testar slot com Concentration spell → patch **não** deve se aplicar (channeling normal)