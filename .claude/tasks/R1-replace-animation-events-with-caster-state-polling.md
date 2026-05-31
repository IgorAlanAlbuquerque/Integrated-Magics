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

## Variáveis/mecanismos que esta task elimina

| Item | Localização | Motivo da existência |
|---|---|---|
| `CastFlags::castStopsToSkip` | `State.h`, `MagicStatePump.cpp:283` | Contar CastStop espúrios |
| `SessionState::firstInterrupt` | `State.h`, `MagicStatePump.cpp:407` | Contar InterruptCast espúrios |
| `SessionState::dualCastSkipCastStops` | `State.h`, `MagicStatePump.cpp:333` | CastStop espúrios no dual cast |
| `HandMode::waitingBeginCast` + `beginCastWaitSecs` + `beginCastRetries` | `State.h` | Polling para confirmar início do cast |
| `HandMode::sawBeginCastEvent` | `State.h` | Flag para filtrar interrupts antes do BeginCast |
| `HandMode::waitingAutoAfterEquip` + `waitingEnableBumperSecs` | `State.h` | Aguardar EnableBumper antes de enviar ataque |
| `kDelayedStartSec = 0.050f` | `State.h:298` | Delay ao reiniciar ataque após CastStop skip |
| `kFallbackDelay = 0.25f` | `MagicStatePump.cpp:643` | Fallback se EnableBumper não chegar |
| `kStallTimeout = 0.20f` + `kMaxRetries = 3` | `MagicStatePump.cpp:722` | Retry se caster ficar idle inesperadamente |
| `HasRealCastStarted()` | `MagicStatePump.cpp:94` | Probe do caster para confirmar início |
| `IsCasterIdleForExpectedSpell()` | `MagicStatePump.cpp:39` | Probe para detectar stall |
| `DelayedStart` struct + `PumpDelayedStarts()` | `State.h:174`, `MagicStatePump.cpp:777` | Delay de 50ms entre stop e restart |
| `HandMode::casterInterruptPending` | `State.h` | Defer interrupt para o próximo pump |

## Nova arquitetura de pump

Substituir o sistema de eventos por um único `PumpCastPhase(Hand, dt)` chamado a cada frame:

```
PumpCastPhase(Hand h):
  hm = ModeFor(h)
  caster = GetMagicCaster(h)

  if caster == null || hm.finished: return

  casterState = caster->state  // 0=Idle, 1=Charging, 2=Ready, 3=Concentrating

  // Cast confirmado: caster saiu de Idle para Charging depois de enviarmos o ataque
  if hm.phase == StartRequested && casterState >= 1:
    → hm.phase = Casting
    → cancelar qualquer DelayedStart pendente

  // Charge completo: caster chegou a Ready
  if hm.phase == Casting && casterState == 2 && !hm.chargeComplete:
    → hm.chargeComplete = true
    → release attack button (stop dispatch)
    → hm.phase = WaitingChargeRelease

  // Cast terminou: caster voltou a Idle
  if (hm.phase == Casting || hm.phase == WaitingChargeRelease) && casterState == 0:
    → É o fim real do cast (não precisa de contador)
    → FinishHand(h) se for Automatic/Hold
    → Para Hold: reiniciar ataque se hotkey ainda pressionada
```

Para **iniciar** o cast, continua usando `DispatchAttack` (envio sintético), mas a **confirmação** de que começou é pelo `casterState >= 1`, não por `BeginCastLeft/Right`.

## Passos

1. **Adicionar `PumpCastPhase(Hand h, float dt)`** em `MagicStatePump.cpp`:
   - Lê `caster->state` e `caster->currentSpell`
   - Detecta transições: `Idle → Charging`, `Charging → Ready`, `Any → Idle`
   - Atualiza `AutoCastPhase` baseado nas transições, sem depender de eventos externos

2. **Simplificar `AutoCastPhase`** — remover fases que só existiam por causa dos eventos:
   - Remover `WaitingAttackEnable` (substituído por "enviou ataque, aguarda caster->state ≥ 1")
   - Manter: `Idle`, `StartRequested`, `Casting`, `WaitingChargeRelease`, `Done`

3. **Modificar `EnterHand`** em `MagicStateSlot.cpp`:
   - Remover inicialização de `waitingAutoAfterEquip`, `waitingEnableBumperSecs`, `waitingBeginCast`
   - Remover `castStopsToSkip` (não é mais necessário)
   - Ao enviar o primeiro ataque, setar `phase = StartRequested`

4. **Modificar `NotifyAttackEnabled`** (`MagicStatePump.cpp:191`):
   - Remover lógica de `waitingAutoAfterEquip` (EnableBumper passa a ser ignorado ou apenas um log)
   - Ou remover o método completamente se EnableBumper não for mais necessário

5. **Simplificar `OnCastStop`** (`MagicStatePump.cpp:249`):
   - Remover o bloco `if (_cast.castStopsToSkip > 0)` inteiro
   - O evento `CastStop` pode ser mantido como notificação secundária, mas não é mais o gatilho primário
   - A detecção de fim de cast já é feita por `PumpCastPhase` (caster volta a Idle)

6. **Remover `PumpDelayedStarts`** e `PumpAutoStartFallback`:
   - `PumpAutoStartFallback` só existe como fallback dos eventos — com polling, não é mais necessário
   - O restart do ataque em Hold mode é guiado por "caster voltou a Idle" e "hotkey ainda pressionada"

7. **Remover campos obsoletos** de `SessionState` e `HandMode` em `State.h`.

8. **Remover `CastFlags` struct** (continha apenas `castStopsToSkip`).

9. Testar: Hold, Press, Automatic, Dual cast, Shout, Power — todos os modos.

## Dependências

- **R2** (fix wasHandsDown) pode ser feita independentemente antes desta, mas se torna desnecessária após esta task completar
- **R3** (consolidar interrupts) fica muito mais simples depois desta task
- **R7** (eliminar timers) é consequência direta desta

## Resultado esperado

- Zero contadores de skip de eventos
- Zero timers fixos para detecção de fase
- `AutoCastPhase` sempre reflete o estado real do `RE::MagicCaster`
- Funciona corretamente com ou sem `skipEquipAnimation`
- Funciona com espada puxada, mãos baixas, dual cast — sem casos especiais baseados em contagem