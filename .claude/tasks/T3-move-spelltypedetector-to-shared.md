# T3 — Mover `SpellTypeDetector` para `Shared/`

## Violação
**Persistence → Inbound**

`src/Persistence/SpellSettingsDB.cpp` importa `Adapters/Inbound/SpellTypeDetector.h`.
Persistence é a camada mais baixa e não pode importar nada acima dela. Inbound é a camada mais alta.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `src/Persistence/SpellSettingsDB.cpp` | Violador — importa Adapters/Inbound/SpellTypeDetector.h |
| `include/Adapters/Inbound/SpellTypeDetector.h` | Origem — a mover para Shared/ |
| `src/Adapters/Inbound/SpellTypeDetector.cpp` (se existir) | Implementação — verificar dependências |

## Estratégia

`SpellTypeDetector` é uma utilidade de detecção de tipo de spell a partir de dados do jogo (`RE::`). Está em `Adapters/Inbound/` por razões históricas, mas conceitualmente é uma função pura de classificação — não é um hook nem um adapter. O lugar correto é `Shared/`.

## Passos

1. Verificar o conteúdo e dependências do header e implementação:
   ```
   grep -n "^#include" include/Adapters/Inbound/SpellTypeDetector.h
   grep -n "^#include" src/Adapters/Inbound/SpellTypeDetector.cpp  (se existir)
   ```

2. Confirmar que `SpellTypeDetector` não tem dependências de outras classes em `Adapters/Inbound/`.

3. Mover o header:
   - `include/Adapters/Inbound/SpellTypeDetector.h` → `include/Shared/SpellTypeDetector.h`

4. Se houver implementação `.cpp`, mover:
   - `src/Adapters/Inbound/SpellTypeDetector.cpp` → `src/Shared/SpellTypeDetector.cpp`

5. Atualizar `CMakeLists.txt` se necessário.

6. Atualizar consumidores:
   - `src/Persistence/SpellSettingsDB.cpp`: substituir por `#include "Shared/SpellTypeDetector.h"`
   - Buscar outros consumidores: `grep -rn "SpellTypeDetector" src/ include/`

7. Build — confirmar que não há erros.

## Resultado esperado

- `SpellSettingsDB.cpp` não importa mais nada de `Adapters/`
- `SpellTypeDetector` vive em `Shared/` e é acessível por todas as camadas