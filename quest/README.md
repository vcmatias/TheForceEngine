# The Force Engine VR for Meta Quest (motion controls)

A standalone VR build of [The Force Engine](https://github.com/TheForceEngine/TheForceEngine)
for Meta Quest 3 / 3S, with an OpenXR layer written for this port: stereo rendering of the
GPU renderer, 6DOF head tracking, and Touch motion controls (the shot leaves the weapon in
your hand, not the centre of the view).

Version in this branch: **0.41** (`versionCode 87`). Base: upstream commit
`6ae6d225c31e7623c5d1ab357f018b5577d83869`.

> **The game data is not included.** You need your own copy of Star Wars: Dark Forces
> (Steam or GOG). Nothing from the game is distributed here.

## What the port adds

- OpenXR backend (`quest/android/src/tfe_xr*.cpp`): per-eye camera from the head pose,
  the game's menus, PDA and map on panels in the world.
- Aim override in `TFE_DarkForces/weaponFireFunc.cpp`: every weapon fires from the pose of
  the right controller; weapon inaccuracy as in the original game.
- Weapons held in the hand as 3D models, in five styles: high resolution, voxel, fine voxel,
  models built with Blender scripts in `quest/tools/weapons` (the default; packed as
  `dfmod_weapons.bin`, made for this port) and DFMOD (see the installer below), with a hand drawn
  on the grip.
- Left wrist watch with the HUD, holographic map, mission PDA held in the left hand,
  pause menu on the PDA, start menu inside a ship room.
- Weapon wheel and previous/next weapon strip on the right stick; three walking speeds;
  movement by head or by controller direction; snap turn.
- Controls screen with a drawing of the Touch controllers; every button can be remapped
  there (saved in `tfe_vr.ini`). Grips work as modifiers.
- Optional gas mask overlay; optional voxel objects (see credits).

The development log, version by version, is in
[`NOTAS-DE-DESENVOLVIMENTO.md`](NOTAS-DE-DESENVOLVIMENTO.md) (Portuguese). Most versions after
0.21 were built without a headset test; the log says which ones were checked on the device.

## Layout

| Path | Contents |
|---|---|
| `TheForceEngine/` | The engine, with the port's changes applied (55 files changed, `GameUI/vrMenu.*` added) |
| `TheForceEngine/Voxels/` | Dark Forces Voxel Pack models used by the "voxel objects" option |
| `quest/android/` | CMake project, manifest, APK script, OpenXR layer and VR code (`src/`) |
| `quest/tools/` | Scripts that generate the port's data: weapons, hands, gas mask, grips |
| `quest/installer/` | Installer of the DFMOD weapon style, from the player's own copy of the mod |
| `quest/hosttest/` | Small desktop harnesses used to check geometry and menus without a headset |

## Building

Linux, Android NDK r27c, SDK platform 34, build-tools 34 and 35, CMake, Ninja, JDK 17.
The APK is assembled without Gradle.

```sh
cd quest
git clone --depth 1 --branch release-2.30.x https://github.com/libsdl-org/SDL.git SDL2
git clone --depth 1 --branch SDL2 https://github.com/libsdl-org/SDL_image.git SDL2_image
git clone --depth 1 https://github.com/KhronosGroup/OpenXR-SDK.git OpenXR-SDK

export ANDROID_NDK_HOME=/path/to/android-ndk-r27c ANDROID_HOME=/path/to/android-sdk
cmake -S android -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
ninja -C build-android
EXTRA_SO=$(find build-android -name libopenxr_loader.so | head -1) android/build_apk.sh
```

The result is `quest/out/TFE-VR-Quest.apk`, signed with a debug key generated on the
first run. Notes:

- The working tree this branch was published from kept the engine in a sibling folder;
  the paths in `android/CMakeLists.txt` and `android/build_apk.sh` were adjusted to this
  layout for publication and the build was not run again here. The dependency versions
  above are the series used (SDL 2.30); exact tags were not recorded.
- Several scripts under `quest/tools/` still carry absolute paths of the machine they
  were written on. They document how each asset was produced and need their paths edited
  to run elsewhere. The modelling scripts need Blender 4.2.

## Installing

Sideload the APK (package `com.vcmatias.tfevr`) and open it once, so it creates its data
folder and extracts the engine files. Then copy the files of your Dark Forces game folder
(everything except the `.exe` and `.dll` files) to
`/sdcard/Android/data/com.vcmatias.tfevr/files/`, and the files of its `LFD` folder to
`files/LFD/`, always with the full destination file name, for example
`adb push DARK.GOB /sdcard/Android/data/com.vcmatias.tfevr/files/DARK.GOB`.

## What was left out of this repository

- Any file of the game.
- The Portuguese translation of the game (texts, subtitles, briefing and menu images) and its
  tools. The engine still looks for `Translation/pt` and `Captions/subtitles-pt.txt` in the data
  folder; without them the game runs in English. The port's own VR option screens are written
  in Portuguese in the code.
- The weapon models of the Dark Forces Mod Remastered (Jedi Academy), used by the DFMOD weapon
  style. [`quest/installer`](installer/README.md) builds that file on your computer from your own
  download of the mod and copies it to the headset (`DFMOD2_BIN` in `build_apk.sh` is for private builds).
- Signing keys and the Android toolchain.

## License and credits

- The Force Engine: luciusDXL and contributors, GPL-2.0 (`LICENSE` at the root). The
  changes and the new code in this branch are released under the same license.
- Hand mesh and skeleton: WebXR Input Profiles generic hand, Copyright (c) 2019 Amazon,
  MIT (`quest/tools/hand/LICENSE-webxr-input-profiles.md`).
- Voxel objects: Dark Forces Voxel Pack by Dzierzan, non-commercial use with attribution,
  for use with a legitimate copy of Dark Forces (`TheForceEngine/Voxels/LICENSE.txt`).
- OpenXR loader: The Khronos Group, Apache-2.0. SDL2 and SDL2_image: zlib license.
- Star Wars: Dark Forces is a trademark and copyright of Lucasfilm Ltd. This project is not
  affiliated with or endorsed by Lucasfilm, Disney or the authors of The Force Engine.
