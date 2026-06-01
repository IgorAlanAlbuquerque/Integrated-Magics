# R2 — Absorver interrupt espúrio pós-confirmação de cast

## Problema (pós-R1)

Com `weaponState == kDrawn` (mãos levantadas — inclui mãos vazias em stance de combate, espada, escudo), ao equipar um spell o behavior machine faz a transição `weapon-ready stance → magic-casting stance`. Essa transição emite um `OnCasterInterrupt` espúrio com `depleteEnergy=true` diretamente no `MagicCaster`, ~50ms após o cast começar.

Com R1, `OnCasterInterrupt` age imediatamente chamando `FinishHand` sempre que `phase == Casting`. Não há distinção entre interrupt real e espúrio. Resultado: o cast é morto 50ms após começar, o personagem fica parado com o spell na mão.

### Evidência nos logs

`weaponState=3` (kDrawn) em todos os casos. O padrão é idêntico para Flames (esquerda), Frostbite (direita), Sparks (ambas), Conjure Familiar (automático):

```
[t=31.836] RequestAutoAttackStart: ACCEPTED — aaHeld=true
[t=31.837] OnEquipComplete: dispatchLeft=true
[t=31.853] PumpCastPhase: StartRequested → Casting (state=1)
[t=31.853] ConfirmAutoCastStarted: hand=Left
[t=31.902] OnCasterInterrupt: hand=Left phase=2 depleteEnergy=true  ← espúrio (48ms depois)
[t=31.902] FinishHand: hand=Left                                    ← mata o cast
```

O mesmo acontece para direita, ambas as mãos, e modo automático. O intervalo de ~48–80ms é consistente.

### Comportamento com intervenção manual

Após o interrupt, se o jogador clicar manualmente o botão de ataque, o cast inicia e segue normalmente. O spell permanece na mão; o sistema está funcional — basta redespachar o ataque.

### O que o sistema pré-R1 fazia

`OnCastInterrupt` (evento de animação, não `OnCasterInterrupt`) tinha um contador `firstInterrupt`:
- Sempre ignorava o **primeiro** interrupt (esse espúrio da transição)
- Ignorava o segundo se `wasHandsDown` (transição de arma)
- No terceiro/segundo real: `FinishHand`

Com R1, o hook `OnCasterInterrupt` (nível MagicCaster) é mais confiável que o evento de animação, mas também dispara no interrupt espúrio. A solução mantém a lógica de absorver o primeiro interrupt, porém via hook direto.

---

## Fix

Absorver o **primeiro** `OnCasterInterrupt` após `ConfirmAutoCastStarted` (o espúrio da transição de stance). Redespachar o ataque para reiniciar. No segundo interrupt (se `phase == Casting` ainda), agir normalmente.

### Novos campos

**`HandMode` (`State.h`):**
```cpp
bool firstCasterInterruptSeen{false};
```
Resetado em `ConfirmAutoCastStarted`. Setado em `true` no primeiro interrupt (espúrio). No segundo: `FinishHand`.

**`CastInterruptResult` (`PumpResults.h`):**
```cpp
bool restartLeft{false};
bool restartRight{false};
```
Sinaliza que o controller deve redespachar o ataque (press inicial) para reiniciar o cast.

### `OnCasterInterrupt` com o fix

```cpp
if (hm.autoCastPhase != AutoCastPhase::Casting) return result;

if (!hm.firstCasterInterruptSeen) {
    // Primeiro interrupt pós-cast: espúrio (transição weapon-ready → magic stance)
    hm.firstCasterInterruptSeen = true;
    hm.autoCastPhase = AutoCastPhase::StartRequested;
    hm.waitingChargeComplete = false;
    hm.chargeComplete = false;
    // _aa.Held permanece true (PumpAutoAttack continua hold)
    _aa.Secs(hand) = 0.f;
    if (IsLeft(hand)) result.restartLeft = true;
    else result.restartRight = true;
    return result;
}

// Segundo interrupt com phase=Casting: real → terminar
const float finished = FinishHand(hand);
if (IsLeft(hand)) result.finishedLeft = finished;
else result.finishedRight = finished;
```

### Controller (`OnCastInterrupted`)

```cpp
const auto r = MagicState::Get().OnCasterInterrupt(*hand, spell, depleteEnergy);
if (r.restartLeft)  detail::DispatchAttack(Left,  1.0f, 0.0f);  // redespachar press
if (r.restartRight) detail::DispatchAttack(Right, 1.0f, 0.0f);
if (r.finishedLeft  != -1.f) detail::DispatchAttack(Left,  0.0f, r.finishedLeft);
if (r.finishedRight != -1.f) detail::DispatchAttack(Right, 0.0f, r.finishedRight);
```

---

## Passos

1. Adicionar `firstCasterInterruptSeen{false}` em `HandMode` (`include/Domain/State.h`)
2. Adicionar `restartLeft/restartRight` em `CastInterruptResult` (`include/Shared/PumpResults.h`)
3. Resetar `hm.firstCasterInterruptSeen = false` em `ConfirmAutoCastStarted` (`src/Domain/MagicStatePump.cpp`)
4. Atualizar `OnCasterInterrupt` com a lógica de absorção + restart
5. Atualizar `SpellSystemController::OnCastInterrupted` para despachar restart
6. Limpar `firstCasterInterruptSeen` em `FinishHand` (housekeeping)

---

## Resultado esperado

- Hold + autocast com `weaponState == kDrawn`: cast inicia, mantém, termina ao soltar hotkey
- Interrupt real (bloquear durante cast): `FinishHand` no segundo interrupt
- Sem regressão: mãos sheathed, dual cast, modo automático