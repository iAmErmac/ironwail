<a href="https://github.com/iAmErmac/IronRift/releases/latest">![GitHub Release](https://img.shields.io/github/v/release/iAmErmac/IronRift?display_name=release&style=for-the-badge&label=Download)</a>

# What's this?

Ironwail is a high-performance Quake engine derived from [QuakeSpasm](https://sourceforge.net/projects/quakespasm/). This `android` branch adds a native, flat-screen Android build through the [IronRift](https://github.com/iAmErmac/IronRift) host project.

The Android renderer targets OpenGL ES 3.1 as its baseline. It uses ordinary GLES VBOs, EBOs, VAOs, UBOs, and direct indexed draws, with optional capabilities enabled only when the device reports that they are usable.

## Does performance still matter on a phone?

It does when a map has a lot going on. Modern Quake releases and community maps can push far more geometry, models, particles, and translucent surfaces than the original levels. Android devices also vary widely, so the renderer keeps the base path conservative and measures the work that matters on real hardware.

The desktop renderer can stream model data through SSBOs and GPU-generated indirect draw lists. That approach is not a good fit for Adreno’s tiled/binning architecture: streamed SSBO vertex data can increase synchronization and binning pressure even when it reduces CPU draw setup. The Android path therefore keeps persistent world and model meshes in static VAOs, streams transient GUI and particle data through rotating VBO/EBO buffers, and uses aligned UBO ranges for per-object data. Opaque world surfaces can be combined into bounded indexed batches where ordering and material state allow it. This gives the phone renderer a predictable fallback while preserving the desktop fast path on desktop builds.

## Bonus features

- play the original Quake releases and installed add-ons from Android storage
- native OpenGL ES rendering through the IronRift Android host
- touch controls for movement, looking, attack, jump, use, menus, and console access
- keyboard and gamepad input alongside touch controls when the device supports them
- a *Mods* menu for quick access to installed add-ons
- weapon bindings that can be changed from the UI
- alternative HUD styles based on the Q64 layout
- real-time palettization with optional dithering
- classic underwater warp effect
- lightmapped liquid surfaces
- smoothly interpolated lightstyles
- reduced heap usage and loading time for large maps
- higher color/depth precision to reduce banding and z-fighting artifacts
- a more precise workaround for the z-fighting present in the original levels
- a capped framerate while no map is loaded

## Getting started on Android

The Android app is hosted by [IronRift](https://github.com/iAmErmac/IronRift). IronRift owns the Android Activity, EGL/GLES context, surface lifecycle, input bridge, and APK packaging; this repository supplies the Ironwail engine and renderer. Ironwail is tracked as an Android-branch submodule, so a normal checkout should fetch it automatically:

```text
git clone --recurse-submodules https://github.com/iAmErmac/IronRift.git
git -C IronRift submodule update --init --recursive
```

If IronRift was already cloned without submodules, run the second command before building. The `Projects/Android/jni/ironwail` submodule should point at the `android` branch of this repository.

From the IronRift repository root, the build helpers can build the debug APK and optionally install it:

```text
scripts/build.ps1
scripts/build.ps1 Install
```

On a shell environment, use:

```text
./scripts/build.sh
./scripts/build.sh Install
```

The direct Gradle form is also available from `Projects/Android`:

```text
./gradlew assembleDebug
```

The build uses the Android SDK, NDK 27.2.12479018, Gradle 8.2.1, and produces an arm64-v8a APK. The debug APK is written under `Projects/Android/build/outputs/apk/debug/`. For a clean rebuild, use `./gradlew clean assembleDebug` from that directory.

With a USB-debugging-enabled device or emulator connected, install and launch the APK with:

```text
adb devices
adb install -r Projects/Android/build/outputs/apk/debug/ironrift-debug.apk
adb shell appops set com.ermac.ironwail MANAGE_EXTERNAL_STORAGE allow
adb shell am start -n com.ermac.ironwail/.GLES3JNIActivity
```

## Quake data and storage permission

The app uses `/sdcard/ironwail` as its working directory. Android requires the app to have full file-system access (“All files access”) so it can read the game data and write configuration, logs, screenshots, and saves there. Grant that permission in Android settings when prompted; without it the app may start but will not find the game data correctly.

Copy the Quake data directories into `/sdcard/ironwail` itself. A normal installation contains:

```text
/sdcard/ironwail/id1
/sdcard/ironwail/hipnotic
/sdcard/ironwail/rogue
/sdcard/ironwail/dopa
/sdcard/ironwail/mg1
/sdcard/ironwail/mg3
```

Copy only the directories you own or are licensed to use. Keep each directory’s original Quake layout and do not add an extra nesting level. For a development launch, `/sdcard/ironwail/commandline.txt` can contain arguments such as:

```text
ironwail +game id1 +map start
```

The Android wrapper handles presentation and lifecycle; Ironwail renders the flat target and does not perform a desktop window swap.

## System requirements

| | Minimum | Recommended |
|:--|:--|:--|
|Android|Android 10 / API 29, arm64-v8a, OpenGL ES 3.1|A recent Snapdragon/Adreno device with ample memory and sustained performance|
|Storage|Full file-system access and Quake data under `/sdcard/ironwail`|Fast internal storage for shorter map loads|
|Controls|Touchscreen|Touchscreen plus keyboard or gamepad|

Notes:
1. Android API 29 is the current APK minimum. OpenGL ES 3.1 is the renderer compatibility baseline; GLES 3.2 and other extensions are optional runtime tiers.
2. Android performance depends heavily on resolution, thermal state, driver version, and the map being played. Emulator timings are useful for regression checks, not as a substitute for a physical phone.
3. You need a legally obtained Quake installation or data set; the Android host does not provide the game data.
## Desktop VR controls

The desktop OpenXR build registers these archived cvars. They can be set from the command line with `+set name value` or changed in the console:

| Cvar | Purpose | Default |
|---|---|---:|
| `vr_mode` | Enable desktop VR | `1` |
| `vr_curved_screen` | Curve the floating screen | `1` |
| `vr_curve_radius` | Curved-screen radius | `6.0` |
| `vr_screen_scale` | Floating screen size | `2.2` |
| `vr_screen_distance` | Floating screen distance in meters | `2.5` |
| `vr_desktop_mirror` | Keep the desktop mirror visible | `1` |
| `vr_world_scale` | World-to-VR scale | `33.5` |
| `vr_hud_scale` | HUD graphics scale inside its texture | `0.7` |
| `vr_hud_size` | Entire HUD layer size | `0.35` |
| `vr_hud_distance` | HUD layer distance in meters | `0.5` |
| `vr_hud_yoffset` | Whole HUD layer vertical offset in meters | `0` |
| `vr_hud_render_yoffset` | HUD graphics vertical offset inside the texture | `0` |

Example:

```text
+set vr_hud_scale 0.7 +set vr_hud_size 0.35 +set vr_hud_distance 0.5
```

Android-specific renderer defaults are applied by the IronRift host. The Android bridge accepts launch arguments and forwards them to the engine; game data and configuration remain external to the APK.

## Build output

The CMake build regenerates `ironwail.pak` from `Misc/pak` and copies it beside the executable, including `build/Debug/ironwail.pak`. The committed `Quake/ironwail.pak` remains an unchanged fallback/reference copy.