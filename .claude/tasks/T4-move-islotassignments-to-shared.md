# T4 — Mover `ISlotAssignments` para `Shared/`

## Violação
**Persistence → Config**

`include/Persistence/SpellSettingsDB.h` importa `Config/Ports/SlotAssignments.h` para usar a interface `ISlotAssignments`.
Persistence é a camada mais baixa e não pode importar Config.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `include/Persistence/SpellSettingsDB.h` | Violador — importa Config/Ports/SlotAssignments.h |
| `include/Config/Ports/SlotAssignments.h` | Origem da interface `ISlotAssignments` |
| `src/Persistence/SpellSettingsDB.cpp` | Implementação — usa ISlotAssignments como parâmetro |

## Contexto

`ISlotAssignments` é uma interface abstrata pura (virtual) declarada no namespace `IntegratedMagic::Config`. Ela só depende de `Shared/SpellType.h`. Como Persistence precisa dela mas não pode importar Config, mover a interface para `Shared/` é a correção natural.

```cpp
// Config/Ports/SlotAssignments.h (atual)
class ISlotAssignments {
    virtual int SlotCount() const = 0;
    virtual uint32_t GetSpell(int slot, bool leftHand) const = 0;
    // ...
};
```

## Passos

1. Criar `include/Shared/ISlotAssignments.h` com o conteúdo da interface:
   - Copiar a declaração de `ISlotAssignments` de `Config/Ports/SlotAssignments.h`
   - Manter o namespace `IntegratedMagic::Config` (ou mover para `IntegratedMagic` se preferir — avaliar impacto)
   - O único include necessário é `Shared/SpellType.h`

2. Atualizar `include/Config/Ports/SlotAssignments.h`:
   - Substituir a definição da interface por `#include "Shared/ISlotAssignments.h"`
   - Manter qualquer outra coisa que esteja no arquivo (structs, implementações concretas)

3. Atualizar `include/Persistence/SpellSettingsDB.h`:
   - Substituir `#include "Config/Ports/SlotAssignments.h"` por `#include "Shared/ISlotAssignments.h"`

4. Verificar que `SpellSettingsDB.cpp` compila sem mudanças (já inclui o header via `.h`).

5. Buscar outros consumidores de `Config/Ports/SlotAssignments.h` que usem `ISlotAssignments`:
   ```
   grep -rn "ISlotAssignments\|SlotAssignments.h" src/ include/
   ```
   Eles podem continuar importando `Config/Ports/SlotAssignments.h` (que agora re-exporta de Shared) — sem mudança necessária.

6. Build — confirmar que não há erros.

## Resultado esperado

- `SpellSettingsDB.h` não importa mais nada de `Config/`
- `ISlotAssignments` vive em `Shared/ISlotAssignments.h`
- `Config/Ports/SlotAssignments.h` re-exporta via include de Shared — zero quebra para consumidores existentes