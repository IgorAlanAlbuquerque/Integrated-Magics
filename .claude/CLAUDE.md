# Integrated Magic — Development Guide

SKSE plugin for Skyrim SE/AE. A hotkey-driven magic slot system: assign spells, shouts, and powers to numbered slots; press a hotkey to equip them, cast, then auto-restore previous equipment. Inspired by action games like Black Myth: Wukong.

Built with CommonLibSSE-NG (C++23), ImGui (HUD), Microsoft Detours (hooks). Output: `IntegratedMagic.dll` in `SKSE/Plugins/`.

---

## Architecture layers

```
Inbound Hooks → Input Layer → Application Controllers → Domain (MagicState)
                                                       ↘ Outbound Adapters → Game
                HUD (ImGui) ←←←←←←←←←←←←←←←←←←←←←←←←←↗
```

| Layer | Namespace | Location |
|---|---|---|
| Inbound hooks (game → plugin) | — | `include/Adapters/Inbound/`, `src/Adapters/Inbound/` |
| Outbound adapters (plugin → game) | — | `include/Adapters/Outbound/`, `src/Adapters/Outbound/` |
| Application controllers | `Application::` | `include/Application/`, `src/Application/` |
| Domain / state machine | `IntegratedMagic::` | `include/Domain/`, `src/Domain/` |
| Input processing | `Input::` | `include/Input/`, `src/Input/` |
| UI / HUD | `UI::` | `include/UI/`, `src/UI/` |
| Config & persistence | `IntegratedMagic::Config::` | `include/Config/`, `src/Config/` |
| Shared value types | `IntegratedMagic::` | `include/Shared/` |
| Save/load persistence | `IntegratedMagic::` | `include/Persistence/`, `src/Persistence/` |

Entry point: `src/plugin.cpp` — SKSE plugin init, registers hooks, handles save/load events.
Hook installation: `include/Hooks.h` / `src/Hooks.cpp`.

---

## Key singletons

| Singleton | Purpose |
|---|---|
| `IntegratedMagic::MagicState::Get()` | Core state machine — spell activation lifecycle |
| `Application::SpellSystemController::Get()` | Drives MagicState each frame, handles events |
| `Application::InputController::Get()` | Processes raw input, slot edge detection |
| `Application::HudController::Get()` | Drives HUD frame rendering |
| `Application::AssignService::Get()` | Assigns hovered spells to slots |
| `IntegratedMagic::Config::MagicConfigAdapter::Get()` | Config read/write |
| `IntegratedMagic::SaveSpellDB::Get()` | Per-save slot assignments |
| `IntegratedMagic::SpellSettingsDB::Get()` | Global per-spell activation mode settings |

---

## Per-frame data flow

```
PollInputDevicesHook::ProcessAndFilter(input_events)
  → InputController processes raw input
  → KeyStateStore updated (atomic per-key booleans)
  → HotkeyMatcher tests each slot combo
  → SlotEdgeStore detects press/release edges
  → ExclusiveStore enforces one-at-a-time activation
  ↓
SpellSystemController::OnFrame(dt, inputBlocked)
  → InputController::ConsumePressedSlot() / ConsumeReleasedSlot()
  → MagicState::OnSlotPressed(slot) → returns SlotPressAction
      • Snapshots current equipment → RestoreContext
      • Builds EquipIntents (which spells for which hands)
      • Determines castStopsToSkip (see below)
  → Executes EquipIntents via MagicEquip / RestoreEquip
  → MagicState::PumpAutomatic(dt) — advances auto-cast phases
  → MagicState::PumpAutoAttack(dt) — holds synthetic attack input
  ↓
DXGIPresentHook → HudManager::DrawHudFrame()
  → Reads MagicState + slot config → HudView
  → SlotDrawer renders slot circles, icons, cooldowns
  → PopupDrawer renders assignment popup if HUD toggle was pressed
  → Mouse click on popup → AssignService::Assign()
```

---

