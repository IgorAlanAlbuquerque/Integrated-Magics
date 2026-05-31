# R5 — Substituir diff de inventário `UpdatePrevExtraEquippedForOverlay`

## Problema

`UpdatePrevExtraEquippedForOverlay` captura um snapshot do inventário antes e depois de cada operação de equip para detectar itens que o jogo desaparou como side effect (`ActorEquipManager` às vezes desequipa itens "overlay" ao equipar outros):

```cpp
// State.h:302
template <class Fn>
void MagicState::UpdatePrevExtraEquippedForOverlay(Fn&& equipFn) {
    auto before = BuildInventoryIndex(player);
    std::forward<Fn>(equipFn)();           // executa o equip
    auto after = BuildInventoryIndex(player);

    // detecta o que desapareceu
    for (auto* base : before.wornBases) {
        if (after.wornBases.contains(base)) continue;
        // ...adiciona a prevExtraEquipped para restaurar depois
    }
}
```

**Problemas:**
1. `BuildInventoryIndex` faz scan completo do inventário do player a cada chamada de equip — custo potencialmente alto se o inventário for grande
2. A abordagem depende de que nada mais mude o inventário entre `before` e `after`
3. Torna `MagicState` responsável por rastrear side effects de equip que não fazem parte do fluxo principal

## Estratégia

`EquipEventAdapter` (Inbound) já recebe eventos de `RE::ActorEquipManager` para todos os equips/unequips do player. Em vez de fazer diff ativo, usar os eventos passivos para capturar side effects.

### Fluxo proposto

1. Quando `SpellSystemController` inicia um equip (enquanto `_inSlotSetup = true`), `EquipEventAdapter` captura eventos de **unequip** que não foram solicitados pelo plugin — esses são os side effects de overlay.

2. `EquipEventAdapter` notifica `SpellSystemController` (Application), que notifica `MagicState` via `NotifyUnexpectedUnequip(formID)`.

3. `MagicState` adiciona o item a `prevExtraEquipped` sem precisar fazer diff.

```cpp
// Novo método em MagicState
void NotifyUnexpectedUnequip(RE::TESBoundObject* base) {
    if (!_inSlotSetup) return;  // só durante setup de slot
    const bool exists = std::ranges::any_of(_restore.prevExtraEquipped,
        [&](auto& e) { return e.base == base; });
    if (!exists) _restore.prevExtraEquipped.push_back({base, nullptr});
}
```

4. Remover `UpdatePrevExtraEquippedForOverlay` e o template de `State.h`.

### Filtragem em `EquipEventAdapter`

`EquipEventAdapter` já recebe eventos de equip/unequip. Precisa distinguir:
- Unequips **solicitados pelo plugin** (parte normal do fluxo) → ignorar
- Unequips **não solicitados** que ocorrem durante `_inSlotSetup` → reportar como side effect

O critério: se `SpellSystemController.IsInSlotSetup()` está ativo e o item que foi desaparado não é o spell que o plugin acabou de equipar → é um side effect de overlay.

## Passos

1. Adicionar `NotifyUnexpectedUnequip(RE::TESBoundObject* base)` em `MagicState` e em `SpellSystemController`.

2. Modificar `EquipEventAdapter` para chamar `SpellSystemController::NotifyUnexpectedUnequip` quando:
   - `SpellSystemController::IsInSlotSetup() == true`
   - O evento é um **unequip** (não um equip)
   - O item desaparado não é um dos spells sendo equipados neste ciclo

3. Remover o template `UpdatePrevExtraEquippedForOverlay` de `State.h`.

4. Remover todas as chamadas a `UpdatePrevExtraEquippedForOverlay` em `SpellSystemController.cpp`.

5. Simplificar o local onde o equip era chamado (não precisa mais ser dentro do template).

6. Testar: equipar spell que causa unequip de item overlay (ex: algum mod de outfit overlay) — verificar que o item é restaurado corretamente após o slot desativar.

## Resultado esperado

- Sem diff de inventário por operação de equip
- `MagicState` não precisa mais de template para capturar side effects
- Side effects de overlay são capturados passivamente via `EquipEventAdapter`
- `_inSlotSetup` permanece como flag de coordenação entre os dois sistemas