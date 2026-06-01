# T12 — Remover dependências de Config em `SpellSystemController`

## Violação
**Application → Config**

`src/Application/SpellSystemController.cpp` importa `Config/Slots.h` e `Config/ConfigAdapter.h`.
Application só pode importar Domain e Shared.

## Mapeamento de uso

| Import | Onde é usado | Para quê |
|---|---|---|
| `Config/Slots.h` | `GetActiveSlotContents()` | GetSlotSpell(left), GetSlotSpell(right), GetSlotShout para o slot ativo |
| `Config/ConfigAdapter.h` | `ExecuteRestoreSnapshotPlan()` (3×) | `SkipEquipAnimationOnReturn()` — bool de configuração |

## Estratégia: duas frentes independentes

### Frente 1 — `Config/Slots.h` via `GetActiveSlotContents()`

**Contexto:** `GetActiveSlotContents()` foi criado na T8 para que EquipEventAdapter (Inbound)
pudesse consultar os FormIDs do slot ativo sem importar Config. A implementação atual delega
para `Slots::GetSlot*` — substituindo a violação de EquipEventAdapter por uma violação de
SpellSystemController.

**Solução:** MagicState (Domain) já armazena `SlotEntry` internamente quando `OnSlotPressed()`
é chamado (campo privado `SlotEntry` com `leftID`, `rightID`, `shoutID`). Basta expor esses
FormIDs na API pública de MagicState:

Adicionar em `SessionState` (ou diretamente em `MagicState`):
```cpp
struct SessionState {
    // ... campos existentes ...
    RE::FormID activeLeftID{0};
    RE::FormID activeRightID{0};
    RE::FormID activeShoutID{0};
};
```

Populados em `EnterHand()`/`PrepareSlotEntry()` quando o slot é ativado. Limpos em
`ResetSessionState()`.

Adicionar getters públicos em `MagicState`:
```cpp
RE::FormID ActiveLeftID() const noexcept  { return _session.activeLeftID; }
RE::FormID ActiveRightID() const noexcept { return _session.activeRightID; }
RE::FormID ActiveShoutID() const noexcept { return _session.activeShoutID; }
```

`GetActiveSlotContents()` em `SpellSystemController` passa a ler de `MagicState::Get()`:
```cpp
ActiveSlotContents SpellSystemController::GetActiveSlotContents() const {
    auto& s = IntegratedMagic::MagicState::Get();
    return { s.ActiveLeftID(), s.ActiveRightID(), s.ActiveShoutID() };
}
```

Remove a necessidade de `Config/Slots.h` em SpellSystemController.

### Frente 2 — `Config/ConfigAdapter.h` via `SkipEquipAnimationOnReturn()`

**Contexto:** `ExecuteRestoreSnapshotPlan()` lê `SkipEquipAnimationOnReturn()` 3 vezes para
decidir se deve aplicar animação de retorno de equip. É um bool de configuração imutável
durante a sessão.

**Solução A (cache local em SpellSystemController):**
Adicionar `m_skipEquipAnimOnReturn` ao estado interno de SpellSystemController. Populado em
`OnConfigChanged()` que já existe e é chamado quando config muda.

```cpp
// include/Application/SpellSystemController.h — campo privado
bool m_skipEquipAnimReturn{false};

// src/Application/SpellSystemController.cpp
void SpellSystemController::OnConfigChanged() const {
    // já chama InputController::Get().OnConfigChanged()
    m_skipEquipAnimReturn = IntegratedMagic::Config::MagicConfigAdapter::Get().SkipEquipAnimationOnReturn();
    InputController::Get().OnConfigChanged();
}
```

`ExecuteRestoreSnapshotPlan()` usa `m_skipEquipAnimReturn` em vez de chamar ConfigAdapter.

**Solução B (parâmetro no RestoreSnapshotPlan):**
Adicionar `bool skipEquipAnimOnReturn` em `RestoreSnapshotPlan` (Shared). O chamador
(SpellSystemController) lê de Config e inclui no plano ao criá-lo. `ExecuteRestoreSnapshotPlan`
usa o valor do plano, sem tocar Config.

**Recomendação:** Solução A é mais simples e mantém `RestoreSnapshotPlan` limpo.
Se SpellSystemController já vai ser const-refactorado, B pode fazer mais sentido.

### Passos

1. Em `include/Domain/State.h` — `SessionState`: adicionar `activeLeftID`, `activeRightID`,
   `activeShoutID`. Adicionar getters públicos em `MagicState`.
2. Em `src/Domain/` — onde `PrepareSlotEntry` popula o `SlotEntry`: copiar os IDs para
   `_session.activeLeft/Right/ShoutID`. Limpar em `ResetSessionState()`.
3. Atualizar `GetActiveSlotContents()` em SpellSystemController para ler de MagicState.
4. Remover `#include "Config/Slots.h"` de SpellSystemController.
5. Implementar Solução A: adicionar `m_skipEquipAnimReturn` ao SpellSystemController;
   popular em `OnConfigChanged()`; usar em `ExecuteRestoreSnapshotPlan()`.
6. Remover `#include "Config/ConfigAdapter.h"` de SpellSystemController.
7. Build — confirmar que não há erros.

## Resultado esperado

- `SpellSystemController.cpp` não importa mais nada de `Config/`
- `GetActiveSlotContents()` lê FormIDs armazenados em MagicState (Domain) — fonte autoritária
  durante sessão ativa
- `SkipEquipAnimationOnReturn` cacheado em SpellSystemController ao carregar config
- Fluxo correto: Application lê config uma vez em `OnConfigChanged()` e opera com dados locais