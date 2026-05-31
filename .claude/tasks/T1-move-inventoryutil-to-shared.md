# T1 — Mover `InventoryUtil` para `Shared/`

## Violação
**Outbound → Domain**

`include/Adapters/Outbound/RestoreEquip.h` importa `Domain/InventoryUtil.h`.
A camada Outbound só pode importar `Shared/`. Importar `Domain/` cria dependência proibida.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `include/Adapters/Outbound/RestoreEquip.h` | Violador — importa Domain/InventoryUtil.h |
| `include/Domain/InventoryUtil.h` | Origem dos tipos a serem movidos |

## Estratégia

Os tipos de `InventoryUtil` usados por `RestoreEquip` (structs de snapshot de inventário como `ObjSnapshot`, `ExtraEquippedItem`, `InventoryIndex`) são tipos de valor sem lógica de negócio — pertencem a `Shared/`. A implementação de funções que dependem de `RE::PlayerCharacter` continua em `Domain/`.

## Passos

1. Identificar quais tipos de `Domain/InventoryUtil.h` são usados por `RestoreEquip.h`:
   ```
   grep -n "InventoryUtil\|ObjSnapshot\|ExtraEquipped\|InventoryIndex" include/Adapters/Outbound/RestoreEquip.h
   ```

2. Criar `include/Shared/InventoryUtil.h` com apenas os tipos necessários (structs, enums, aliases) — sem dependências de `RE::` ou de outros headers de Domain.

3. Atualizar `include/Domain/InventoryUtil.h` para importar `Shared/InventoryUtil.h` (Domain pode importar Shared). Remover as definições duplicadas que foram movidas.

4. Atualizar `include/Adapters/Outbound/RestoreEquip.h`:
   - Substituir `#include "Domain/InventoryUtil.h"` por `#include "Shared/InventoryUtil.h"`

5. Verificar que todos os outros arquivos que já importavam `Domain/InventoryUtil.h` continuam compilando (recebem os tipos via re-export).

6. Build — confirmar que não há erros de compilação.

## Resultado esperado

- `RestoreEquip.h` importa apenas `Shared/`
- `Domain/InventoryUtil.h` re-exporta os tipos de `Shared/InventoryUtil.h`
- Nenhum outro arquivo precisa mudar seus imports