# T13 — Remover dependência de Config em `InputController`

## Violação
**Application → Config**

`src/Application/InputController.cpp` importa `Config/ConfigAdapter.h`.
Application só pode importar Domain e Shared.

## Mapeamento de uso

| Import | Onde é usado | Para quê |
|---|---|---|
| `Config/ConfigAdapter.h` | `IsModifierHeld()` | `ModifierKbPosition()`, `ModifierGpPosition()`, `GetSlotBinding(0)` |

### Código atual de `IsModifierHeld()`:

```cpp
bool InputController::IsModifierHeld() {
    const auto& bindings = IntegratedMagic::Config::MagicConfigAdapter::Get();
    const int kbPos = bindings.ModifierKbPosition();
    const int gpPos = bindings.ModifierGpPosition();

    if (kbPos > 0) {
        const auto binding = bindings.GetSlotBinding(0);
        const int code = kbPos == 1 ? binding.kb[0] : kbPos == 2 ? binding.kb[1] : binding.kb[2];
        if (code >= 0 && code < kMaxCode && m_keys.kbDown[code].load(...)) return true;
    }
    if (gpPos > 0) {
        const auto binding = bindings.GetSlotBinding(0);
        const int code = gpPos == 1 ? binding.gp[0] : gpPos == 2 ? binding.gp[1] : binding.gp[2];
        if (code >= 0 && code < kMaxCode && m_keys.gpDown[code].load(...)) return true;
    }
    return false;
}
```

`IsModifierHeld()` lê Config ao vivo a cada frame, mas o modifier binding muda apenas
quando a configuração é recarregada — via `OnConfigChanged()`.

## Estratégia: cache local na inicialização e no `OnConfigChanged()`

InputController já possui `OnConfigChanged()` que chama `LoadHotkeyCache_FromConfig()`.
Estender para também cachear o modifier binding:

### Adicionar campos privados em InputController:

```cpp
// include/Application/InputController.h
private:
    int m_modifierKbCode{-1};
    int m_modifierGpCode{-1};
```

### Extrair o load do modifier binding:

```cpp
// src/Application/InputController.cpp
void InputController::LoadModifierBinding() {
    const auto& cfg = IntegratedMagic::Config::MagicConfigAdapter::Get();
    const int kbPos = cfg.ModifierKbPosition();
    const int gpPos = cfg.ModifierGpPosition();
    const auto binding = cfg.GetSlotBinding(0);

    m_modifierKbCode = (kbPos > 0) ?
        (kbPos == 1 ? binding.kb[0] : kbPos == 2 ? binding.kb[1] : binding.kb[2]) : -1;
    m_modifierGpCode = (gpPos > 0) ?
        (gpPos == 1 ? binding.gp[0] : gpPos == 2 ? binding.gp[1] : binding.gp[2]) : -1;
}
```

Chamar `LoadModifierBinding()` dentro de `OnConfigChanged()` e também na primeira chamada
(flag `m_cacheInitialized` já existe — pode ser reutilizada ou criar flag separada).

### `IsModifierHeld()` simplificado (sem Config):

```cpp
bool InputController::IsModifierHeld() {
    if (m_modifierKbCode >= 0 && m_modifierKbCode < kMaxCode &&
        m_keys.kbDown[static_cast<std::size_t>(m_modifierKbCode)].load(std::memory_order_relaxed))
        return true;
    if (m_modifierGpCode >= 0 && m_modifierGpCode < kMaxCode &&
        m_keys.gpDown[static_cast<std::size_t>(m_modifierGpCode)].load(std::memory_order_relaxed))
        return true;
    return false;
}
```

### Passos

1. Adicionar `m_modifierKbCode` e `m_modifierGpCode` como campos privados em
   `include/Application/InputController.h`.
2. Implementar `LoadModifierBinding()` privado em `src/Application/InputController.cpp`
   (pode importar Config/ConfigAdapter apenas para este helper de inicialização — mas o
   objetivo é remover o import completamente).
3. Chamar `LoadModifierBinding()` dentro de `OnConfigChanged()`.
4. Garantir que `LoadModifierBinding()` seja chamado antes de `IsModifierHeld()` ser
   invocado pela primeira vez (na primeira passagem de `ProcessAndFilter`, onde
   `m_cacheInitialized` é verificado).
5. Simplificar `IsModifierHeld()` para usar apenas `m_modifierKbCode`/`m_modifierGpCode`.
6. Remover `#include "Config/ConfigAdapter.h"` de `InputController.cpp`.
7. Build — confirmar que não há erros.

## Observação: `LoadModifierBinding()` ainda lê Config

O helper `LoadModifierBinding()` chama `MagicConfigAdapter::Get()`, mas é chamado apenas
em `OnConfigChanged()` e na inicialização — não a cada frame. Se o objetivo for que
**nenhum** arquivo de Application importe Config, então `LoadModifierBinding()` precisa
receber os dados como parâmetro ou usar um Shared cache.

Alternativa cleaner: `OnConfigChanged()` em SpellSystemController passa o modifier binding
para InputController:
```cpp
void SpellSystemController::OnConfigChanged() const {
    const auto& cfg = MagicConfigAdapter::Get();
    const int kbPos = cfg.ModifierKbPosition();
    const int gpPos = cfg.ModifierGpPosition();
    const auto bind0 = cfg.GetSlotBinding(0);
    InputController::Get().OnConfigChanged(kbPos, gpPos, bind0);
}
```

SpellSystemController já importa Config (T12 ainda não foi implementado neste ponto) —
portanto pode servir como o único ponto de leitura de Config para esta informação.
Após T12, essa responsabilidade migra para um Shared cache.

## Resultado esperado

- `InputController.cpp` não importa mais nada de `Config/`
- `IsModifierHeld()` lê apenas campos locais — O(1), zero acesso externo por frame
- Modifier binding atualizado a cada `OnConfigChanged()` — consistente com hotkey cache