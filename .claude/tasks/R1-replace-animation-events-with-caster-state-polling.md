# R1 — Substituir detecção de fase por polling de `RE::MagicCaster::state`

## Problema (causa raiz de ~5 gambiarras)

O sistema atual detecta fases do cast ouvindo **eventos de animação** (`CastStop`, `InterruptCast`, `EnableBumper`, `BeginCastLeft/Right`). Esses eventos:
- Chegam em momentos imprecisos
- Disparam espuriamente quando outros mods alteram animações (`skipEquipAnimation`)
- Exigem contadores de skip (`castStopsToSkip`, `firstInterrupt`, `dualCastSkipCastStops`)
- Obrigam timers fixos de espera como fallback quando os eventos não chegam

`RE::MagicCaster::state` (lido via `caster->state.get()`) já contém a fase exata do cast **diretamente na engine**, frame a frame:

| `state` | Significado |
|---|---|
| `0` | Idle (não está castando) |
| `1` | Charging (carregando) |
| `2` | Ready (carregado, aguardando release) |
| `3` | Concentrating / Releasing |

Ler esse estado a cada frame elimina a necessidade de contar eventos.

---

## Estado atual do código (pós T1–T14)

Desde que R1 foi escrito, foram adicionadas duas coisas importantes ao sistema:

**`OnCasterStartCast(Hand, spell, type)`** (`MagicStatePump.cpp:1222`) — Hook no `RE::MagicCaster` (não é evento de animação) que dispara quando o caster começa um cast. Atualmente chama `ConfirmAutoCastStarted(hand)` se `phase == StartRequested`. Isso já substitui parcialmente `BeginCastLeft/Right`, mas ainda coexiste com `sawBeginCastEvent` e `waitingBeginCast`.

**`OnCasterInterrupt(Hand, spell, depleteEnergy)`** (`MagicStatePump.cpp:1246`) — Hook no `RE::MagicCaster` que dispara quando um cast é interrompido no nível do caster. Atualmente seta `casterInterruptPending = true` mas é filtrado por `!hm.sawBeginCastEvent` (que depende de um evento de animação).

Ambos os hooks existem mas estão **condicionados a flags que dependem de eventos de animação**. Com polling de `caster->state`, esses guards são desnecessários.

A infraestrutura de polling já existe: `HasRealCastStarted()` e `IsCasterIdleForExpectedSpell()` leem `caster->state` e `caster->currentSpell`. Mas atualmente são usadas como **fallback** dentro de `PumpAutoStartFallback`, não como fonte primária.

---

## Variáveis/mecanismos que esta task elimina

| Item | Localização | Motivo da existência |
|---|---|---|
| `CastFlags::castStopsToSkip` | `State.h:128`, `MagicStatePump.cpp:284` | Contar CastStop espúrios |
| `SessionState::firstInterrupt` | `State.h:69`, `MagicStatePump.cpp:408` | Contar InterruptCast espúrios |
| `SessionState::dualCastSkipCastStops` | `State.h:70`, `MagicStatePump.cpp:334` | CastStop espúrios no dual cast |
| `HandMode::waitingBeginCast` + `beginCastWaitSecs` + `beginCastRetries` | `State.h:50-52` | Polling para confirmar início do cast via evento |
| `HandMode::sawBeginCastEvent` | `State.h:58` | Flag para filtrar interrupts antes do BeginCast |
| `HandMode::waitingAutoAfterEquip` + `waitingEnableBumperSecs` | `State.h:45,49` | Aguardar EnableBumper antes de enviar ataque |
| `AutoCastPhase::WaitingAttackEnable` | `State.h:30` | Fase de espera pelo EnableBumper |
| `kDelayedStartSec = 0.050f` | `State.h:289` | Delay ao reiniciar ataque após CastStop skip |
| `kFallbackDelay = 0.25f` | `MagicStatePump.cpp:658` | Fallback se EnableBumper não chegar |
| `kStallTimeout = 0.20f` + `kMaxRetries = 3` | `MagicStatePump.cpp:748-749` | Retry se caster ficar idle inesperadamente |
| `HandMode::stalledCastSecs` + `startRequestSecs` | `State.h:56-57` | Timers do PumpAutoStartFallback |
| `HasRealCastStarted()` | `MagicStatePump.cpp:95` | Probe do caster para confirmar início (vira PumpCastPhase inline) |
| `IsCasterIdleForExpectedSpell()` | `MagicStatePump.cpp:40` | Probe para detectar stall (desnecessário com polling contínuo) |
| `DelayedStart` struct + `PumpDelayedStarts()` | `State.h:181`, `MagicStatePump.cpp:803` | Delay de 50ms entre stop e restart |
| `HandMode::casterInterruptPending` | `State.h:59` | Defer interrupt para o próximo pump |
| `PumpAutoStartFallback()` | `MagicStatePump.cpp:633` | Fallback inteiro baseado em timers e retries |

