# R6 — Substituir `ShouldForceInterrupt` por hooks específicos por condição

## Problema

`ShouldForceInterrupt` é um método de "polling de consistência" chamado a cada frame em `PumpAutomatic`. Cada condição representa um caso onde a engine interrompeu o cast sem notificar o plugin corretamente:

```cpp
// MagicStateLifecycle.cpp:283
bool MagicState::ShouldForceInterrupt() const {
    if (PlayerIsDead(pc)) return true;
    if (PlayerIsKnockedOrStaggered(pc) && !pressMode) return true;
    if (PlayerIsBlocking(pc) && !pressMode) return true;
    if (PlayerIsSheathingOrSheathed(pc) && !shout && !pendingSheatheRestore) return true;
    if (CasterSpellMismatch(caster, modeSpellRight)) return true;
    if (CasterSpellMismatch(caster, modeSpellLeft)) return true;
    return false;
}
```

**Problemas:**
1. Executa checks ativos a cada frame — mesmo quando nenhuma das condições é relevante
2. Cada condição aqui indica que falta um hook/evento para aquele caso específico
3. O check de "caster spell mismatch" cobre o mesmo território que R1 (polling de caster state) tornaria redundante

## Análise por condição

### `PlayerIsDead` — OK, já tem handler
`SpellSystemController::NotifyPlayerDeath()` é chamado por `GameEventAdapter` quando o player morre. O check em `ShouldForceInterrupt` é redundante.

**Fix:** Remover do `ShouldForceInterrupt`. O `NotifyPlayerDeath` já cobre.

### `PlayerIsKnockedOrStaggered` — falta hook de animação
Stagger e knockback têm eventos de animação em Skyrim (`StaggerStart`, `KnockDown`).

**Fix:** Adicionar listener para `"staggerStart"` e `"KnockDown"` em `SpellSystemController::NotifyAnimEvent`. Chamar `ForceExit()` nesses eventos (já existem outros anim events sendo ouvidos).

### `PlayerIsBlocking` — falta hook de animação
Block start já tem handler parcial:
```cpp
// SpellSystemController.cpp:174
if (tag == "blockStart"sv || tag == "BashExit"sv) {
    if (!state.IsPressMode()) HandleForceExitResult(state.ForceExit());
}
```
O check em `ShouldForceInterrupt` é **redundante** com esse handler.

**Fix:** Remover do `ShouldForceInterrupt`. O handler de `blockStart` já cobre.

### `PlayerIsSheathingOrSheathed` — parcialmente coberto
`tailMTIdle` e `IdleStop` já notificam sheathe complete:
```cpp
if (tag == "tailMTIdle"sv || tag == "IdleStop"sv) {
    if (state.IsWaitingSheatheRestore()) state.NotifySheatheComplete();
}
```
Mas o sheathe inesperado (player sheathes durante cast) não tem hook de animação.

**Fix:** Adicionar listener para `"WeaponSheathe"` ou `"WantToSheathe"` anim event (verificar o nome exato no behavior). Chamar `ForceExit` quando sheathe inesperado e não estamos em `pendingRestoreAfterSheathe`.

### `CasterSpellMismatch` — coberto por R1
Com R1 (polling de `caster->state`), detectar que o caster não tem mais o spell esperado é parte do loop de pump normal. O mismatch seria detectado como "cast terminou" e tratado pelo fluxo de exit normal.

**Fix:** Remover do `ShouldForceInterrupt` após R1 estar implementado. Ou, antes de R1, manter apenas como log (não como force exit).

## Passos

1. **Remover `PlayerIsDead`** — `NotifyPlayerDeath` cobre.

2. **Remover `PlayerIsBlocking`** — `blockStart` anim event cobre.

3. **Adicionar listeners de animação para stagger/knockback** em `SpellSystemController::NotifyAnimEvent`:
   ```cpp
   if (tag == "staggerStart"sv || tag == "KnockDown"sv) {
       if (!state.IsPressMode()) HandleForceExitResult(state.ForceExit());
   }
   ```
   Verificar nomes exatos dos anim events no Skyrim behavior. Depois remover `PlayerIsKnockedOrStaggered` do `ShouldForceInterrupt`.

4. **Adicionar listener para sheathe inesperado**:
   - Pesquisar evento de animação de sheathe (`"WeaponSheathe"`, `"Sheath"`, ou similar)
   - Adicionar handler com condição: se `!pendingRestoreAfterSheathe && shout == 0`, chamar `ForceExit`
   - Depois remover `PlayerIsSheathingOrSheathed` do `ShouldForceInterrupt`

5. **Remover `CasterSpellMismatch`** após R1, ou desativar como force exit (manter só como log).

6. Se após todos os passos `ShouldForceInterrupt` ficar vazio → **remover o método** e sua chamada em `PumpAutomatic`.

7. Testar: morrer durante cast, ser knocked, bloquear, sheathe manual — verificar que todos fazem force exit corretamente.

## Resultado esperado

- `ShouldForceInterrupt` removido (ou vazio — remover)
- `PumpAutomatic` sem polling de consistência por frame
- Cada condição de interrupção forçada tem seu próprio evento/hook específico
- Zero checks ativos por frame para estados que raramente acontecem