# T11 — Remover dependências de Config em `HudController`

## Violação
**Application → Config**

`src/Application/HudController.cpp` importa `Config/Slots.h`, `Config/ConfigAdapter.h` e
`Config/StyleConfig.h`. HudController é Application — deve depender apenas de Domain e Shared.

## Mapeamento de uso

| Import | Onde é usado | Para quê |
|---|---|---|
| `Config/Slots.h` | OnFrame — loop de slots | GetSlotCount, GetSlotSpell(i, hand), GetSlotShout(i) para cada slot |
| `Config/Slots.h` | OnFrame — ClearSlot intent | GetSlotShout, GetSlotSpell para determinar o que limpar |
| `Config/ConfigAdapter.h` | EvaluateHudVisibility | FlagSet(visibility flags) |
| `Config/ConfigAdapter.h` | OnFrame — ClosePopup intent | FlushSpellSettingsIfDirty() |
| `Config/ConfigAdapter.h` | OnFrame — build HudView | ModifierKbPosition, ModifierGpPosition, GetSlotBinding(0), GetSlotBinding(i) |
| `Config/StyleConfig.h` | InitializeGraphics | font.path, font.size, font.range* |

## Estratégia: duas frentes

### Frente 1 — `Config/StyleConfig.h` (InitializeGraphics — chamada única)

`InitializeGraphics()` recebe os dados de fonte como parâmetro ao invés de ler Config:

```cpp
// include/Application/HudController.h
struct FontConfig {
    std::string path;
    float size{0.f};
    bool rangePolish{false};
    bool rangeCyrillic{false};
    bool rangeJapanese{false};
    bool rangeChineseSimplified{false};
    bool rangeKorean{false};
    bool rangeGreek{false};
};
void InitializeGraphics(const FontConfig& font);
```

`D3DInitHook.cpp` (Inbound) lê `StyleConfig::Get().font` e monta o struct antes de chamar
`HudController::Get().InitializeGraphics(fontCfg)`. D3DInitHook pode importar Config/StyleConfig
pois já importa outros headers de setup. Remove `Config/StyleConfig.h` de HudController.

### Frente 2 — `Config/Slots.h` e `Config/ConfigAdapter.h` (OnFrame — cada frame)

Criar um cache Shared populado por Config quando os dados mudam, lido por HudController
a cada frame — padrão idêntico ao T6 (HoveredFormState).

#### 2a — Dados de slot: criar `Shared/SlotDataCache.h`

```cpp
// include/Shared/SlotDataCache.h
namespace IntegratedMagic::SlotData {
    struct SlotEntry {
        RE::FormID leftSpell{0};
        RE::FormID rightSpell{0};
        RE::FormID shout{0};
    };
    constexpr int kMaxCachedSlots = 64;

    void Refresh();  // chamado por Config quando dados mudam
    int GetSlotCount();
    SlotEntry GetSlot(int i);
}
```

`src/Shared/SlotDataCache.cpp` — lê Config/Slots e armazena em array de atomics/mutex.
`Config/ConfigAdapter` (ou onde Slots::Set* é chamado) invoca `SlotData::Refresh()` após
qualquer mutação de slot. HudController importa apenas `Shared/SlotDataCache.h`.

#### 2b — Config HUD settings: criar `Shared/HudConfigCache.h`

```cpp
// include/Shared/HudConfigCache.h
namespace IntegratedMagic::HudConfig {
    struct BindingEntry { int kb[3]; int gp[3]; };

    void Refresh();  // chamado quando config muda
    int ModifierKbPosition();
    int ModifierGpPosition();
    BindingEntry GetSlotBinding(int slot);
    bool FlagSet(/* HudVisibilityFlag */ int flag);

    // Flush de spell settings — chamado por HudController no ClosePopup
    void FlushSpellSettingsIfDirty();
}
```

`src/Shared/HudConfigCache.cpp` — delega para `Config::MagicConfigAdapter`.

Quando `OnConfigChanged()` é chamado no Application, ele invoca `HudConfig::Refresh()` e
`SlotData::Refresh()` para manter os caches atualizados.

### Passos

1. Adicionar `FontConfig` struct ao header de HudController; atualizar `InitializeGraphics()`.
2. Em `D3DInitHook.cpp`: ler `StyleConfig::Get().font`, montar `FontConfig`, passar para
   `InitializeGraphics()`. Remover `Config/StyleConfig.h` de HudController.
3. Criar `include/Shared/SlotDataCache.h` + `src/Shared/SlotDataCache.cpp`.
4. Criar `include/Shared/HudConfigCache.h` + `src/Shared/HudConfigCache.cpp`.
5. Em `Config/MagicConfigAdapter` (ou onde Slots são mutados): chamar `SlotData::Refresh()`
   e `HudConfig::Refresh()` ao salvar/carregar.
6. Atualizar `HudController.cpp`: substituir todos os `Slots::*` por `SlotData::*` e todos
   os `MagicConfigAdapter::Get().*` por `HudConfig::*`.
7. Remover `Config/Slots.h`, `Config/ConfigAdapter.h`, `Config/StyleConfig.h` de HudController.
8. Build — confirmar que não há erros.

## Resultado esperado

- `HudController.cpp` não importa mais nada de `Config/`
- Fluxo: Config muta → Refresh() atualiza Shared caches → HudController::OnFrame lê Shared
- `InitializeGraphics()` recebe FontConfig como parâmetro — zero acoplamento a Config

## Observação sobre FlushSpellSettingsIfDirty

`HudConfig::FlushSpellSettingsIfDirty()` em Shared delega para `MagicConfigAdapter` — isso
cria um `Shared → Config` que não é ideal. Alternativa: HudConfigCache expõe apenas
`MarkSpellSettingsDirty()` e Config consulta a flag na próxima janela de salvamento.
A decisão final fica para a implementação.