**Nota:** `OnCasterStartCast` e `OnCasterInterrupt` são **mantidos** — são hooks de nível MagicCaster (não animação) e permanecem como sinais rápidos. A diferença é que deixam de ser a única fonte confiável: polling os corrobora frame a frame.

---

## Nova arquitetura de pump

Substituir o sistema de eventos/timers por um único `PumpCastPhase(Hand, dt)` chamado a cada frame como fonte primária:

```
PumpCastPhase(Hand h, float dt):
  hm = ModeFor(h)
  caster = GetMagicCaster(h)

  if !active || hm.finished: return

  casterState = caster->state  // 0=Idle, 1=Charging, 2=Ready, 3=Concentrating

  // Transição StartRequested → Casting
  // (OnCasterStartCast também faz isso no mesmo frame — redundância intencional)
  if hm.autoCastPhase == StartRequested && casterState >= 1:
    → hm.autoCastPhase = Casting
    → hm.waitingChargeComplete = true

  // Transição Casting → WaitingChargeRelease (charge completo)
  if hm.autoCastPhase == Casting && IsChargeComplete(caster, spell):
    → result.attack = StopDispatch
    → hm.autoCastPhase = WaitingChargeRelease
    → hm.chargeComplete = true

  // Cast terminou: caster voltou a Idle
  if (phase == Casting || phase == WaitingChargeRelease) && casterState == 0:
    → FinishHand(h) para Auto
    → Para Hold: reiniciar ataque se hotkey ainda pressionada (hm.holdActive)
```

`OnCastStop` é reduzido a: checar se alguma mão tem `chargeComplete` e dar finish; sem contadores de skip. O sinal confiável de "cast real terminou" é `casterState == 0`, não o evento.

`OnCastInterrupt` é reduzido a: chamar `FinishHand` diretamente se `phase == Casting`. O guard `sawBeginCastEvent` é substituído por `phase == Casting` (que só é atingido após `casterState >= 1` confirmado por polling).

`EnterHand` para Hold/Auto/Press: enviar o ataque imediatamente (como hoje), setar `phase = StartRequested`. Sem `waitingAutoAfterEquip` nem `WaitingAttackEnable`. O EnableBumper que chega depois vira no-op.

---

## Passos

1. **Adicionar `PumpCastPhase(Hand h, float dt)`** em `MagicStatePump.cpp`:
   - Lê `caster->state` e compara com `hm.autoCastPhase`
   - Detecta: `Idle → Charging`, `Charging → Ready`, `Casting/WCR → Idle`
   - Atualiza `AutoCastPhase` baseado nas transições
   - Retorna `PumpCastPhaseResult` com `stopAttack` e flag de `finished`

2. **Simplificar `AutoCastPhase`** — remover `WaitingAttackEnable`:
   - Novo enum: `Idle → StartRequested → Casting → WaitingChargeRelease → Done`

3. **Modificar `EnterHand`** em `MagicStateSlot.cpp`:
   - Remover: `waitingAutoAfterEquip = true`, `waitingEnableBumperSecs`, `waitingBeginCast`, `WaitingAttackEnable`
   - Setar diretamente `phase = StartRequested` se `wantAutoAttack`
   - Remover `castStopsToSkip` (e toda a lógica `skipAnim ? wasHandsDown ? 2 : 1`)
   - Remover bloco no-op equip (`currentCasterSpell == e.rightSpell` pre-decrement)

