# MBTH Ghost Client

> Minecraft 1.7.10 / 1.8.9 utility client with combat modules, humanised aim/click pacing, an in-game module search, notification sounds, and configurable presets.
>
> **Status:** Educational / personal research. KillAura visible-rotation v0.1 is **detectable on Hypixel** — the safe combat stack is **AimAssist + TriggerBot**.


---

## What's new vs. upstream Fusion+

| Module / change | Status | Notes |
|---|---|---|
| **TriggerBot** | new | Auto-attacks the entity already under your crosshair while LMB is held. **No rotation manipulation** — server sees only clean swings + C02 packets. Pairs with AimAssist for the safest combat envelope. |
| **KillAura v0.1** | new (visible-rotation) | Full target picker + humanisation pipeline (per-axis speed limit, ±10% imperfect correlation, ±3% yaw / ±2% pitch jitter, slow-down lerp, GCD quantisation, optional short-stop). Ships with an "Apply Legit Preset" button + big red BANNABLE warning. Silent rotation is the v0.2 milestone. |

| **Reach (rewrite)** | upgrade | Replaced with a real ray-vs-AABB extended-melee implementation gated by CPS / weapon / visibility / friend / LMB. |
| **AutoTool** | new | Switches to best mining tool for the block being mined; switches back when LMB releases. |

| **Notification sounds** | new | Per-type Windows MessageBeep, with a "Test sounds" button in HUD settings. |
| **Module search bar** | new | Top of menu — type to filter modules across all categories with click-to-jump. |
| **Per-module descriptions + tooltips** | new | Every module exposes a one-line description shown on hover. |
| **Hypixel-Legit / Hypixel-Safe configs** | new | Two pre-tuned `.fusion` configs for legit-style PvP. |
| **MBTH branding** | new | Internal paths and config-file extension preserved for backwards compatibility. |


## The safe combat stack

```
AimAssist  (small FOV, weapon-only, jitter, visibility check)
   +
TriggerBot (LMB-only, players-only, 150 ms reaction-hold, 6–11 CPS, weapon-only)
```

Watchdog has no rotation-velocity / aim-modulus / dup-rotation surface to inspect because **TriggerBot never publishes a rotation we authored** — it only fires a synthetic LMB when the user's own aim is on a hitbox. Load the `Hypixel-Safe.fusion` preset to enable this stack with conservative defaults.

## Build


Requires Visual Studio 2019 / 2022 with **v142 toolset** + Windows 10 SDK.

```cmd
build-killaura.bat
```

Or manually:

```cmd
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" ^
  fusion-plus\fusion-plus.vcxproj ^
  /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142 /m /nologo /v:minimal
```

Outputs `fusion-plus\build\fusion-plus.dll` (~1.6 MB).

## Inject

1. Run `fusion-injector` (build it the same way from `fusion-injector/fusion-injector.vcxproj` if you don't have a binary).
2. Launch Lunar Client (1.7.10 or 1.8.9), get to the main menu.
3. Click **Inject** in the injector.
4. In-game, press **`Insert`** to open the menu.
5. Press **`End`** to detach.

## Modules at a glance

**Combat:** AimAssist · TriggerBot · KillAura · Reach · LeftAutoClicker · RightAutoClicker
**Visual:** ESP · Radar · Nametag · BlockESP
**Movement:** Sprint · SprintReset · BridgeAssist · Velocity
**Inventory:** ChestStealer · InventorySorter
**Utility:** ArrayList · ClientBrandChanger · Weapon · AutoTool

## Layout

```
fusion-plus/                  C++ DLL — the actual cheat
  src/base/moduleManager/     Module registry + per-module Update/RenderOverlay/RenderHud/RenderMenu
    modules/combat/           AimAssist, KillAura, TriggerBot, Reach, autoclickers
    modules/visual/           ESP, Radar, Nametag, BlockESP
    modules/movement/         Sprint*, BridgeAssist, Velocity
    modules/inventory/        ChestStealer, InventorySorter
    modules/utility/          ArrayList, ClientBrandChanger, Weapon, AutoTool
  src/base/util/math/         rotationUtils.{h,cpp}  humanised rotation pipeline
  src/base/sdk/               C++ wrappers for Minecraft Java classes (JNI/JVMTI)

  src/base/configManager/     settings + JSON serialise/deserialise
  src/base/menu/              ImGui rendering + window hooks
  src/base/notificationManager Toast notifications + Windows beeps

fusion-injector/              Standalone injector .exe (Dear ImGui)
asm/                          ASM patches for the Java side (legacy)
```


## ⚠️ Use at your own risk

This is a public binary built from public source. It will be hash-flagged by signature-aware anti-cheats. The safe assumption is that **any account you use this on can be banned**. Don't use it on accounts you're not willing to lose.


The author of this fork takes no responsibility for bans, account loss, or anything else that happens when you use this code.

## Credits

MBTH Ghost Client is a fork of **[Fusion+](https://github.com/h1meji/fusion-plus)** by **Himeji**, with additional contributions from **11Luke11** and **Autocliicker**. All upstream work is preserved under its original license — see `LICENSE`.


