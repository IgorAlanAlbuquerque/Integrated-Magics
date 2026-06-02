# B1 — Exclusive state sujo ao iniciar o jogo

## Sintoma

Com o patch `pressBothAtSame` ou `requireExclusiveHotkey` ativo, ao abrir o jogo e carregar
um save o sistema de hotkeys não funciona. Abre um menu (inventário, magia, etc.) e fecha
— o sistema passa a funcionar normalmente.

Abrir+fechar um menu chama `ClearLikelyStuckKeysAfterMenuClose` → `ClearEdgeStateOnly`
→ `DiscardExclusivePending`, que limpa o `ExclusiveStore`, `SlotEdgeStore` e o vetor
`retained`. Isso conserta o estado.

## Causa raiz

`NotifyLoadGame` (chamado via `GameEventAdapter` no `TESLoadGameEvent`) chama apenas
`ForceExit()` no `MagicState`, mas **não reseta o `InputController`**.

O reset do `InputController` hoje depende de:
1. `kDataLoaded` → `OnConfigChanged()` → `ResetExclusiveState` — só na inicialização do plugin
2. Transição `m_prevBlocked (true) → blocked (false)` → `ClearLikelyStuckKeysAfterMenuClose`

O caso 2 deveria cobrir o fim do loading screen, mas pode falhar quando:
- A transição `MainMenu → LoadingMenu → gameplay` acontece rápido demais e
  `ProcessAndFilter` não é chamado com `blocked = true` entre as janelas
- O loading screen fecha mas o `PollInputDevicesHook` não recebe eventos durante essa
  transição, mantendo `m_prevBlocked` desatualizado

### Por que o bug se manifesta especificamente com exclusive press

Para slots configurados como multi-key ou com `requireExclusive`:
- O path normal (`else` em `RecomputeSlotEdges`) nunca executa para eles
- `ComputeAcceptedExclusive` mantém estado em `pendingSrc`, `simWindowActive`,
  `prevAnyKeyDown`, `filterWindowActive`, `retained`
- Se esse estado fica sujo (ex: `slotWasAccepted = true` com combo aparentemente down
  por um evento espúrio durante o load), `ShouldFilterAndSave` vai filtrar TODOS os
  eventos futuros para aquele slot

Para slots simples sem exclusive patches: `accNow = rawNow` — nenhum estado intermediário.
Por isso o bug não aparece sem os patches.

## Arquivos relevantes

| Arquivo | O que olhar |
|---|---|
| `src/Application/InputController.cpp` | `ProcessAndFilter` — transições de `m_prevBlocked`, `ClearLikelyStuckKeysAfterMenuClose` |
| `src/Input/ExclusiveTracker.cpp` | `ResetExclusiveState`, `ClearEdgeStateOnly`, `DiscardExclusivePending` |
| `src/Application/SpellSystemController.cpp` | `NotifyLoadGame` — onde adicionar o reset |
| `include/Application/InputController.h` | Declarar novo método `ResetInputState` |
| `src/plugin.cpp` | `kPostLoadGame` — ponto alternativo para reset |

## Fix

### Opção A — Reset no NotifyLoadGame (recomendado)

Adicionar `InputController::ResetInputState()` que chama `ClearLikelyStuckKeysAfterMenuClose`
(ou no mínimo `ResetExclusiveState`) e chamá-lo em `SpellSystemController::NotifyLoadGame`:

```cpp
// InputController.h
void ResetInputState();

// InputController.cpp
void InputController::ResetInputState() {
    Input::detail::ResetExclusiveState(m_slots, m_exclusive, m_replay, m_retained, m_deferred);
}

// SpellSystemController.cpp
void SpellSystemController::NotifyLoadGame() const {
    auto& state = IntegratedMagic::MagicState::Get();
    HandleForceExitResult(state.ForceExit());
    InputController::Get().ResetInputState();  // ← ADD
}
```

### Opção B — Reset no kPostLoadGame (plugin.cpp)

```cpp
case SKSE::MessagingInterface::kPostLoadGame: {
    // ... código existente ...
    Application::InputController::Get().ResetInputState();
    break;
}
```

Opção A é preferível porque `NotifyLoadGame` já é o handler de load no nível da aplicação.

## Invariantes

- `ResetExclusiveState` deve ser chamado do game main thread (runs-on: `GameEventAdapter`
  que já está no main thread) — sem problemas de threading.
- `m_prevBlocked` **não precisa** ser resetado junto — a próxima transição o atualizará.
- O reset não deve afetar `m_hotkeys` nem `m_slots` — esses são config, não estado.
  Não chamar `OnConfigChanged()` (que recarrega config do disco desnecessariamente).

## Teste

1. Abrir o jogo
2. Carregar um save
3. Pressionar hotkey imediatamente sem abrir nenhum menu → slot deve ativar
4. Repetir com `pressBothAtSame` e `requireExclusiveHotkey` ambos ativados/desativados