4. **Simplificar `NotifyAttackEnabled`** (`MagicStatePump.cpp:192`):
   - Remover lógica de `waitingAutoAfterEquip` — com R1, o ataque já foi enviado
   - Manter como no-op ou remover completamente se EnableBumper não tiver outros callers

5. **Simplificar `OnCastStop`** (`MagicStatePump.cpp:250`):
   - Remover todo o bloco `if (_cast.castStopsToSkip > 0)` (linhas 284–332)
   - Remover bloco `_session.dualCastSkipCastStops` (linhas 334–337)
   - Manter apenas: se `chargeComplete` ou `holdFiredAndWaitingCastStop` → FinishHand
   - Alternativamente: o evento `OnCastStop` vira no-op completo, deixando o polling liderar

6. **Simplificar `OnCastInterrupt`** (`MagicStatePump.cpp:401`):
   - Remover contagem de `firstInterrupt` (linhas 408–436)
   - Substituir guard `!sawBeginCastEvent` por `phase != Casting`
   - `OnCasterInterrupt` (hook direto) já lida com o caso real; `OnCastInterrupt` (animação) pode virar no-op

7. **Remover `PumpDelayedStarts`** (`MagicStatePump.cpp:803`):
   - A lógica de "stop + restart 50ms depois" só existia por causa do castStopsToSkip — sem ela, não há restart

8. **Remover `PumpAutoStartFallback`** (`MagicStatePump.cpp:633`):
   - O loop inteiro de WaitingAttackEnable, stall, retries deixa de existir
   - A chamada em `PumpAutomatic` (linha 1046–1047) some junto

9. **Simplificar `OnCasterStartCast`** (`MagicStatePump.cpp:1222`):
   - Manter: ainda útil como confirmação rápida (mesmo frame) da transição `StartRequested → Casting`
   - Remover guard de `!sawBeginCastEvent`; usar `phase == StartRequested` diretamente

10. **Simplificar `OnCasterInterrupt`** (`MagicStatePump.cpp:1246`):
    - Remover guard `!hm.sawBeginCastEvent`; substituir por `phase != Casting`
    - Pode agir diretamente (FinishHand) em vez de setar `casterInterruptPending`

11. **Remover campos obsoletos** de `HandMode` em `State.h`:
    - `waitingAutoAfterEquip`, `waitingEnableBumperSecs`
    - `waitingBeginCast`, `beginCastWaitSecs`, `beginCastRetries`
    - `sawBeginCastEvent`, `casterInterruptPending`
    - `stalledCastSecs`, `startRequestSecs`

12. **Remover campos obsoletos** de `SessionState`:
    - `firstInterrupt`, `dualCastSkipCastStops`, `attackEnabled`

13. **Remover `CastFlags` struct** e `_cast` member — continha apenas `castStopsToSkip`.

14. **Remover helpers mortos**:
    - `HasRealCastStarted()` — lógica migra para `PumpCastPhase`
    - `IsCasterIdleForExpectedSpell()` — desnecessário com polling contínuo
    - `ResetAutoCastStartState()` — campos que reseta não existem mais

15. Testar: Hold, Press, Automatic, Dual cast, Shout, Power — todos os modos, com e sem `skipEquipAnimation`.

---

## Dependências

- **R2** (fix wasHandsDown weapon-drawn case) pode ser feita independentemente antes desta, mas `wasHandsDown` perde relevância após R1 (sem `castStopsToSkip`, não há mais uso de `wasHandsDown` para contagem)
- **R3** (consolidar interrupts) fica trivial depois desta task: `OnCastInterrupt` vira no-op, `OnCasterInterrupt` cuida do caso real
- **R7** (eliminar timers) é consequência direta desta

---

## Resultado esperado

- Zero contadores de skip de eventos de animação
- Zero timers fixos para detecção de fase
- `AutoCastPhase` sempre reflete o estado real do `RE::MagicCaster`
- `PumpCastPhase` é a única fonte de transições de fase
- `OnCasterStartCast` e `OnCasterInterrupt` funcionam como aceleradores (mesmo frame) mas não como única fonte de verdade
- Funciona corretamente com ou sem `skipEquipAnimation`
- Funciona com espada puxada, mãos baixas, dual cast — sem casos especiais baseados em contagem