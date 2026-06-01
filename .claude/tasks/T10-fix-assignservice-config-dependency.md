# T10 — Remover dependências de Config em `AssignService`

## Violação
**Application → Config**

`src/Application/AssignService.cpp` importa `Config/Slots.h` e `Config/ConfigAdapter.h`.
Application só pode importar Domain e Shared. Acessar a camada Config diretamente acopla
a lógica de atribuição ao mecanismo de persistência de configuração.

## Mapeamento de uso

| Import | Onde é usado | Para quê |
|---|---|---|
| `Config/Slots.h` | TryAssignHoveredSpellToSlot, TryAssignHoveredShoutToSlot, TryClearSlotShout | SetSlotSpell, GetSlotSpell, SetSlotShout |
| `Config/ConfigAdapter.h` | TryClearSlotHand | `adapter.SetSpell()` + `adapter.Save()` |

## Inconsistência detectada

`TryClearSlotHand` usa `MagicConfigAdapter::SetSpell() + .Save()` enquanto as demais funções
usam `Slots::SetSlotSpell(..., saveNow=true)` — que faz exatamente o mesmo. O `ConfigAdapter`
aqui é um artefato legado.

## Estratégia

### Passo 1 — Eliminar `Config/ConfigAdapter.h` (trivial)

Substituir em `TryClearSlotHand`:
```cpp
// Antes:
auto& adapter = Config::MagicConfigAdapter::Get();
adapter.SetSpell(slot, hand == Hand::Left, 0u);
adapter.Save();

// Depois:
Slots::SetSlotSpell(slot, hand, 0u, true);
```

### Passo 2 — Eliminar `Config/Slots.h`

AssignService não deve escrever em Config diretamente. Em vez disso, as funções
devem **retornar intenções de mutação** e deixar o chamador aplicá-las.

Criar `include/Shared/SlotMutation.h` com um struct de intenção:
```cpp
namespace IntegratedMagic {
    struct SlotMutation {
        int slot{-1};
        std::optional<std::pair<Hand, RE::FormID>> spellChange;   // hand + novo formID (0 = limpar)
        std::optional<RE::FormID> shoutChange;                     // novo formID (0 = limpar)
    };
}
```

Mudar as assinaturas de AssignService para retornar o que fazer:
```cpp
// include/Application/AssignService.h
std::optional<SlotMutation> ComputeSpellAssignment(int slot, Hand hand,
    RE::FormID existingLeftID);  // caller lê existingLeftID de Config/Slots
std::optional<SlotMutation> ComputeShoutAssignment(int slot);
std::optional<SlotMutation> ComputeClearHand(int slot, Hand hand);
std::optional<SlotMutation> ComputeClearShout(int slot);
```

Os **chamadores** (HudController, SpellSystemController) lêem o estado atual de Config/Slots,
passam o necessário para AssignService, recebem a mutação e aplicam com `Slots::Set*`.

### Passos

1. Aplicar o Passo 1 (remover ConfigAdapter de TryClearSlotHand).
2. Criar `include/Shared/SlotMutation.h` com o struct.
3. Refatorar `include/Application/AssignService.h` — novas assinaturas que recebem dados
   e retornam `SlotMutation`.
4. Refatorar `src/Application/AssignService.cpp` — sem nenhum import de Config/.
5. Atualizar todos os chamadores:
   - `src/Application/HudController.cpp` — ler Config/Slots, passar para AssignService,
     aplicar mutação via Slots::Set*.
   - `src/Application/SpellSystemController.cpp` (TryAssignHoveredToSlotByHotkey) — idem.
6. Build — confirmar que não há erros.

## Resultado esperado

- `AssignService.cpp` não importa mais nada de `Config/`
- Lógica de decisão (qual formID atribuir, qual mão limpar) permanece em AssignService
- Persistência (Slots::Set*) executada pelos controladores Application que já têm Config

## Notas

- `SlotMutation` em Shared fica livre de dependências — só usa tipos primitivos e Hand
- Os chamadores já têm Config access (HudController, SpellSystemController); não introduz
  novas violações neles