## MagicState — state machine

`include/Domain/State.h`. The heart of the system. Two files implement it:
- `src/Domain/MagicStateLifecycle.cpp` — slot press/release, snapshot, restore
- `src/Domain/MagicStatePump.cpp` — per-frame pump, castStop/interrupt handling

### Key structs inside MagicState

**`SessionState _session`** — overall session status:
- `active`, `activeSlot` — is a slot currently active and which one
- `wasHandsDown` — was the player's weapon state `kSheathed` when the slot was pressed (affects `castStopsToSkip`)
- `attackEnabled` — game has signalled it's ready for attack input
- `isDualCasting`, `dualCastSkipCastStops` — dual-cast tracking
- `firstInterrupt` — interrupt counter for Hold mode

**`HandMode _left / _right`** — per-hand cast state (same struct for both hands):
- `mode` — `ActivationMode::Hold | Press | Automatic`
- `holdActive` — hotkey is being held (Hold mode)
- `pressActive` — toggle is on (Press mode)
- `autoActive` — automatic cast is running
- `autoCastPhase` — `Idle → WaitingAttackEnable → StartRequested → Casting → WaitingChargeRelease → Done`
- `wantAutoAttack` — whether auto-attack should be injected for this hand
- `waitingAutoAfterEquip` — waiting for equip to complete before starting auto-attack
- `chargeComplete` — charge spell has fired
- `holdFiredAndWaitingCastStop` — Hold mode: attack was sent, waiting for final castStop
- `waitingBeginCast`, `beginCastRetries` — polling for cast start event
- `finished` — this hand is done

**`RestoreContext _restore`** — equipment restoration:
- `snapshot` (`HandSnapshot`) — captured left/right objects + spells + shout before activation
- `prevExtraEquipped` — additional worn items to restore (overlay items)
- `dirtyLeft / dirtyRight / dirtyShout` — which slots were modified and need restoring
- `pendingRestore` — restore is scheduled (happens after cast ends)
- `pendingRestoreAfterSheathe` — restore waits for sheathe animation
- `sheatheAnimComplete` — sheathe animation event was received

**`CastFlags _cast`** — spurious castStop skip counter (see section below)

**`AutoAttackState _aa`** — synthetic attack hold state: `heldLeft/heldRight`, `secsLeft/secsRight`

**`ShoutState _shout`** — voice slot: shout/power lifecycle

### Activation modes

| Mode | Behavior |
|---|---|
| `Hold` | Hotkey held → cast loop. Release → restore. Uses `holdActive` + `holdFiredAndWaitingCastStop`. |
| `Press` | First press activates, second press (or cast end) deactivates. Uses `pressActive`. |
| `Automatic` | Fires once, auto-exits on cast complete. Uses `autoActive` + `AutoCastPhase`. |

### SpellType enum

`include/Shared/SpellType.h`: `Unknown`, `Concentration`, `Cast`, `Bound`, `Power`, `Shout`.

---

## CastFlags — castStopsToSkip

The mod is designed to run alongside **skipEquipAnimation** (a mod that skips weapon draw/sheathe animations). That mod causes the behavior machine to emit spurious `castStop` events at the moment a cast starts, which would normally terminate the cast prematurely.

**Root cause:** When the plugin raises the player's hands and starts a cast, the behavior machine fires one or two `castStop` events before the real cast begins. These are artifacts of the animation skip, not real cast ends.

**Counting rule** (set in `EnterHand`, `src/Domain/MagicStateSlot.cpp:105`):
```
castStopsToSkip = skipAnim ? (wasHandsDown ? 2 : 1) : 0
```
- `skipAnim = false` → no skipping needed, count = 0
- Hands were already raised (`wasHandsDown = false`) → 1 spurious stop (from skipEquip itself)
- Hands were lowered/sheathed (`wasHandsDown = true`) → 2 spurious stops (one for raising hands + one from skipEquip)

