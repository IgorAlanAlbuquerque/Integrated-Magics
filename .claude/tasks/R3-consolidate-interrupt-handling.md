# R3 — Consolidar tratamento de interrupts em um único caminho

## Problema

Existe duplicação de lógica de filtragem de interrupts em dois lugares desconexos:

### Caminho 1: `OnCastInterrupt` (evento de animação `InterruptCast`)
`src/Domain/MagicStatePump.cpp:400`

```cpp
// Ignora os primeiros N interrupts baseado em wasHandsDown + contador
if (_session.firstInterrupt == 0) { ignore; return; }
if (_session.wasHandsDown && _session.firstInterrupt == 1) { ignore + reset phase; return; }
// Terceiro (ou segundo com mãos levantadas) → interrupt real
```

Usa `SessionState::firstInterrupt` como contador de quantos interrupts espúrios já chegaram.

### Caminho 2: `OnCasterInterrupt` (hook de `RE::MagicCaster`)
`src/Domain/MagicStatePump.cpp:1220`

```cpp
// Ignora se o cast não começou de verdade ainda
if (!hm.sawBeginCastEvent) { ignore; return; }
// Ignora durante WaitingChargeRelease (charge já saiu)
if (hm.autoCastPhase == WaitingChargeRelease) { ignore; return; }
// Não-imediato: só seta casterInterruptPending = true
// O pump lida com ele no próximo ciclo
```

Usa `sawBeginCastEvent` e `casterInterruptPending` como filtros.

### Por que isso é um problema

Os dois caminhos filtram a mesma classe de problema (interrupts antes do cast real) com estados diferentes, e podem conflitar:
- Um interrupt pode passar pelo filtro de um caminho mas não do outro
- Corrigir um não corrige o outro
- `casterInterruptPending` difere o handling para o próximo frame sem necessidade clara

## Estratégia de consolidação

Com **R1** implementado (polling de `caster->state`), `OnCastInterrupt` (evento de animação) pode ser **completamente removido** — a detecção de fim/interrupt de cast é feita pelo polling direto do caster, não por eventos.

`OnCasterInterrupt` (hook do `RE::MagicCaster`) é mais confiável por vir da engine, mas ainda precisa de filtros. A consolidação simplifica para:

```cpp
void MagicState::OnCasterInterrupt(Hand hand, ...) {
    if (!_session.active) return;
    auto& hm = ModeFor(hand);
    if (hm.finished) return;

    // Só relevante se estamos ativamente castando
    if (hm.phase != AutoCastPhase::Casting && hm.phase != AutoCastPhase::WaitingChargeRelease)
        return;

    // Interrupt real enquanto castando → encerrar a mão
    FinishHand(hand);
    TryFinalizeExit();
}
```

Sem `casterInterruptPending`, sem `sawBeginCastEvent`, sem contador `firstInterrupt`.

## Passos (após R1)

1. **Remover `OnCastInterrupt`** de `MagicStatePump.cpp` e sua declaração de `State.h`.

2. **Remover `SessionState::firstInterrupt`** de `State.h`.

3. **Simplificar `OnCasterInterrupt`**:
   - Remover a guarda `!hm.sawBeginCastEvent`
   - Remover `WaitingChargeRelease` como caso especial de ignorar (avaliar se ainda faz sentido)
   - Remover `casterInterruptPending = true` — processar inline, sem defer

4. **Remover `HandMode::sawBeginCastEvent`** e **`HandMode::casterInterruptPending`** de `State.h`.

5. **Remover handler em `SpellSystemController::NotifyAnimEvent`** para `"InterruptCast"sv` — ou mantê-lo como log apenas.

6. Testar: interrupt por bloco, knockback, morte — verificar que todos os casos são tratados corretamente.

## Passos (sem R1 — alternativa de curto prazo)

Se R1 ainda não está pronto, a consolidação parcial é:
- Unificar a lógica de filtragem em um único método `ShouldIgnoreInterrupt(Hand h)` chamado por ambos
- Remover `casterInterruptPending` — processar `OnCasterInterrupt` inline usando `IsCasterIdleForExpectedSpell` para confirmar

## Resultado esperado

- Um único ponto de tratamento de interrupt (`OnCasterInterrupt`)
- Sem counters (`firstInterrupt`)
- Sem flags de defer (`casterInterruptPending`, `sawBeginCastEvent`)
- Lógica de "é um interrupt real?" derivada da fase atual (`AutoCastPhase`), não de contagem