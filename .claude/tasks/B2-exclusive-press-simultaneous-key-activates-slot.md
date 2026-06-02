# B2 — Tecla do slot + outra tecla simultânea ativa o slot incorretamente

## Sintoma

Com o patch `requireExclusiveHotkey` ativo:

- **Correto**: Segurar uma tecla qualquer e depois pressionar a tecla do slot → slot NÃO ativa
- **Bug**: Pressionar a tecla do slot + outra tecla ao mesmo tempo → slot ATIVA

O comportamento correto de `requireExclusive` é: o slot só ativa se APENAS as teclas do
combo estiverem pressionadas (mais WASD/câmera que são ignorados).

## Causa raiz

Em `ComputeAcceptedExclusive` (`src/Input/ExclusiveTracker.cpp`), quando um pending existe,
a re-verificação de exclusividade é feita **apenas para combos multi-key**:

```cpp
// ExclusiveTracker.cpp ~linha 77
if (requireExcl && srcIsMulti) {
    const bool stillExcl = ComboExclusiveNow(...);
    if (!stillExcl) {
        ClearExclusivePending(s, ClearReason::Cancelled, ...);
        return false;
    }
}
```

Para combos de tecla única (`srcIsMulti = false`), **não há re-verificação** durante o
período de pending. O fluxo para single-key vai direto para:

```cpp
// ExclusiveTracker.cpp ~linha 159
} else {   // single-key path
    if (!stillDown) { ... return true; }  // aceita no release
    excl.pendingTimer[s] -= dt;           // sem check de exclusividade!
    if (excl.pendingTimer[s] <= 0.0f) {
        ClearExclusivePending(s, ClearReason::Success, ...);
        return true;                       // aceita por timeout
    }
    return false;
}
```

### Por que "simultâneo" escapa

O `PollInputDevicesHook` pode ser chamado múltiplas vezes por game tick (ex: teclado e
XInput são fontes separadas de eventos). Se S (tecla do slot) chega no hook call 1 e X
(outra tecla) chega no hook call 2 dentro do mesmo tick:

**Hook call 1:**
- `keys.kbDown[S] = true`, `keys.kbDown[X] = false`
- `ComputeAcceptedExclusive`: `ComboExclusiveNow` → apenas S está down → exclusivo → `pendingSrc = Kb` criado ✓

**Hook call 2:**
- `keys.kbDown[X] = true` (X agora está down)
- `ComputeAcceptedExclusive`: `HasExclusivePending = true`, `srcIsMulti = false`
- **Re-check de exclusividade não acontece para single-key**
- `stillDown = true` (S ainda down), timer conta... → slot ativa ✗

Quando S é segurado antes do press (caso correto): S já está down antes do pending ser
criado, então no momento do press de X:
- `ComboExclusiveNow` vê S+X → não exclusivo → pending NÃO é criado ✓

## Arquivos relevantes

| Arquivo | Linhas | O que olhar |
|---|---|---|
| `src/Input/ExclusiveTracker.cpp` | ~77-88 | Re-check multi-key (modelo a seguir) |
| `src/Input/ExclusiveTracker.cpp` | ~158-185 | Path single-key sem re-check |
| `src/Input/ExclusiveTracker.cpp` | ~190-225 | Criação do pending — check inicial |
| `include/Input/HotkeyMatcher.h` | — | `ComboExclusiveNow` |

## Fix

Aplicar a mesma re-verificação de exclusividade ao path de tecla única. A condição
`requireExcl && srcIsMulti` deve ser `requireExcl` (independente de multi ou single):

```cpp
// ExclusiveTracker.cpp — seção HasExclusivePending
if (HasExclusivePending(s, excl)) {
    const auto src = excl.pendingSrc[s];
    const bool stillDown = (src == PendingSrc::Kb) ? kbNow : gpNow;
    const bool srcIsMulti = ...;
    const bool simPatch = ...;

    // ↓ MUDANÇA: era `requireExcl && srcIsMulti`, agora aplica a single-key também
    if (requireExcl) {
        const bool stillExcl =
            (src == PendingSrc::Kb)
                ? ComboExclusiveNow(hk.kb, keys.kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)
                : ComboExclusiveNow(hk.gp, keys.gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
        if (!stillExcl) {
            ClearExclusivePending(s, ClearReason::Cancelled, excl, replay, retained, deferred);
            return false;
        }
    }

    if (srcIsMulti) {
        // ... lógica multi-key existente (fullComboSeen, etc.) ...
    } else {
        // ... lógica single-key existente ...
    }
}
```

### Efeito colateral a validar

O re-check agora cancela o pending se WASD é pressionado durante a janela de single-key.
Mas `ComboExclusiveNow` passa as funções `IsAllowedExtra_*` que já permitem WASD.
Verificar que pressionar WASD enquanto o hotkey está sendo segurado não cancela o pending.

Se necessário, o re-check para single-key pode ter uma janela de tolerância menor
(ex: só re-checar nos primeiros N ms do pending, não nos últimos antes do timeout).

## Patch relacionado: `pressBothAtSame`

O `pressBothAtSame` tem um problema similar mas distinto: a `simWindowActive` é aberta
quando a PRIMEIRA tecla de um combo multi-key é pressionada (`AnyComboKeyDown`). Se o
usuário pressiona a tecla do slot + uma tecla fora do combo ao mesmo tempo, a sim window
pode abrir com base na tecla do slot, e o sistema interpreta isso como o início de um
press válido. Investigar separadamente se `requireExclusive` resolver o B2 não cobrir
o `pressBothAtSame` também.

## Teste

1. Ativar `requireExclusiveHotkey` no config
2. Pressionar tecla do slot + outra tecla simultaneamente → **não deve ativar**
3. Segurar outra tecla, depois pressionar tecla do slot → **não deve ativar** (já funciona)
4. Pressionar apenas a tecla do slot → **deve ativar** (regredir não pode)
5. Pressionar WASD enquanto segura o hotkey (Hold mode) → **não deve cancelar**