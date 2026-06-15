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
| `IntegratedMagic::SlotCooldownTracker::Get()` | Per-slot cooldown progress tracking |
| `IntegratedMagic::Config::MagicConfigAdapter::Get()` | Config read/write |
| `IntegratedMagic::SaveSpellDB::Get()` | Per-save slot assignments |
| `IntegratedMagic::SpellSettingsDB::Get()` | Global per-spell activation mode settings |

> **Note:** Spell assignment is done via free functions in `IntegratedMagic::MagicAssign` (see `include/Shared/AssignService.h`): `ComputeSpellAssignment`, `ComputeShoutAssignment`, `ComputeClearHand`, `ComputeClearShout`. These are not a singleton.

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
      • Optionally calls MagicAction::SetSkipEquipVars + DrawWeaponMagicHands
  → Executes EquipIntents via MagicAction::EquipSpellInHand / EquipShoutInVoice
  → MagicState::PumpAutomatic(dt) — advances auto-cast phases
  → MagicState::PumpAutoAttack(dt) — holds synthetic attack input
  ↓
DXGIPresentHook → HudManager::DrawHudFrame()
  → Reads MagicState + slot config → HudView
  → SlotDrawer renders slot circles, icons, cooldowns
  → PopupDrawer renders assignment popup if HUD toggle was pressed
  → Mouse click on popup → MagicAssign::ComputeSpellAssignment()
