# R2 — Corrigir `wasHandsDown` para o caso de arma puxada

## Problema

`wasHandsDown` é setado como `true` apenas quando o weapon state é `kSheathed`:
```cpp
// MagicStateLifecycle.cpp:54
_session.wasHandsDown = (ws == RE::WEAPON_STATE::kSheathed);
```

Isso determina `castStopsToSkip`:
```cpp
_cast.castStopsToSkip = skipAnim ? (_session.wasHandsDown ? 2 : 1) : 0;
```

**O bug:** Quando o jogador tem uma **arma puxada** na mão direita (`kDrawn`) e equipa um spell na mão esquerda, o behavior machine faz uma transição adicional de "weapon drawn" para "sword+magic stance". Com `skipEquipAnimation` ativo, essa transição extra causa **2 CastStops espúrios** em vez de 1. Mas como `wasHandsDown = false` (arma puxada ≠ sheathed), o código seta `castStopsToSkip = 1` — o segundo stop espúrio é tratado como stop real → cast é encerrado antes de começar.

Reportado como: *"When using a shield and sword, or only equipping a sword in the right hand, with the spell bound to the left hand for casting, spellcasting fails if the weapon is drawn."*

## Quando este fix se aplica

- `skipEquipAnimationPatch = true`
- Slot tem spell apenas na mão esquerda (`hasLeft = true`, `hasRight = false`)
- Jogador tem arma física (não-spell) na mão direita, **puxada** (`kDrawn`)
- Weapon state **não é** `kSheathed` (então `wasHandsDown` seria `false`)

## Fix

Em `EnterHand` (ou logo antes em `OnSlotPressed`), detectar se a mão oposta tem uma arma ativa puxada e ajustar o skip count:

```cpp
// Em MagicStateSlot.cpp, ao calcular castStopsToSkip:

// Antes (atual):
_cast.castStopsToSkip = skipAnim ? (_session.wasHandsDown ? 2 : 1) : 0;

// Depois:
const bool opposingHandHasWeapon = HasDrawnWeaponInOpposingHand(hand);
const bool needsExtraSkip = !_session.wasHandsDown && opposingHandHasWeapon;
_cast.castStopsToSkip = skipAnim ? (_session.wasHandsDown || needsExtraSkip ? 2 : 1) : 0;
```

A função auxiliar `HasDrawnWeaponInOpposingHand(Hand hand)`:
```cpp
bool HasDrawnWeaponInOpposingHand(Hand hand) {
    auto* pc = GetPlayer();
    if (!pc) return false;

    // Só relevante se a mão oposta não tem spell no slot atual
    const bool leftHand = IsLeft(hand);
    auto* entry = pc->GetEquippedEntryData(!leftHand);
    if (!entry) return false;

    auto* obj = entry->GetObject();
    if (!obj) return false;

    // Se é spell item, não é arma
    if (obj->As<RE::SpellItem>()) return false;

    // Verificar se está puxada (não sheathed)
    const auto ws = pc->AsActorState()->GetWeaponState();
    return ws == RE::WEAPON_STATE::kDrawn || ws == RE::WEAPON_STATE::kWantToDraw;
}
```

## Passos

1. Adicionar função auxiliar `HasDrawnWeaponInOpposingHand(Hand hand)` em `MagicStateSlot.cpp` (ou em `InventoryUtil`).

2. Atualizar o cálculo de `castStopsToSkip` em `EnterHand` para todos os três modos (Hold, Automatic, Press) — linhas 105, 128, 151 de `MagicStateSlot.cpp`.

3. Adicionar log: `[State] EnterHand: opposingHandHasWeapon={} needsExtraSkip={} castStopsToSkip={}`.

4. Testar: espada direita + spell esquerda com `skipEquipAnimation` ativo. O cast deve iniciar normalmente.

5. Testar regressão: spell em ambas as mãos, mãos sheathed, dual cast — verificar que os casos existentes não quebraram.

## Nota sobre R1

Esta task é um **fix de curto prazo** para o bug específico reportado. Após a implementação de **R1** (polling de `RE::MagicCaster::state`), o mecanismo de `castStopsToSkip` inteiro é eliminado e esta task se torna obsoleta. Ainda assim, vale fazer R2 agora para o bug não ficar aberto enquanto R1 (que é maior) não está pronto.

## Resultado esperado

- Spell em mão esquerda com espada puxada na direita funciona com `skipEquipAnimation`
- Sem regressão nos outros casos (mãos sheathed, spell nas duas mãos, etc.)