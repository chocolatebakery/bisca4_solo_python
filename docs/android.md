# Android Build Guide

This repository now ships two deliverables for mobile:

- a native shared library (`libbisca4_android.so`) containing the game engines;
- a Kivy front‑end (`mobile_app/`) that calls the engine through `ctypes`.

Follow the steps below to produce an APK.

## 1. Build the native engine

Make sure the Android NDK r25+ is installed and that `ANDROID_NDK_HOME` points to it.
Repeat the configure/build pair for every ABI you intend to ship (example: `arm64-v8a`).

```bash
# Configure
cmake -S . -B build-android-arm64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake ^
  -DANDROID_ABI=arm64-v8a ^
  -DANDROID_PLATFORM=android-24

# Compile the shared library
cmake --build build-android-arm64 --target bisca4_android --config Release
```

The shared object is produced as:

```
build-android-arm64/libbisca4_android.so
```

Copy it to the location expected by Buildozer:

```
mobile_app/libs/arm64-v8a/libbisca4_android.so
```

Repeat the process for other ABIs (for example `armeabi-v7a`) and drop the `.so`
files in their respective folders inside `mobile_app/libs/`.

## 2. Prepare assets

`mobile_app/assets/` already contains the card sprites and NNUE weights that ship
with the desktop GUI. If you generate updated networks place them under the same
folder and adjust the profile mapping in `mobile_app/main.py` accordingly.

## 3. Package with Buildozer

Install Buildozer and its prerequisites inside a Python virtualenv (`pip install buildozer`).
From inside `mobile_app/` invoke:

```bash
buildozer android debug
```

Important entries in `mobile_app/buildozer.spec`:

- `requirements = python3,kivy`
- `android.add_libs_arm64_v8a = libs/arm64-v8a/libbisca4_android.so`
- `android.add_resources = assets/cards assets/nnue`

Adjust `package.name`, `package.domain`, icons and signing settings before building a
release variant.

## 4. Running locally

For quick desktop runs you can execute the mobile GUI without packaging:

```bash
python mobile_app/main.py
```

If the native library is not available the code automatically falls back to the
desktop executables; on Windows ensure `bisca4.exe` and `bisca4_mcts.exe` exist in
the repository root so the fallback still works.
