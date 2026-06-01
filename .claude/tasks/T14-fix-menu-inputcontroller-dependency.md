# T14 — Remover dependência de `InputController` em `MENU.cpp`

## Violação

**UI → Application**

`src/UI/MENU.cpp` importa `Application/InputController.h`.
UI só pode importar UI/, Config/, Shared/ — e possivelmente Input/ (que é uma camada abaixo de Application).

Esta violação cria uma dependência **circular de camada**:
- `HudController.cpp` (Application) → `HudFrameLogic.h` (UI) — fluxo correto
- `MENU.cpp` (UI) → `InputController.h` (Application) — viola a separação

## Mapeamento de uso

| Método chamado | Onde em MENU.cpp | O que faz |
|---|---|---|
| `RequestHotkeyCapture()` | `DrawKeyValueCapture` (linhas 110–111) | `captureRequested=true`, `capturedEncoded=-1` |
| `CancelHotkeyCapture()` | `CancelFieldCapture` (linhas 68–69) | `captureRequested=false`, `capturedEncoded=-1` |
| `PollCapturedHotkey()` | `DrawKeyValueCapture` (linha 87) | Lê e limpa `capturedEncoded` |
| `SetCaptureModeActive(bool)` | Linhas 69, 94, 97, 111 | Seta `m_captureModeActive` |
| `OnConfigChanged()` | `DrawSettings` (linha 1417) | Recarrega cache de hotkeys |

## Por que InputController não é a camada certa para este estado

`CaptureState` é **estado de interação de UI** — o usuário rebinda teclas num menu.
Que um controlador de Application seja o guardião desse estado é um acidente histórico.
O struct `CaptureState` já existe em `include/Input/CaptureState.h` (Input layer) e contém apenas `atomic_bool captureRequested` e `atomic_int capturedEncoded`. É o lugar natural para este estado.

## Estratégia

### Parte A — Elevar `CaptureState` para singleton no Input layer

#### 1. Ampliar `include/Input/CaptureState.h`

```cpp
#pragma once
#include <atomic>

struct CaptureState {
    std::atomic_bool captureRequested{false};
    std::atomic_int  capturedEncoded{-1};
    std::atomic_bool captureActive{false};    // ← novo: substitui m_captureModeActive

    static CaptureState& Get();

    void Request() noexcept {
        capturedEncoded.store(-1, std::memory_order_relaxed);
        captureRequested.store(true, std::memory_order_relaxed);
    }

    void Cancel() noexcept {
        captureRequested.store(false, std::memory_order_relaxed);
        capturedEncoded.store(-1, std::memory_order_relaxed);
    }

    // Retorna o código capturado e limpa; -1 se nada capturado ainda
    [[nodiscard]] int Poll() noexcept {
        const int v = capturedEncoded.load(std::memory_order_relaxed);
        if (v == -1) return -1;
        capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }
};
```

Implementação de `Get()` em novo arquivo `src/Input/CaptureState.cpp`:
```cpp
#include "Input/CaptureState.h"

CaptureState& CaptureState::Get() {
    static CaptureState inst;
    return inst;
}
```

#### 2. Atualizar `InputController`

**`include/Application/InputController.h`**: remover campos:
```cpp
// removidos:
CaptureState m_captureState{};
bool m_captureModeActive{false};
```

**`src/Application/InputController.cpp`**: todos os acessos a `m_captureState` e `m_captureModeActive` passam a usar `CaptureState::Get()`. Os métodos delegadores (`RequestHotkeyCapture`, `CancelHotkeyCapture`, `PollCapturedHotkey`, `SetCaptureModeActive`, `IsCaptureModeActive`, `InjectCapturedScancode`, `InjectCapturedGamepad`) são mantidos no header para que WndProcHook e outros callers do Application layer continuem funcionando — eles apenas delegam para o singleton.

Atenção: `Input::detail::ProcessButtonEvents(a_evns, m_captureState, wantCapture, m_keys)` — o parâmetro `m_captureState` passa a ser `CaptureState::Get()`.

#### 3. Atualizar `MENU.cpp`

Substituições diretas:

| Antes | Depois |
|---|---|
| `Application::InputController::Get().RequestHotkeyCapture()` | `CaptureState::Get().Request()` |
| `Application::InputController::Get().CancelHotkeyCapture()` | `CaptureState::Get().Cancel()` |
| `Application::InputController::Get().PollCapturedHotkey()` | `CaptureState::Get().Poll()` |
| `Application::InputController::Get().SetCaptureModeActive(false)` | `CaptureState::Get().captureActive.store(false, ...)` |
| `Application::InputController::Get().SetCaptureModeActive(true)` | `CaptureState::Get().captureActive.store(true, ...)` |

