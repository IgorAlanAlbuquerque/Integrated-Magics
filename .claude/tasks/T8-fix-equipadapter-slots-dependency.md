# T8 — Remover `EquipEventAdapter` → `Persistence/Slots` (depende de T5)

## Violação
**Inbound → Persistence** (antes de T5) / **Inbound → Config** (após T5)

`src/Adapters/Inbound/EquipEventAdapter.cpp` importa `Persistence/Slots.h`.
Inbound só pode importar Application. Ler dados de slot diretamente de Config/Persistence bypassa a camada Application.

Após T5 (Slots movido para Config/), a violação muda de Inbound → Persistence para Inbound → Config — continua proibida.

## Arquivos envolvidos

| Arquivo | Papel |
|---|---|
| `src/Adapters/Inbound/EquipEventAdapter.cpp` | Violador — importa Persistence/Slots.h (ou Config/Slots.h após T5) |
| `include/Application/SpellSystemController.h` | Ponto de entrada correto — a receber o novo método |

## Contexto

`EquipEventAdapter` responde a eventos de equip do jogo e precisa saber quais spells estão nos slots para tomar decisões (ex: verificar se o item equipado/desequipado é um dos slots ativos). Atualmente faz isso lendo `Slots` diretamente.

## Estratégia

`SpellSystemController` (Application) já conhece o estado dos slots (via MagicState e Config). Adicionar um método ou consulta em `SpellSystemController` que responde à pergunta que `EquipEventAdapter` precisa. O adapter chama Application, que lê o dado internamente.

## Passos

1. Verificar o que exatamente `EquipEventAdapter` usa de `Slots`:
   ```
   grep -n "Slots::" src/Adapters/Inbound/EquipEventAdapter.cpp
   ```

2. Identificar a pergunta que está sendo feita (ex: "O form X está atribuído a algum slot?" ou "Qual spell está no slot Y?").

3. Adicionar um método em `SpellSystemController` que responde essa pergunta:
   ```cpp
   // Exemplo — adaptar ao que EquipEventAdapter realmente precisa
   bool IsFormAssignedToAnySlot(RE::FormID formID) const;
   // ou
   std::optional<int> FindSlotForForm(RE::FormID formID) const;
   ```

4. Implementar o método em `src/Application/SpellSystemController.cpp` usando `Config/Slots.h` internamente (Application pode importar Config).

5. Atualizar `EquipEventAdapter.cpp`:
   - Substituir as chamadas diretas a `Slots::` pela chamada ao novo método de `SpellSystemController`
   - Remover `#include "Persistence/Slots.h"` (ou `Config/Slots.h` após T5)

6. Build — confirmar que não há erros.

## Pré-requisito

Esta task deve ser executada **após T5** (Slots movido para Config/), pois até lá o import é `Persistence/Slots.h`.

## Resultado esperado

- `EquipEventAdapter.cpp` não importa mais nada de `Persistence/` ou `Config/`
- Fluxo correto: `EquipEventAdapter (Inbound)` → `SpellSystemController (Application)` → `Config/Slots`