# R7 — Eliminar timers empíricos (consequência de R1)

## Problema

O sistema tem 6 constantes de tempo fixo empíricas, cada uma representando um caso onde o código não sabe quando um estado externo vai mudar e espera um tempo arbitrário:

| Constante | Valor | Localização | Por que existe |
|---|---|---|---|
| `kDelayedStartSec` | 50ms | `State.h:298` | Delay entre parar e reiniciar ataque após CastStop skip |
| `kFallbackDelay` | 250ms | `MagicStatePump.cpp:643` | Fallback se `EnableBumper` não chegar |
| `kStallTimeout` | 200ms | `MagicStatePump.cpp:722` | Timeout de retry se caster ficar idle |
| `kSheatheWaitTimeoutSec` | 1000ms | `State.h:91` | Timeout para aguardar animação de sheathe |
| `kPowerRestoreDelaySec` | 50ms | `State.h:89` | Delay antes de restaurar power |
| `kSpellFireFinalizeDelay` | 700ms | `MagicStatePump.cpp:1174` | Delay após spell fire antes de finalizar |

Esta task é principalmente uma **consequência de R1** — a maioria dos timers se torna desnecessária quando a detecção de fase passa a ser feita por `caster->state`. Documenta o destino de cada timer após R1.

## Destino de cada timer

### `kDelayedStartSec = 50ms` — **eliminar após R1**
Existe para evitar race condition ao reiniciar o ataque após um CastStop skip. Com R1, não há mais "reiniciar ataque após CastStop skip" — o ataque é reiniciado quando `caster->state` volta a `Idle`. Sem race condition.

**Substituição:** Aguardar `caster->state == Idle` antes de reenviar o ataque (já é o comportamento de R1).

---

### `kFallbackDelay = 250ms` — **eliminar após R1**
Fallback para quando `EnableBumper` não chega em 250ms. Com R1, `EnableBumper` não é mais o gatilho para iniciar o ataque — o ataque é enviado imediatamente após equip, e a confirmação de que começou é via `caster->state`. O fallback se torna desnecessário.

**Substituição:** Remover junto com `PumpAutoStartFallback` (eliminado em R1).

---

### `kStallTimeout = 200ms` + `kMaxRetries = 3` — **eliminar após R1**
Timeout de retry quando o caster fica idle inesperadamente durante `StartRequested`. Com R1, "caster ficou idle durante StartRequested" é simplesmente detectado como "cast não começou" — reinicia o ataque uma vez (sem contador de retries), ou desiste após N frames idle.

**Substituição:** Lógica simples em R1: se `StartRequested` por mais de N frames e `caster->state == Idle`, reiniciar ataque uma vez. Se ainda assim Idle, desistir (`FinishHand`).

---

### `kSheatheWaitTimeoutSec = 1000ms` — **manter como safety net**
Aguarda animação de sheathe antes de restaurar equipamento. Com R6 (hooks de animação), sheathe complete é detectado pelo evento de animação. O timeout de 1s permanece como safety net caso o evento não chegue (ex: teleporte, loading screen durante o processo).

**Ação:** Manter o valor, mas documentar que é safety net para evento de animação. Considerar reduzir para 500ms se o evento for confiável com R6.

---

### `kPowerRestoreDelaySec = 50ms` — **investigar se ainda necessário**
Delay de 50ms antes de restaurar o power após uso. Provavelmente existe para evitar que a restauração conflite com o animation state do power sendo disparado.

**Ação antes de eliminar:** 
1. Logar quando o delay é usado
2. Testar remover o delay — se funcionar, eliminar
3. Se o problema persistir, documentar precisamente POR QUE o delay é necessário (qual estado conflita)

---

### `kSpellFireFinalizeDelay = 700ms` — **investigar se ainda necessário**
Delay após `MRh_SpellFire_Event` / `MLh_SpellFire_Event` antes de finalizar o cast. Provavelmente existe para dar tempo ao spell projectile ser criado antes de restaurar o equipamento anterior.

**Ação antes de eliminar:**
1. Testar remover o delay — se o spell projectile aparece corretamente, eliminar
2. Alternativamente, substituir por verificação de estado: aguardar `caster->currentSpell == null` após o fire event

---

## Passos (após R1)

1. Após R1 estar funcionando, verificar um a um:
   - `kDelayedStartSec` → confirmar que o `DelayedStart` foi removido (R1 o elimina)
   - `kFallbackDelay` → confirmar que `PumpAutoStartFallback::WaitingAttackEnable` foi removido (R1 o elimina)
   - `kStallTimeout` → simplificar ou eliminar o retry baseado em caster state idle

2. Para `kPowerRestoreDelaySec` e `kSpellFireFinalizeDelay`:
   - Adicionar logs detalhados nos pontos de uso
   - Testar em builds de debug com os delays zerados
   - Documentar resultado antes de decidir eliminar ou manter

3. Após eliminar os timers que podem ser removidos, atualizar `CLAUDE.md` removendo referências a eles.

## Resultado esperado

- `kDelayedStartSec`, `kFallbackDelay`, `kStallTimeout`, `kMaxRetries` — **removidos** (dependem de R1)
- `kSheatheWaitTimeoutSec` — **mantido** como safety net com documentação clara
- `kPowerRestoreDelaySec`, `kSpellFireFinalizeDelay` — **avaliados empiricamente** e removidos ou documentados com justificativa precisa
- Nenhum timer cujo valor foi determinado "parece que funciona com esse tempo"