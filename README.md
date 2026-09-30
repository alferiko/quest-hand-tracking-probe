# Quest Hand Tracking Probe

A standalone OpenXR demo that checks whether a Quest headset provides
camera-based finger tracking **while the same hand holds an active controller**.
It displays hand skeletons, controller inputs and the runtime-reported data
source in a VR dashboard. No engine or game assets are required.

[Русская инструкция](README.ru.md) · [Download APK](https://github.com/alferiko/quest-hand-tracking-probe/releases/latest)

## What it demonstrates

For each hand, the app creates two separate trackers:

| Tracker | Requested sources | Purpose |
| --- | --- | --- |
| AUTO | Unobstructed + controller | Observe the runtime's source selection |
| CAMERA ONLY | Unobstructed | Request camera-based joints even when a controller is active |

It enables `XR_META_simultaneous_hands_and_controllers`, reads source identity
through `XR_EXT_hand_tracking_data_source`, and obtains 26 joints through
`XR_EXT_hand_tracking`. Ordinary grip-pose, trigger and squeeze actions run
alongside those trackers.

**Green means CAMERA; orange means CONTROLLER.** The controller source estimates
a hand pose from controller inputs. An unknown or inactive source is shown as
such; the app does not substitute it with a camera label.

The dashboard draws a schematic palm-plane projection anchored at the wrist,
not a world-space hand overlay. CSV files preserve the original 3D positions,
orientations and flags in LOCAL space.

## Try it

Tested on **Quest 3**. Other headsets are not verified; actual availability
depends on the runtime exposing the required extensions and capabilities.

1. Enable developer mode/USB debugging for sideloading and hand tracking in the
   headset settings.
2. Download the APK from Releases, then install it with:
   ```powershell
   adb install -r quest-hand-tracking-probe-v0.1.0.apk
   adb shell am start -n io.github.alferiko.handprobe/android.app.NativeActivity
   ```
   It also appears under Unknown Sources as **Quest Hand Probe**.
3. Check `FOCUS YES`, `SOURCE API YES` and `MULTIMODAL ON`.
4. Put both controllers down and show open hands for about 10 seconds.
5. Hold one controller, move that hand's fingers, and press/release its trigger
   and grip. Repeat with the other hand and then both controllers. Use the straps.
6. Watch **CAMERA ONLY**, not just AUTO: AUTO may choose controller-derived poses.
7. Put the controllers down again and check tracking recovery.

`CTRL ON` means the controller grip action is active; it does not prove a person
is physically holding it. `VALID` / `TRACKED` count joints with the corresponding
position flags. Occluded joints may be estimated, including in camera mode.

An earlier run of this demo on Quest 3 showed camera joints alongside an active
controller, corroborated by a person moving their fingers while holding it.
That observation is not a precision benchmark or a guarantee for every runtime.

## Build on Windows

Requirements: PowerShell 7, JDK 17, Android SDK platform 35, Build Tools 35.0.0,
and NDK 27.0.12077973. Python 3 is needed only for CSV analysis and its tests.

Install Android components through SDK Manager or `sdkmanager`:

```powershell
sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;27.0.12077973" "platform-tools"
$env:ANDROID_HOME = 'C:\Android\Sdk'
$env:JAVA_HOME = 'C:\Java\jdk-17'
./build.ps1
```

Alternatively pass `-Sdk` and `-Jdk`. The first build downloads official Khronos
OpenXR **1.1.53** headers and loader from Maven Central and verifies the pinned
SHA256. Later builds can use the cached AAR offline. No Gradle installation or
private dependencies are needed.

Output: `dist/quest-hand-tracking-probe-v0.1.0.apk` and its SHA256 file.
The script generates a local development signing key under `.local/`, which is
ignored by Git. A locally built APK uses a different key from the release APK;
switching between them requires uninstalling the previous app first (export
captures before uninstalling). The package is `io.github.alferiko.handprobe`.

## Capture and inspect data

The app stores joint data and diagnostic events locally, with separate files
for each run, under `/sdcard/Android/data/io.github.alferiko.handprobe/files/`.
It does not collect camera images or send telemetry. CSV logging is capped at
18,000 sample groups at up to 10 Hz; the dashboard continues after that limit.

```powershell
./collect.ps1                         # adb must be on PATH
./collect.ps1 -Serial YOUR_DEVICE_ID  # when multiple devices are connected
python summarize.py captures/<capture>/device-files/probe-<timestamp>.csv --out summary.json
python -m unittest discover -s tests -v
```

The report counts source states, camera/controller overlap, pressed inputs and
finger-bend ranges during overlap. Incomplete final groups are skipped. Overlap
counts are candidate observations: correlate them with the headset view and a
person actually holding that controller. An empty result does not establish
that the hardware cannot do it; check focus, settings and visibility first.

## Code map

- `main.cpp`: OpenXR session, controller actions, trackers, dashboard and CSV.
- `AndroidManifest.xml`: NativeActivity and headset permissions.
- `build.ps1`: standalone ARM64 APK build and development signing.
- `collect.ps1`, `summarize.py`: capture export and analysis.

## References and license

- [OpenXR hand data sources](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_EXT_hand_tracking_data_source.html)
- [Meta simultaneous hands and controllers](https://developers.meta.com/horizon/documentation/native/android/native-multimodal/)
- [Meta hand tracking data sources](https://developers.meta.com/horizon/documentation/native/android/native-capsense/)

Demo source: [MIT](LICENSE). Dependencies: [third-party notices](THIRD_PARTY_NOTICES.md).
