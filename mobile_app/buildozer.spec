[app]
title = Bisca dos 4
package.name = bisca4
package.domain = org.example
source.dir = .
source.include_exts = py,png,jpg,kv,atlas,bin
version = 0.1.0

requirements = python3,kivy
orientation = landscape
fullscreen = 0

# Assets packaged with the APK
android.add_resources = assets/cards assets/nnue

# Native engine libraries generated with CMake
android.add_libs_arm64_v8a = libs/arm64-v8a/libbisca4_android.so
# android.add_libs_armeabi_v7a = libs/armeabi-v7a/libbisca4_android.so

# Target API/NDK (override if your toolchain differs)
android.api = 33
android.minapi = 24
android.archs = arm64-v8a

# Keep Python sources (use '2' for release builds if you prefer obfuscation)
android.p4a_dir =
android.log_level = 1

[buildozer]
log_level = 2
warn_on_root = 1
