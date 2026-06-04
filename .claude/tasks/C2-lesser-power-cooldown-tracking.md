# C2 — Cooldown tracking para Lesser Powers (kLesserPower)

## Contexto

Lesser Powers (SpellType::kLesserPower) **não têm cooldown no Skyrim vanilla** — após o cast,
o `voiceTimer` permanece 0. Portanto o `justStarted` do `SlotCooldownTracker` nunca dispara
para lesser powers vanilla, e nenhum arc de cooldown deve ser exibido (comportamento correto).

Porém, mods podem atribuir cooldown a lesser powers via Papyrus:
```papyrus
Game.GetPlayer().SetVoiceRecoveryTime(30.0)  ; 30s de cooldown
```
Nesse caso o voiceTimer se torna não-zero e o tracker (após C1) **deve capturá-lo
automaticamente** sem código adicional — ele é um SpellItem com `kLesserPower`, detectado
pelo mesmo caminho de `FindEquippedVoiceSlot`.

## Dependência

**C2 depende de C1.** Após C1 implementado, o path de `SpellItem` em `FindEquippedVoiceSlot`
já detecta tanto `kPower` quanto `kLesserPower`. Para lesser powers com cooldown não-zero, o
tracker funciona identicamente aos major powers.

## O que C2 adiciona sobre C1

C2 não é uma implementação de código — é a validação e possível refinamento de dois cenários
que C1 não exercita:

### Cenário A: Lesser power sem cooldown (vanilla)
- `voiceTimer` permanece 0 após cast → `justStarted` não dispara → sem arc → correto
- **Verificar**: nenhum loop espúrio de "justStarted/justFinished" em cada frame

### Cenário B: Lesser power com cooldown (mod)
- `voiceTimer` → não-zero → `justStarted` dispara → tracker identifica o slot
- `isPower = true` em `SlotCooldownInfo`
- Arc exibido proporcional ao cooldown definido pelo mod
- **Verificar**: tracker não confunde lesser power com shout

### Cenário C: Troca de power durante cooldown
- Usuário usa major power A (slot 1) → cooldown de 4320s começa no slot 1
- Usuário usa lesser power B (slot 2) sem cooldown → slot 2 não rastreia nada
- Mas: o voiceTimer de A continua decrementando — slot 1 continua sendo rastreado corretamente?
- **Problema potencial**: após C1, o tracker usa `isCoolingDown = GetVoiceRecoveryTime() > 0`
  para atualizar todos os slots on cooldown. Se major power A está em cooldown e lesser power B
  é usada, a `selectedPower` muda para B e `isCoolingDown` ainda é `true` (timer de A).
  O tracker vai continuar atualizando o slot 1 (A) corretamente — mas `trackedFormID` no slot 1
  não é inspecionado novamente até `justStarted` ou até que a forma do slot mude.
  Este cenário **deve funcionar corretamente** desde que o formID do slot 1 continue apontando
  para o power A (o que acontece, pois o tracker rastreia por formID de slot).

## Distinção visual: major vs lesser power

Opcional: o `isPower` em `SlotCooldownInfo` não distingue `kPower` de `kLesserPower`.
Se o HUD precisar de tratamento visual diferente (ex: cor de arc diferente), estender
`SlotCooldownInfo` com:
```cpp
enum class CooldownSource { Shout, MajorPower, LesserPower };
CooldownSource source{CooldownSource::Shout};
```
e propagar `spellType` capturado em `FindEquippedVoiceSlot`. Por ora, `isPower = true` cobre
ambos igualmente — refinar somente se o designer pedir diferenciação visual.

## Implementação necessária (além de C1)

Nenhuma alteração de código extra é necessária se C1 for implementado corretamente.
As ações de C2 são:

1. **Teste Cenário A**: usar lesser power vanilla → confirmar ausência de arc no HUD
2. **Teste Cenário B**: usar mod ou `SetVoiceRecoveryTime()` via console + SKSE para simular
   lesser power com cooldown → confirmar arc proporcional
3. **Teste Cenário C**: major power A em cooldown, usar lesser power B → confirmar que
   arc do slot A continua correto

## Log esperado

Para lesser power vanilla (sem cooldown):
- Nenhuma linha `[Cooldown] power start:` deve aparecer

Para lesser power com cooldown (mod):
- `[Cooldown] power start: slot=X formID=Y total=Z` onde Z é o cooldown setado
- `[Cooldown] finish: slot=X formID=Y variation=-1 total=Z`

## Alerta: `kMaxTrackedSlots`

O tracker atual tem `kMaxTrackedSlots = 12`. Se o usuário tiver mais de 12 slots com voice
powers, slots além do índice 11 não são rastreados. Avaliar se deve aumentar este limite ao
implementar C1+C2, já que o sistema de slots suporta até 64 (`kMaxViewSlots = 64`).