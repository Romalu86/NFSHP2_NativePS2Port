# Need for Speed: Hot Pursuit 2 — Native PC Port

Native Windows x64 port of the PlayStation 2 game engine (SLES_507.31 \ SLUS_203.62), using Vulkan graphics, XAudio2 audio and SDL2 controllers. **Original game data is required and is not included.**

## Requirements

- Windows x64 and a hardware GPU with Vulkan 1.1+ drivers.
<<<<<<< HEAD
- Complete original PS2 game data: an ISO, a matching `ZDIR.BIN` / `ZZDATA.BIN` pair, or fully extracted resources.
=======
- Complete PS2 PAL game data: an ISO, a `ZDIR.BIN` / `ZZDATA.BIN` pair, or fully extracted resources.
- For building: Visual Studio 2022 with **Desktop development with C++**, MSVC v143 and a Windows SDK.

## Build

1. Open `vs2022/NFSHP2.sln`.
2. Select **Release | x64** and **Build Solution**.
3. Find `NFSHP2.exe`, `NFSHP2Unpack.exe` and `SDL2.dll` in `vs2022/bin/x64/Release`.

Keep `SDL2.dll` beside `NFSHP2.exe`. A separate Vulkan SDK, Clang, Python or shader compiler is not required for the Visual Studio build.

## Run

<<<<<<< HEAD
The game detects adjacent archives or extracted resources automatically. With no data detected, select the archive pair, extracted folder or ISO in the startup dialog, then use the launcher. You can also select the source from the command line:
=======
Place the executable and Extras folder beside the original `ZDIR.BIN` and `ZZDATA.BIN`, or select your game data explicitly:

```powershell
.\NFSHP2.exe --data "D:\Games\NFSHP2\@ZZDATA"
.\NFSHP2.exe --zzdata "D:\Games\NFSHP2\ZZDATA.BIN"
.\NFSHP2.exe --data "D:\Games\NFSHP2.iso"
```

<<<<<<< HEAD
Choose a manually selected source again on a later launch, or pass its path explicitly. For extracted resources, preserve the full directory, including `__Unknown`, `__archive` and `NFSHP2_EXTRACTION_MANIFEST.json`. See [the extraction guide](tools/archive/README.md) for `NFSHP2Unpack` usage.

## Settings and controls

The launcher provides monitor and video adapter selection, resolution, display mode, texture filtering, MSAA or SMAA, draw distance, and advanced gameplay options. Use `--configure` to open the launcher directly or `--no-configurator` to use saved settings.

Use **Options → Graphics** for the frame limit, VSync and the FPS display. Use **Options → Controls** to configure keyboard and gamepad controls for each player. The trigger-based gamepad layouts use proportional acceleration, braking and reverse. When a DualSense is detected, the gamepad menu shows optional adaptive triggers and police radio settings. Police radio on the controller speaker requires a USB connection and an enabled native Windows audio endpoint.

Default keyboard controls are **W/S** for accelerate/brake, **A/D** for steering, **Space** for the handbrake, **C** for camera, **Esc** for pause, **Enter** for confirm and **Backspace** for back.

## Saves and troubleshooting

Keep the `Saves/` folder beside the executable when updating. The game migrates `Saves/NFSHP2_pc.ini` to `Saves/settings.ini` and reads any existing profiles. Profile controls are saved separately from progress. Deleting a profile in the menu also deletes its local progress backup and controls. For bug reports, include `NFSHP2_log.txt`, any `NFSHP2_error.txt` or crash dump, and the steps to reproduce the issue.

Wine/Proton uses the existing XAudio2 PCM audio path. The DualSense speaker feature uses the native Windows endpoint. Wine/Proton playback has not been tested with this build.

Third-party notices: [SDL2](third_party/SDL2/LICENSE.txt) · [Vulkan headers](third_party/vulkan/LICENSE.md) · [Vulkan loader](third_party/vulkan/LICENSE-Loader.txt) · [SMAA](third_party/smaa/LICENSE.txt).
=======
These are alternative launch methods. The game also searches for supported data beside the executable. For extracted resources, preserve the full directory, including `__Unknown`, `__archive` and `NFSHP2_EXTRACTION_MANIFEST.json`. See [the extraction guide](tools/archive/README.md) for `NFSHP2Unpack` usage.

## Settings and controls

The pre-launch configurator provides display settings, texture filtering, MSAA, draw distance, keyboard bindings, controller selection and gamepad bindings. Use `--configure` to open it directly or `--no-configurator` to launch with saved settings.

Default keyboard controls: **W/S** accelerate/brake, **A/D** steer, **Space** handbrake, **C** camera, **Esc** pause, **Enter** confirm and **Backspace** back. Controls can be remapped in the configurator.

## Saves and troubleshooting

Profiles, settings and logs are stored in `Saves/` beside the executable. Keep this folder when updating. For bug reports, include `NFSHP2_log.txt`, any `NFSHP2_error.txt` or crash dump, and the steps to reproduce the issue.

The port is still under development. Wine/Proton audio compatibility is not verified in this base version.

Third-party notices: [SDL2](third_party/SDL2/LICENSE.txt) · [Vulkan headers](third_party/vulkan/LICENSE.md) · [Vulkan loader](third_party/vulkan/LICENSE-Loader.txt).