**Handling in `OnCastStop`** (`src/Domain/MagicStatePump.cpp:283`):
- While `castStopsToSkip > 0`: decrement, and if this was the **last** skip, stop+restart the attack (re-send the attack input via `ScheduleDelayedStart`) so the real cast begins
- When counter is 0: next `castStop` is real → proceed with normal exit/restore logic

**Special case:** If the spells were already on the casters (no-op equip, spells not changed), the counter is pre-decremented by 1 because one of the spurious stops won't fire.

---

## Input system

`include/Input/` — all stateless-ish stores, no singletons.

**`KeyStateStore`** — atomic `bool[kMaxCode]` tracking which keys are currently down. Updated by `PollInputDevicesHook` each frame.

**`HotkeyCacheStore`** — cached hotkey combos (up to 3 keys per slot, keyboard + gamepad) loaded from config.

**`HotkeyMatcher`** — tests `AreAllKeysInComboDown(slot)` against `KeyStateStore`.

**`SlotEdgeStore`** — detects rising/falling edges per slot (press vs. hold).

**`ExclusiveStore`** — tracks per-slot timing windows used by two optional patches (`MagicConfig`):

- `requireExclusiveHotkeyPatch` — a slot only activates if *exactly* the keys in its combo are pressed and no others. Example: combo R1+L1 will not fire if R1+L1+R2 are all held simultaneously.
- `pressBothAtSamePatch` — all keys in the combo must be pressed within a short time window of each other. Holding R1 for a long time and then pressing L1 will not activate the slot even though both are held. Fields: `pendingSrc`, `pendingTimer`, `simWindowActive`, `filterWindowActive`, `deactivatedThisPress`.

**`ReplaySystem`** (`ReplayArr`, `DeferredVec`, `RetainedArr`) — defers input events that arrive while a slot's timing window is active and replays them once the window closes, so a held key that spans the window is not lost.

**`CaptureState`** — captures the next keypress for hotkey rebinding. Activated via `InputController::RequestHotkeyCapture()`, polled via `PollCapturedHotkey()`.

---

## Outbound adapters

**`MagicEquip`** (`include/Adapters/Outbound/MagicEquip.h`) — equips `SpellItem*` in left/right hand or equips a shout/power in voice. Internally calls `RE::PlayerCharacter::RemoveSpell` / `AddSpell`.

**`RestoreEquip`** (`include/Adapters/Outbound/RestoreEquip.h`) — restores items from `RestoreContext::snapshot`. Handles per-hand objects, spells, shout, and extra worn items.

**`SyntheticInput`** (`include/Adapters/Outbound/SyntheticInput.h`) — injects synthetic attack/release input events into the game's input queue to trigger auto-cast without user pressing buttons.

---

## Config & persistence

**`include/Config/Config.h`** — main config struct: per-slot hotkeys, spell assignments, activation modes, HUD style settings, patch flags.

**`include/Config/Ports/`** — sub-configs: `InputBindings`, `SlotAssignments`, `SpellSettings`, `HudSettings`, `PatchSettings`.

**`include/Persistence/SaveSpellDB.h`** — per-save slot assignments, keyed by save filename. Loaded on save load, written on assignment change.

**`include/Persistence/SpellSettingsDB.h`** — global activation mode settings per spell `FormID`. Persists across saves.

**`include/Persistence/Slots.h`** — API to read/write slot assignments (used by both config and persistence layers).

Max slots: `IntegratedMagic::Config::kMaxSlots = 64` (`include/Config/Limits.h`).

---

## HUD / UI

`include/UI/` — all rendered via ImGui on the DXGI Present hook.

**`HudManager`** — coordinator. Reads `MagicState` + slot config, builds `HudView`, delegates to drawers.

**`HudView`** — view model: array of `SlotView` (64 slots), each with `rightSpell`, `leftSpell`, `shoutFormID`, `cooldownProgress`, `kbCodes[3]`, `gpCodes[3]`, `canCast`, `onCooldown`.

