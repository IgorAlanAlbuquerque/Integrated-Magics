# T9 — Desacoplar `InputFilter` de `Domain/State`

## Violação
**Input → Domain**

`src/Input/InputFilter.cpp` importa `Domain/State.h`.
Input só pode importar Config e Shared. Importar Domain cria dependência proibida entre dois pacotes do mesmo nível.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `src/Input/InputFilter.cpp` | Violador — importa Domain/State.h |
| `include/Domain/State.h` | Fonte — MagicState singleton |
| `include/Application/InputController.h` | Ponto correto para passar o estado via parâmetro |

## Contexto

`InputFilter` precisa de algum dado de `MagicState` para decidir se deve bloquear ou filtrar eventos de input (ex: não processar hotkeys enquanto o sistema de magia está em setup, ou enquanto um slot está ativo em Press mode).

## Passos

1. Identificar exatamente o que `InputFilter` usa de `Domain/State.h`:
   ```
   grep -n "MagicState\|IsActive\|IsInSlotSetup\|IsPressMode\|_session\|_left\|_right" src/Input/InputFilter.cpp
   ```

2. Com base no resultado, escolher a abordagem:

   **Opção A — Parâmetro direto (preferida se for 1-2 valores simples):**
   Adicionar parâmetros `bool isActive, bool isInSlotSetup` (ou equivalente) às funções de `InputFilter` que precisam desse estado. `InputController` (Application) lê de `MagicState` e passa para `InputFilter`.

   **Opção B — Struct em Shared (preferida se forem vários campos):**
   Criar `Shared/MagicSystemStatus.h` com um struct simples:
   ```cpp
   struct MagicSystemStatus {
       bool active{false};
       bool inSlotSetup{false};
       bool pressMode{false};
   };
   ```
   `InputController` preenche e passa para `InputFilter`. `InputFilter` importa apenas `Shared/`.

3. Atualizar a assinatura das funções relevantes em `include/Input/InputFilter.h` para receber o dado via parâmetro (ou o struct de Shared).

4. Atualizar `src/Input/InputFilter.cpp`:
   - Remover `#include "Domain/State.h"`
   - Substituir chamadas diretas a `MagicState::Get()` pelo parâmetro recebido

5. Atualizar `src/Application/InputController.cpp` (que chama as funções de InputFilter):
   - Ler o estado necessário de `MagicState::Get()` antes de chamar InputFilter
   - Passar como parâmetro

6. Build — confirmar que não há erros.

## Observação sobre thread safety

`MagicState` não é thread-safe, mas `InputController::ProcessAndFilter` roda na main thread do jogo (mesma thread que atualiza MagicState). Não há risco de race condition nessa passagem de parâmetro.

## Resultado esperado

- `InputFilter.cpp` não importa mais nada de `Domain/`
- O estado necessário chega via parâmetro, injetado por `InputController` (Application)
- Fluxo correto: Application lê Domain e repassa o dado necessário para Input