# R3 — Consolidar tratamento de interrupts em um único caminho

## Status: IMPLEMENTADO (via R1 + R2 + R3)

---

## Problema original

Existiam dois caminhos paralelos de filtragem de interrupts:
- `OnCastInterrupt` (evento de animação `InterruptCast`) — usava `firstInterrupt` + `wasHandsDown`
- `OnCasterInterrupt` (hook `RE::MagicCaster`) — usava `sawBeginCastEvent` + `casterInterruptPending`

Dois estados, dois filtros, podiam conflitar.

---

## O que foi feito

### R1
- Removidos: `firstInterrupt`, `dualCastSkipCastStops`, `sawBeginCastEvent`, `casterInterruptPending`
- `OnCastInterrupt` (animação) virou no-op

### R2 (refinado em R3)
- `OnCasterInterrupt` passou a usar **janela de tempo** em vez de contar eventos
- Qualquer interrupt nos primeiros **0.3s** após `ConfirmAutoCastStarted` → restart
- Após 0.3s → interrupt real → `FinishHand`
- Campo adicionado: `HandMode::castingElapsedSecs` (acumulado por `PumpCastPhase`)

### R3
- Removido `OnCastInterrupt` completamente (declaração, implementação, handler em `NotifyAnimEvent`)
- Removido handler `"InterruptCast"sv` do `SpellSystemController`
- Única fonte de verdade: `OnCasterInterrupt` via `MagicCasterInterruptHook`

---

## Arquitetura final

```
RE::ActorMagicCaster::InterruptCast
  → MagicCasterInterruptHook
  → SpellSystemController::OnCastInterrupted
  → MagicState::OnCasterInterrupt(hand, spell, depleteEnergy)
      if phase != Casting: return
      if castingElapsedSecs < 0.3s: restart (re-dispatch, reset to StartRequested)
      else: FinishHand (interrupt real)
```

Sem contadores. Sem flags de defer. Lógica derivada de tempo real de cast, não de contagem de eventos.