**`SlotDrawer`** — renders slot circles, spell icons (from `TextureManager`), glow on active slot, cooldown arc, hotkey label.

**`PopupDrawer`** — renders the assignment popup when HUD toggle is pressed while Magic Menu is open. Clicking a slot circle calls `AssignService::Assign(hoveredSpell, slot)`.

**`SlotCooldownTracker`** (`include/Domain/SlotCooldownTracker.h`) — tracks per-slot cooldown progress for the HUD ring display.

---

## Common tasks → files

| Task | Files to look at |
|---|---|
| Change how a spell is equipped | `include/Adapters/Outbound/MagicEquip.h`, `src/Adapters/Outbound/MagicEquip.cpp` |
| Change restore logic | `include/Adapters/Outbound/RestoreEquip.h`, `src/Domain/MagicStateLifecycle.cpp` (`RestoreContext`) |
| Bug in castStop / cast interruption | `src/Domain/MagicStatePump.cpp` (`OnCastStop`, `OnCastInterrupt`), `CastFlags` |
| Bug in Hold mode | `src/Domain/MagicStatePump.cpp` (`PumpAutomaticHand`), `HandMode::holdActive/holdFiredAndWaitingCastStop` |
| Bug in Press mode | `src/Domain/MagicStateSlot.cpp` (`TogglePressHand`), `HandMode::pressActive` |
| Bug in Automatic mode | `src/Domain/MagicStatePump.cpp` (`PumpAutomaticHand`), `AutoCastPhase` |
| Bug in auto-attack timing | `src/Domain/MagicStatePump.cpp` (`PumpAutoAttack`, `RequestAutoAttackStart`), `AutoAttackState` |
| Shout/power behavior | `include/Domain/State.h` (`ShoutState`), `src/Domain/MagicStatePump.cpp` (`OnShoutStop`) |
| Hotkey not detected / double-firing | `include/Input/ExclusiveStore.h`, `src/Input/ExclusiveTracker.cpp` |
| Hotkey rebinding | `include/Input/CaptureState.h`, `Application::InputController::RequestHotkeyCapture()` |
| HUD slot rendering | `include/UI/SlotDrawer.h`, `src/UI/SlotDrawer.cpp` |
| Assignment popup | `include/UI/PopupDrawer.h`, `src/UI/PopupDrawer.cpp`, `Application::AssignService` |
| Cooldown display | `include/Domain/SlotCooldownTracker.h` |
| Adding a new config option | `include/Config/Config.h` + relevant `Ports/` file + `src/Config/ConfigAdapter.cpp` |
| Save/load slot assignments | `include/Persistence/SaveSpellDB.h`, `src/Persistence/SaveSpellDB.cpp` |
| Global spell settings (mode, auto-attack) | `include/Persistence/SpellSettingsDB.h` |

---

## Debug logging

Enabled only in DEBUG builds. Macro: `MAGIC_DEBUG_LOG(fmt, ...)`. Output: `%LOCALAPPDATA%\Skyrim Special Edition\SKSE\IntegratedMagic.log`.

Key prefixes in logs:
- `[FLOW]` — high-level cast flow events
- `[State]` — MagicState transitions
- `[SaveLoad]` — persistence events
- `[Input]` — input processing

---

## Invariants to preserve

- `MagicState` is not thread-safe. All calls must come from the game's main thread (hooks run on main thread).
- `KeyStateStore` is `atomic<bool>[]` — safe to read from any thread for display, but write only from `PollInputDevicesHook`.
- When `_session.active = true`, `_session.activeSlot >= 0` is always true.
- `RestoreContext` is only valid while `_session.active`. Cleared by `ResetSessionState()`.
- `castStopsToSkip` is set per `EnterHand` call. If both hands are entered, the value is written twice — the second write wins (acceptable because both hands share the same session context and `wasHandsDown` is the same for both).
- Never call `MagicState::OnSlotPressed` while `_inSlotSetup = true` (re-entrant guard).
