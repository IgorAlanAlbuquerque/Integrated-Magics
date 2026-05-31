# T6 — Inverter dependência `HoveredForm`

## Violação
**Application → Inbound**

`src/Application/HudController.cpp` e `src/Application/AssignService.cpp` importam `Adapters/Inbound/HoveredForm.h`.
Application não pode importar Inbound — o fluxo correto é Inbound chamar Application, nunca o inverso.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `src/Application/HudController.cpp` | Violador — importa Adapters/Inbound/HoveredForm.h |
| `src/Application/AssignService.cpp` | Violador — importa Adapters/Inbound/HoveredForm.h |
| `include/Adapters/Inbound/HoveredForm.h` | Inbound adapter que detecta spell hovered no Magic Menu |
| `src/Adapters/Inbound/HoveredForm.cpp` | Implementação do HoveredForm |

## Contexto atual

`HudController` e `AssignService` chamam `HoveredForm::GetHoveredMagicType()` ou similar para saber qual spell o jogador está hovering no Magic Menu. Isso cria um pull (Application puxa dado de Inbound) quando deveria ser um push (Inbound notifica Application).

## Estratégia

Inverter o fluxo: `HoveredForm` (Inbound) passa a notificar `HudController` e/ou `AssignService` (Application) com os dados do hover. Application expõe um método ou armazena o estado internamente, sem importar HoveredForm.

**Opção A — Notificação direta (mais simples):**
HoveredForm chama `HudController::SetHoveredMagicType(type)` a cada frame/evento. HudController armazena o valor e o usa internamente sem precisar importar HoveredForm.

**Opção B — Estado compartilhado em Shared (sem acoplamento):**
Criar `Shared/HoveredFormState.h` com um struct/atomic que HoveredForm escreve e Application lê. Nenhum dos dois importa o outro.

Opção A é mais simples e direta.

## Passos (Opção A)

1. Verificar o que exatamente HudController e AssignService usam de HoveredForm:
   ```
   grep -n "HoveredForm" src/Application/HudController.cpp src/Application/AssignService.cpp
   ```

2. Adicionar ao header `include/Application/HudController.h` (ou `AssignService.h`) um método público para receber o dado:
   ```cpp
   void SetHoveredMagicType(HoveredMagicType type);  // ou o tipo equivalente
   ```
   O tipo `HoveredMagicType` precisa ser em `Shared/` para que Inbound também possa referenciar.

3. Se `HoveredMagicType` (enum/struct) estiver definido em `HoveredForm.h`, movê-lo para `Shared/HoveredFormTypes.h` (ou integrar em `Shared/HudIntents.h` se já existir algo similar).

4. Implementar o método em `HudController.cpp`/`AssignService.cpp`:
   - Armazenar o valor em um campo privado (ex: `m_hoveredType`)
   - Substituir todas as chamadas a `HoveredForm::GetHoveredMagicType()` pelo campo interno

5. Atualizar `HoveredForm.cpp` para chamar `HudController::SetHoveredMagicType(...)` nos pontos onde o hover muda (em vez de só armazenar localmente).

6. Remover `#include "Adapters/Inbound/HoveredForm.h"` de HudController.cpp e AssignService.cpp.

7. Build — confirmar que não há erros.

## Resultado esperado

- `HudController.cpp` e `AssignService.cpp` não importam mais nada de `Adapters/Inbound/`
- Fluxo corrigido: `HoveredForm (Inbound)` → notifica → `HudController/AssignService (Application)`