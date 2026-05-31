# T7 — Rotear D3D/DXGI/WndProc através de `HudController`

## Violação
**Inbound → UI (bypassando Application)**

Três hooks de Inbound importam diretamente camadas de UI:

| Arquivo | Import proibido |
|---|---|
| `src/Adapters/Inbound/D3DInitHook.cpp` | `UI/FontLoader.h`, `UI/TextureManager.h` |
| `src/Adapters/Inbound/DXGIPresentHook.cpp` | `UI/HudManager.h`, `UI/HudState.h` |
| `src/Adapters/Inbound/WndProcHook.cpp` | `UI/HudManager.h` |

Inbound só pode importar Application. Toda comunicação com UI deve fluir por `HudController`.

## Estratégia

Adicionar métodos em `HudController` que encapsulam as operações de UI que esses hooks precisam. Os hooks passam a chamar `HudController` (Application) — que já podem importar — em vez de `UI/` diretamente.

## Novos métodos a adicionar em `HudController`

```cpp
// include/Application/HudController.h
class HudController {
public:
    // ...métodos existentes...
    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context, HWND hwnd);
    void RenderFrame();
    LRESULT OnWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
```

## Passos

### D3DInitHook.cpp

1. Verificar o que D3DInitHook faz com `FontLoader` e `TextureManager`:
   ```
   grep -n "FontLoader\|TextureManager" src/Adapters/Inbound/D3DInitHook.cpp
   ```

2. Mover essas chamadas de inicialização para dentro de `HudController::Initialize()` em `src/Application/HudController.cpp`.

3. Em `D3DInitHook.cpp`, substituir as chamadas diretas por `HudController::Get().Initialize(device, context, hwnd)`.

4. Remover `#include "UI/FontLoader.h"` e `#include "UI/TextureManager.h"` de `D3DInitHook.cpp`.

### DXGIPresentHook.cpp

1. Verificar o que DXGIPresentHook faz com `HudManager` e `HudState`:
   ```
   grep -n "HudManager\|HudState" src/Adapters/Inbound/DXGIPresentHook.cpp
   ```

2. Mover a chamada de render (ex: `HudManager::DrawHudFrame()`) para dentro de `HudController::RenderFrame()`.

3. Em `DXGIPresentHook.cpp`, substituir por `HudController::Get().RenderFrame()`.

4. Remover `#include "UI/HudManager.h"` e `#include "UI/HudState.h"` de `DXGIPresentHook.cpp`.

### WndProcHook.cpp

1. Verificar o que WndProcHook faz com `HudManager`:
   ```
   grep -n "HudManager" src/Adapters/Inbound/WndProcHook.cpp
   ```

2. Mover a lógica de passagem de mensagem para dentro de `HudController::OnWindowMessage()`.

3. Em `WndProcHook.cpp`, substituir por `HudController::Get().OnWindowMessage(hwnd, msg, wParam, lParam)`.

4. Remover `#include "UI/HudManager.h"` de `WndProcHook.cpp`.

### Finalização

5. Build — confirmar que não há erros.

## Resultado esperado

- `D3DInitHook`, `DXGIPresentHook` e `WndProcHook` não importam mais nada de `UI/`
- `HudController` é o único ponto de entrada de Application para UI nos fluxos de render/init/wndproc
- Fluxo correto: `Inbound → Application (HudController) → UI`