```

---

## MagicState — state machine

`include/Domain/State.h`. The heart of the system. Three files implement it:
- `src/Domain/MagicStateLifecycle.cpp` — slot press/release, snapshot, restore, equip-complete
- `src/Domain/MagicStatePump.cpp` — per-frame pump, interrupt/castStop handling, phase transitions
- `src/Domain/MagicStateSlot.cpp` — `EnterHand`, `TogglePressHand`, `DisableHand`, `FinishHand`, `PrepareSlotEntry`, `SetModeSpellsFromHand`

### Key structs inside MagicState

**`SessionState _session`** — overall session status:
- `active`, `activeSlot` — is a slot currently active and which one
- `wasHandsDown` — was the player's weapon state `kSheathed` when the slot was pressed
- `isDualCasting` — both hands have the same spell (dual cast mode)
- `activeTimeoutSecs` — safety timeout counter to force-exit hung sessions
- `activeLeftID`, `activeRightID`, `activeShoutID` — FormIDs of the active slot's spells/shout
- `modeSpellLeft`, `modeSpellRight` — pointers to the equipped spells (used to match caster events)

**`HandMode _left / _right`** — per-hand cast state (same struct for both hands):
- `mode` — `ActivationMode::Hold | Press | Automatic`
- `holdActive` — hotkey is being held (Hold mode)
- `pressActive` — toggle is on (Press mode)
- `autoActive` — automatic cast is running
- `wantAutoAttack` — whether synthetic attack input should be injected for this hand
- `autoCastPhase` — `Idle → StartRequested → Casting → WaitingChargeRelease → Done`
- `waitingChargeComplete` — charge is in progress, waiting for `IsChargeComplete`
- `chargeComplete` — charge spell has been detected as fully charged
- `holdFiredAndWaitingCastStop` — Hold mode: attack was sent, waiting for final castStop
- `pendingRestartNextFrame` — UP was dispatched this frame; DOWN should go on the next frame
- `needsManualFireInKReady` — cast was confirmed externally without a synthetic hold; fire via stopAttack from kReady
- `startRequestSecs` — time spent in `StartRequested` before timing out and retrying
- `castingElapsedSecs` — elapsed time since cast confirmed (Casting phase)
- `waitingSpellFireFinalize` — waiting for spell fire event or caster idle to finalize
- `spellFireFinalizeSecs` — time since spell fire finalize was scheduled
- `finished` — this hand is done

**`RestoreContext _restore`** — equipment restoration:
- `snapshot` (`HandSnapshot`) — captured left/right objects + spells + shout before activation
- `prevExtraEquipped` — additional worn items to restore (overlay items)
- `dirtyLeft / dirtyRight / dirtyShout` — which slots were modified and need restoring
- `pendingRestore` — restore scheduled immediately after session ends
- `pendingRestoreAfterSheathe` — restore waits for sheathe animation to complete
- `sheatheAnimComplete` — sheathe animation event was received
- `pendingPowerRestore` — power/shout restore deferred until animation settles
- `pendingPowerRestoreDelaySecs` — countdown for power restore delay
- `sheatheWaitSecs` — time spent waiting for sheathe; gives up after `kSheatheWaitTimeoutSec`

**`AutoAttackState _aa`** — synthetic attack hold state: `heldLeft/heldRight`, `secsLeft/secsRight`; helpers `Held(Hand)`, `Secs(Hand)`.

**`ShoutState _shout`** — voice slot lifecycle:
- `modeShoutID`, `finished`, `held`, `heldSecs` — shout/power identity and hold state
- `isPower` — true if the equipped form is a Power (not a shout)
- `powerAutoSecs` — elapsed time for Automatic-mode power hold
- `waitingStopEvent` — waiting for `shoutStop` anim event before finishing
- `dirty` — shout slot was modified

### Activation modes

| Mode | Behavior |
|---|---|
| `Hold` | Hotkey held → cast loop. Release → restore. Uses `holdActive` + `holdFiredAndWaitingCastStop`. |
| `Press` | First press activates, second press (or cast end) deactivates. Uses `pressActive`. |
| `Automatic` | Fires once, auto-exits on cast complete. Uses `autoActive` + `AutoCastPhase`. |

### SpellType enum

`include/Shared/SpellType.h`: `Unknown`, `Concentration`, `Cast`, `Bound`, `Power`, `Shout`.

---

## Automatic mode — cast phase loop

For `Automatic` (and `Hold` with `wantAutoAttack`), the plugin drives the cast via synthetic attack input:

1. **StartRequested** — attack button held DOWN. Waits for caster to reach `kUnk02` (stable charging state).  
   - If interrupted by a spurious `OnCasterInterrupt` (pre-cast noise): releases UP, sets `pendingRestartNextFrame`. On the next frame, goes DOWN again.
   - If `kRedispatchInterval` (0.15 s) elapses without progress: same UP → DOWN restart.
   - **Dual-cast sync**: in dual-cast mode, whenever one hand restarts, the other is also released so both hands go DOWN together on the same frame — required for the game's behavior machine to stay in dual-cast mode.
2. **Casting** — caster reached `kUnk02`. Accumulates `castingElapsedSecs`. Waits for `IsChargeComplete`.  
   - `SkipChanneling` patch: zeros `castingTimer` immediately (after draining magicka) so charge is considered complete instantly. Useful for Automatic mode on spells with long charge animations.
3. **WaitingChargeRelease** — charge detected as complete. Attack button released UP. Spell fires.
4. **Done** — `OnSpellFired` received; `FinishHand` called; session exits.

---

## Input system

`include/Input/` — all stateless-ish stores, no singletons.

**`KeyStateStore`** — atomic `bool[kMaxCode]` tracking which keys are currently down. Updated by `PollInputDevicesHook` each frame.

**`HotkeyCacheStore`** — cached hotkey combos (up to 3 keys per slot, keyboard + gamepad) loaded from config.

**`HotkeyMatcher`** — tests `AreAllKeysInComboDown(slot)` against `KeyStateStore`.

**`SlotEdgeStore`** — detects rising/falling edges per slot (press vs. hold).

**`ExclusiveStore`** — tracks per-slot timing windows used by two optional patches (`MagicConfig`):

- `requireExclusiveHotkeyPatch` — a slot only activates if *exactly* the keys in its combo are pressed and no others. Example: combo R1+L1 will not fire if R1+L1+R2 are all held simultaneously.
- `pressBothAtSamePatch` — all keys in the combo must be pressed within a short time window of each other. Holding R1 for a long time and then pressing L1 will not activate the slot even though both are held. Key fields: `pendingSrc`, `pendingTimer`, `simWindowActive`, `simWindowRemaining`, `filterWindowActive`, `filterWindowTimer`, `deactivatedThisPress`, `fullComboSeen`.

**`ReplaySystem`** (`ReplayArr`, `DeferredVec`, `RetainedArr`) — defers input events that arrive while a slot's timing window is active and replays them once the window closes, so a held key that spans the window is not lost.

**`CaptureState`** — captures the next keypress for hotkey rebinding. Activated via `InputController::RequestHotkeyCapture()`, polled via `PollCapturedHotkey()`.

---

## Outbound adapters

**`MagicAction`** (`include/Adapters/Outbound/MagicEquip.h`) — namespace with functions to equip spells/shouts and manage animation variables:
- `EquipSpellInHand(player, spell, hand, skipAnim)`
- `EquipShoutInVoice(player, form)`
- `ClearHandSpell(player, hand)` / `ClearVoiceShout(player)`
- `SetSkipEquipVars(player, on)` / `ResetSkipEquipToken()` / `ApplySkipEquipAnimReturn(...)`
- `DrawWeaponMagicHands(bool)`

**`Outbound::RestoreOneHand` / `ReequipPrevExtraEquipped`** (`include/Adapters/Outbound/RestoreEquip.h`) — restores items from `RestoreContext::snapshot`. Handles per-hand objects, spells, shout, and extra worn items.

**`SyntheticInput` / `detail::DispatchAttack` / `detail::DispatchShout`** (`include/Adapters/Outbound/SyntheticInput.h`) — injects synthetic attack/release input events into the game's input queue to trigger auto-cast without user pressing buttons.

---

## Config & persistence

**`include/Config/Config.h`** — main config struct: per-slot hotkeys, spell assignments, activation modes, HUD style settings, patch flags.

**`include/Config/Ports/PatchSettings.h`** — patch flags:
- `skipEquipAnimationPatch` — injects skip-equip-animation variables when equipping spells
- `skipEquipAnimationOnReturnPatch` — same, applied when restoring equipment
- `requireExclusiveHotkeyPatch` — exclusive hotkey detection (see Input system)
- `pressBothAtSamePatch` — simultaneous key window detection (see Input system)
- `skipChannelingPatch` — instantly completes spell charge in Automatic mode (zeros `castingTimer`)

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

**`PopupDrawer`** — renders the assignment popup when HUD toggle is pressed while Magic Menu is open. Clicking a slot circle calls `MagicAssign::ComputeSpellAssignment(slot, hand, existingLeftID)`.

**`SlotCooldownTracker`** (`include/Domain/SlotCooldownTracker.h`) — tracks per-slot cooldown progress for the HUD ring display.

---

## Common tasks → files

| Task | Files to look at |
|---|---|
| Change how a spell is equipped | `include/Adapters/Outbound/MagicEquip.h`, `src/Adapters/Outbound/MagicEquip.cpp` |
| Change restore logic | `include/Adapters/Outbound/RestoreEquip.h`, `src/Domain/MagicStateLifecycle.cpp` (`RestoreContext`) |
| Bug in castStop / cast interruption | `src/Domain/MagicStatePump.cpp` (`OnCastStop`, `OnCasterInterrupt`) |
| Bug in Hold mode | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase`), `HandMode::holdActive/holdFiredAndWaitingCastStop` |
| Bug in Press mode | `src/Domain/MagicStateSlot.cpp` (`TogglePressHand`), `HandMode::pressActive` |
| Bug in Automatic mode | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase`, `PumpAutomatic`), `AutoCastPhase` |
| Bug in dual-cast sync | `src/Domain/MagicStatePump.cpp` (`PumpAutomatic` dual-cast sync, `OnCasterInterrupt` dual-cast sync) |
| Bug in auto-attack timing | `src/Domain/MagicStatePump.cpp` (`PumpAutoAttack`, `RequestAutoAttackStart`), `AutoAttackState` |
| Shout/power behavior | `include/Domain/State.h` (`ShoutState`), `src/Domain/MagicStatePump.cpp` (`OnShoutStop`) |
| Skip channeling behavior | `src/Domain/MagicStatePump.cpp` (`PumpCastPhase` → SkipChanneling block) |
| Skip equip animation | `include/Adapters/Outbound/MagicEquip.h` (`SetSkipEquipVars`, `ApplySkipEquipAnimReturn`) |
| Hotkey not detected / double-firing | `include/Input/ExclusiveStore.h`, `src/Input/ExclusiveTracker.cpp` |
| Hotkey rebinding | `include/Input/CaptureState.h`, `Application::InputController::RequestHotkeyCapture()` |
| HUD slot rendering | `include/UI/SlotDrawer.h`, `src/UI/SlotDrawer.cpp` |
| Assignment popup | `include/UI/PopupDrawer.h`, `src/UI/PopupDrawer.cpp`, `IntegratedMagic::MagicAssign` |
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
- Never call `MagicState::OnSlotPressed` while `_inSlotSetup = true` (re-entrant guard).
- In dual-cast mode (`_session.isDualCasting`), both hands must always restart DOWN together. Any UP dispatched for one hand must be paired with an UP for the other in the same frame so both go DOWN on the next frame — otherwise the game's behavior machine exits dual-cast and fires individual casts.
