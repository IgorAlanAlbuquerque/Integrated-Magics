# R4 — Remover flag `pressAutocast` — tornar comportamento explícito

## Problema

`HandMode::pressAutocast` é um flag que faz o modo Press executar um **único cast automático** ao ser ativado, comportamento essencialmente idêntico ao modo Automatic. O resultado é um terceiro modo disfarçado de flag booleano, com ramificações condicionais espalhadas em vários pontos:

| Localização | O que faz |
|---|---|
| `MagicStateSlot.cpp:138` (`EnterHand Press`) | Seta `pressAutocast = true` se `wantAutoAttack` |
| `MagicStatePump.cpp:355` (`OnCastStop`) | Branch especial: reseta campos sem chamar `FinishHand` |
| `MagicStatePump.cpp:1098` (`OnSpellFired`) | Branch especial: reseta campos sem chamar `FinishHand` |

Em todos esses pontos há um `if (pressAutocast) { ... } else { FinishHand(); }` que cria dois caminhos paralelos para o mesmo evento.

## Por que existe

Press mode com autocast quer o comportamento: "ao pressionar, dispara uma vez; a magia fica equipada (toggle); ao pressionar novamente, desativa". O autocast é o disparo único inicial. Sem o flag, o Press mode não enviaria nenhum ataque automaticamente.

## Estratégia

Em vez de um flag que mistura lógica, tornar o comportamento de autocast no Press mode **uma responsabilidade do `EnterHand`** apenas: ao entrar em Press mode com `wantAutoAttack = true`, simplesmente definir os campos de autocast como se fosse Automatic para o **primeiro disparo**, mas manter `pressActive = true` para o toggle:

```cpp
// EnterHand Press com wantAutoAttack:
hm.pressActive = true;
hm.autoActive = true;          // primeiro disparo usa fluxo de Automatic
hm.waitingChargeComplete = true;
hm.waitingAutoAfterEquip = true;
hm.autoCastPhase = WaitingAttackEnable;
// (sem pressAutocast)
```

A diferença entre Automatic e Press-com-autocast está no que acontece **após** `FinishHand`:
- Automatic → `TryFinalizeExit()` → session termina
- Press → `autoActive = false`, mas `pressActive` permanece → session continua até segundo press

Esse comportamento pós-FinishHand pode ser encapsulado em `FinishHand` ou em `TryFinalizeExit` com verificação de `pressActive`.

## Passos

1. **Remover `HandMode::pressAutocast`** de `State.h`.

2. **Modificar `EnterHand` (Press com wantAutoAttack)** em `MagicStateSlot.cpp`:
   - Setar `autoActive = true`, `waitingChargeComplete = true`, `autoCastPhase = WaitingAttackEnable`
   - Sem setar `pressAutocast`

3. **Modificar `FinishHand`** em `MagicStateSlot.cpp`:
   - Adicionar lógica: se `pressActive == true` ao finalizar, não fazer `TryFinalizeExit` — apenas desativar `autoActive` e aguardar o segundo press
   - Ou criar `FinishAutocastForPressHand(Hand h)` separado

4. **Remover os blocos `if (pressAutocast)`** em `OnCastStop` e `OnSpellFired`:
   - Substituir pelo comportamento unificado de `FinishHand` que lida com Press corretamente

5. Testar: Press mode com e sem wantAutoAttack — verificar toggle funciona, autocast do primeiro press funciona, segundo press desativa corretamente.

## Resultado esperado

- Sem flag `pressAutocast` em `HandMode`
- `OnCastStop` e `OnSpellFired` sem branches especiais de pressAutocast
- Press mode com autocast usa o mesmo fluxo de Automatic para o primeiro disparo, diferindo apenas no que acontece depois