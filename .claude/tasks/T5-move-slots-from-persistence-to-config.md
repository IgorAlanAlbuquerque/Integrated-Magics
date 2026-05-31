# T5 — Mover `Slots` de `Persistence/` para `Config/`

## Violações corrigidas (6 arquivos)
**Domain → Persistence** e **Application → Persistence**

Domain e Application importam `Persistence/Slots.h` diretamente:
- Domain só pode importar Config e Shared
- Application só pode importar Input, Domain, UI e Outbound

`Slots` é uma API de leitura/escrita de quais spells estão em quais slots — é dado de configuração que persiste, não lógica de persistência pura. O lugar correto é `Config/`.

## Arquivos violadores

| Arquivo | Camada | Violação |
|---|---|---|
| `src/Domain/MagicStatePump.cpp` | Domain | Domain → Persistence |
| `src/Domain/MagicStateSlot.cpp` | Domain | Domain → Persistence |
| `src/Domain/SlotCooldownTracker.cpp` | Domain | Domain → Persistence |
| `src/Domain/SlotCostUtil.cpp` | Domain | Domain → Persistence |
| `src/Application/HudController.cpp` | Application | Application → Persistence |
| `src/Application/AssignService.cpp` | Application | Application → Persistence |
| `src/Persistence/Slots.cpp` | Persistence | Persistence → Config (Slots.cpp importa Config/ConfigAdapter.h — vira interno ao Config após a mudança) |

## Estratégia

Mover `Slots.h/.cpp` para `Config/`. Isso é consistente com o restante do Config layer (que já usa `ConfigAdapter` e lê/escreve dados de slot). A dependência `Slots.cpp → Config/ConfigAdapter.h` deixa de ser uma violação (passa a ser intra-pacote).

## Passos

1. Mover os arquivos:
   - `include/Persistence/Slots.h` → `include/Config/Slots.h`
   - `src/Persistence/Slots.cpp` → `src/Config/Slots.cpp`

2. Atualizar o include guard / pragma once em `include/Config/Slots.h` (sem mudança necessária se usar `#pragma once`).

3. Atualizar o include dentro de `src/Config/Slots.cpp`:
   - `#include "Persistence/Slots.h"` → `#include "Config/Slots.h"`
   - `#include "Config/ConfigAdapter.h"` permanece (agora é intra-pacote — sem violação)

4. Atualizar `CMakeLists.txt`:
   - Remover `src/Persistence/Slots.cpp` dos sources
   - Adicionar `src/Config/Slots.cpp`

5. Atualizar todos os consumidores (substituir `#include "Persistence/Slots.h"` por `#include "Config/Slots.h"`):
   - `src/Domain/MagicStatePump.cpp`
   - `src/Domain/MagicStateSlot.cpp`
   - `src/Domain/SlotCooldownTracker.cpp`
   - `src/Domain/SlotCostUtil.cpp`
   - `src/Application/HudController.cpp`
   - `src/Application/AssignService.cpp`
   - Buscar outros: `grep -rn "Persistence/Slots.h" src/ include/`

6. Verificar `include/Persistence/Slots.h` — se outros arquivos fora da lista o importam, adicionar um stub temporário que redireciona para `Config/Slots.h`.

7. Build — confirmar que não há erros.

## Atenção: T8 depende desta task

Após esta task, `src/Adapters/Inbound/EquipEventAdapter.cpp` passa de **Inbound → Persistence** para **Inbound → Config** — continua sendo uma violação (Inbound não pode importar Config). A T8 corrige isso.

## Resultado esperado

- `Slots.h/.cpp` vivem em `Config/`
- Domain importa `Config/Slots.h` — dentro das regras
- Application importa `Config/Slots.h` — dentro das regras
- `Slots.cpp → Config/ConfigAdapter.h` é intra-pacote — sem violação