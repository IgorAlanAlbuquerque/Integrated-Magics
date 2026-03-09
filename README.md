# Integrated Magic

> A SKSE plugin that brings a hotkey-driven magic slot system to Skyrim — assign spells, shouts, and powers to dedicated slots and cast them on demand, with full control over how and when they fire.

---

## What does it do?

**Integrated Magic** replaces the need to manually equip spells before combat. You assign spells, shouts, and powers to numbered slots, bind hotkeys to those slots, and the mod handles equipping, casting, and restoring your gear automatically.

### Features

#### 🎯 Magic Slots with Hotkeys

Bind up to N hotkeys, one (or a combo of max 3) per slot. Press a hotkey and the spell assigned to that slot is equipped and activated immediately — no need to open menus mid-combat.

Each slot can hold:

- **Two spells** — one per hand (left and right independently)
- **One shout or power** — centered in the voice slot

#### 🖱️ HUD Assignment via Magic Menu

Open the in-game Magic Menu, hover over any spell, shout, or power, and trigger the assignment popup with a configurable hotkey. A visual circle appears for each slot — click to assign the hovered item. Right-click to clear.

#### ⚙️ Activation Modes

Each spell or shout in a slot can be configured independently with one of three activation modes:

| Mode | Behavior |
| --- | --- |
| **Hold** | Activates while the hotkey is held, deactivates on release |
| **Press** | Toggles on with the first press, off with the second |
| **Automatic** | Fires once and exits automatically when the cast completes |

#### 🗣️ Shouts & Powers

Shouts (`TESShout`) and powers (`SpellItem` of type Power or Lesser Power) are fully supported as slot content. They use the voice slot and respect the same activation mode rules.

#### ⚔️ Auto-Cast / Auto-Attack

For Hold and Automatic modes, the mod can automatically fire the attack input after equipping, allowing continuous casting without manual button presses. Works with the Skyrim attack animation system and respects charge time before firing.

#### 💾 Persistence

All slot assignments are saved per save file and restored on load. Activation mode settings (Hold/Press/Automatic and auto-attack preference) are stored per spell FormID and persist globally across saves.

---

## How it works (brief)

1. Open the Magic Menu and hover over a spell or shout
2. Press the HUD toggle hotkey — a popup appears with your slots
3. Click a slot circle to assign the hovered item
4. Close the menu and press the slot's hotkey in combat
5. The mod equips the item, activates it according to its mode, then restores your previous equipment when done

---

## Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) *(free Community edition)*
- [`vcpkg`](https://github.com/microsoft/vcpkg)
  1. Clone the repository or [download as .zip](https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip)
  2. Enter the `vcpkg` folder and run `bootstrap-vcpkg.bat`
  3. Add a system/user environment variable:
     - **Name:** `VCPKG_ROOT`
     - **Value:** `C:\path\to\your\vcpkg`

<img src="https://raw.githubusercontent.com/SkyrimDev/Images/main/images/screenshots/Setting%20Environment%20Variables/VCPKG_ROOT.png" height="150">

---

## Building

Open the project folder in Visual Studio 2022, VS Code, or CLion. CMake will run automatically and download [CommonLibSSE NG](https://github.com/alandtse/CommonLibVR) and all dependencies.

> For VS Code: install the [C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) and [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) extensions if not prompted automatically, then reopen the folder.

By default the compiled `.dll` goes into `build/`. To output directly into your game or mod manager:

**Output to Skyrim `Data` folder:**

- Set environment variable `SKYRIM_FOLDER` to your Skyrim installation path
  - e.g. `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition`

<img src="https://raw.githubusercontent.com/SkyrimDev/Images/main/images/screenshots/Setting%20Environment%20Variables/SKYRIM_FOLDER.png" height="150">

**Output to Mod Organizer 2 / Vortex `mods` folder:**

- Set environment variable `SKYRIM_MODS_FOLDER` to your mods path
  - MO2: `C:\Users\<user>\AppData\Local\ModOrganizer\Skyrim Special Edition\mods`
  - Vortex: `C:\Users\<user>\AppData\Roaming\Vortex\skyrimse\mods`

<img src="https://raw.githubusercontent.com/SkyrimDev/Images/main/images/screenshots/Setting%20Environment%20Variables/SKYRIM_MODS_FOLDER.png" height="150">

<details>
<summary>Finding your mods folder in MO2</summary>

> Click the `...` next to "Mods" to get the full folder path

<img src="https://raw.githubusercontent.com/SkyrimDev/Images/main/images/screenshots/MO2/MO2SettingsModsFolder.png" height="150">
</details>

<details>
<summary>Finding your mods folder in Vortex</summary>

<img src="https://raw.githubusercontent.com/SkyrimDev/Images/main/images/screenshots/Vortex/VortexSettingsModsFolder.png" height="150">
</details>

---

## Compatibility

Built with [CommonLibSSE NG](https://github.com/alandtse/CommonLibVR) — supports:

| Edition | Supported |
| --- | --- |
| Skyrim SE | ✅ |
| Skyrim AE | ✅ |
| Skyrim GOG | ✅ |
| Skyrim VR | ✅ |

---

## License

[MIT](LICENSE)