MENU.cpp troca `#include "Application/InputController.h"` por `#include "Input/CaptureState.h"`.

---

### Parte B — Eliminar `OnConfigChanged()` de MENU.cpp

#### Contexto

`DrawSettings()` em MENU.cpp (linha 1417):
```cpp
Application::InputController::Get().OnConfigChanged();
```

`InputController::OnConfigChanged()` faz:
```cpp
Input::detail::LoadHotkeyCache_FromConfig(m_hotkeys, m_slots);
Input::detail::LoadModifierBinding_FromConfig(m_modifierKbCode, m_modifierGpCode);
Input::detail::ResetExclusiveState(m_slots, m_exclusive, m_replay, m_retained, m_deferred);
```

Estas funções leem/escrevem nos campos privados de `InputController`. Não podem ser chamadas diretamente de fora. A solução é um **flag deferred** no Input layer.

#### Implementação

Em `include/Input/HotkeyMatcher.h`, adicionar:
```cpp
void RequestHotkeyReload();      // chamado por MENU
bool ConsumeHotkeyReloadRequest(); // chamado por InputController em ProcessAndFilter
```

Em `src/Input/HotkeyMatcher.cpp`, implementar com um `std::atomic<bool>`:
```cpp
namespace {
    std::atomic<bool> g_hotkeyReloadRequested{false};
}

void Input::detail::RequestHotkeyReload() {
    g_hotkeyReloadRequested.store(true, std::memory_order_relaxed);
}

bool Input::detail::ConsumeHotkeyReloadRequest() {
    return g_hotkeyReloadRequested.exchange(false, std::memory_order_relaxed);
}
```

Em `InputController::ProcessAndFilter`, logo após o bloco `m_cacheInitialized`:
```cpp
if (Input::detail::ConsumeHotkeyReloadRequest()) {
    Input::detail::LoadHotkeyCache_FromConfig(m_hotkeys, m_slots);
    Input::detail::LoadModifierBinding_FromConfig(m_modifierKbCode, m_modifierGpCode);
    Input::detail::ResetExclusiveState(m_slots, m_exclusive, m_replay, m_retained, m_deferred);
}
```

Em MENU.cpp, substituir:
```cpp
// antes:
Application::InputController::Get().OnConfigChanged();
// depois:
Input::detail::RequestHotkeyReload();
```

E adicionar `#include "Input/HotkeyMatcher.h"` (pode já estar indiretamente via CaptureState, mas incluir explicitamente).

#### Timing

O reload ocorre na próxima chamada de `ProcessAndFilter` (hook de input), que é diferente do hook de render (onde MENU roda via DXGI Present). O atraso é de ≤1 frame — imperceptível para o usuário.

Os outros callers de `OnConfigChanged` (`plugin.cpp`, `SpellSystemController`) continuam chamando diretamente e recebem o reload síncrono — comportamento inalterado.

---

## Passos de implementação

1. Ampliar `include/Input/CaptureState.h` — adicionar `captureActive`, `Get()`, `Request()`, `Cancel()`, `Poll()`.
2. Criar `src/Input/CaptureState.cpp` — implementar `Get()`.
3. Atualizar `include/Application/InputController.h` — remover `m_captureState` e `m_captureModeActive`. Os métodos públicos de delegação (`RequestHotkeyCapture`, etc.) podem ser mantidos ou tornar-se inline que delegam para `CaptureState::Get()`.
4. Atualizar `src/Application/InputController.cpp` — todos os acessos a `m_captureState`/`m_captureModeActive` usam `CaptureState::Get()` e `.captureActive`.
5. Adicionar `RequestHotkeyReload()` / `ConsumeHotkeyReloadRequest()` em `include/Input/HotkeyMatcher.h` e `src/Input/HotkeyMatcher.cpp`.
6. Em `InputController::ProcessAndFilter`, checar `ConsumeHotkeyReloadRequest()` e disparar reload.
7. Atualizar `MENU.cpp` — trocar todos os `InputController::Get().X()` pelas chamadas diretas ao Input layer; remover `#include "Application/InputController.h"`.
8. Build — confirmar sem erros.

## Resultado esperado

- `MENU.cpp` importa apenas `Input/CaptureState.h`, `Input/HotkeyMatcher.h`, Config/, UI/ — nenhum Application.
- A dependência circular de camada UI↔Application é eliminada.
- `CaptureState` vive no Input layer como singleton — acessível por qualquer camada acima sem passar por Application.
- `InputController` mantém seus métodos públicos de delegação para os callers existentes (WndProcHook, plugin.cpp) sem alteração de API pública.