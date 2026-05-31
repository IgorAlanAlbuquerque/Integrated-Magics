# T2 — Mover `SpellClassify` para `Shared/`

## Violação
**Inbound → Domain**

`src/Adapters/Inbound/HoveredForm.cpp` importa `Domain/SpellClassify.h`.
Inbound só pode importar Application. Importar Domain diretamente é proibido.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `src/Adapters/Inbound/HoveredForm.cpp` | Violador — importa Domain/SpellClassify.h |
| `include/Domain/SpellClassify.h` | Origem — a mover para Shared/ |
| `src/Domain/SpellClassify.cpp` (se existir) | Implementação — verificar se tem deps de RE:: |
| `src/Application/AssignService.cpp` | Consumidor — importa Domain/SpellClassify.h (import continua válido via Shared) |

## Estratégia

`SpellClassify` é uma utilidade de classificação de spells (determina tipo: Bound, Concentration, Power etc.). Não tem estado próprio e sua única dependência externa é a API RE::. Como é usada por Inbound (que não pode importar Domain) e por Application (que pode), o lugar correto é `Shared/`.

## Passos

1. Verificar o conteúdo de `include/Domain/SpellClassify.h` e checar se `SpellClassify.cpp` usa apenas `RE::` (externo) e `Shared/`:
   ```
   grep -n "^#include" include/Domain/SpellClassify.h src/Domain/SpellClassify.cpp
   ```

2. Mover o header:
   - `include/Domain/SpellClassify.h` → `include/Shared/SpellClassify.h`

3. Se existir `src/Domain/SpellClassify.cpp`, mover para `src/Shared/SpellClassify.cpp` (ou manter em Domain se tiver dependências de Domain internas — nesse caso só o header vai para Shared e o .cpp continua onde está com include do novo path).

4. Atualizar `CMakeLists.txt` se os arquivos `.cpp` forem movidos (novo path nos sources).

5. Atualizar todos os consumidores:
   - `src/Adapters/Inbound/HoveredForm.cpp`: substituir por `#include "Shared/SpellClassify.h"`
   - `src/Application/AssignService.cpp`: substituir por `#include "Shared/SpellClassify.h"`
   - Qualquer outro arquivo que importe `Domain/SpellClassify.h`

6. Se preferir manter compatibilidade temporária, deixar um `include/Domain/SpellClassify.h` stub que só faz `#include "Shared/SpellClassify.h"` — remove depois.

7. Build — confirmar que não há erros.

## Resultado esperado

- `HoveredForm.cpp` não importa mais nada de `Domain/`
- `SpellClassify` vive em `Shared/` e é acessível por todas as camadas