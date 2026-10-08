# Happy Wheels for PS Vita

A loader that runs the Android version of **Happy Wheels** (Fancy Force, v1.1.2,
armeabi-v7a) natively on the PS Vita and in the Vita3K emulator.

It loads the game's original Android library (`libMyGame.so`) into memory,
resolves its imports with native Vita functions and fakes the small part of
the Java side the game needs. Built on
[soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate).

**This package does not contain any game files.** You need your own copy of the
Android version of Happy Wheels (it is free on Google Play).

## Requirements

On a real PS Vita (HENkaku / Ensō):

1. **kubridge**: download `kubridge.skprx` from
   <https://github.com/bythos14/kubridge/releases>, copy it to `ur0:tai/` and add
   it under `*KERNEL` in `ur0:tai/config.txt`:
   ```
   *KERNEL
   ur0:tai/kubridge.skprx
   ```
   Reboot afterwards.
2. **libshacccg.suprx** (Sony's shader compiler): install and run
   [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D) once. It puts the
   file at `ur0:data/libshacccg.suprx` automatically.

On Vita3K: put `kubridge.skprx` and `config.txt` in the emulator's `ux0/tai/`
folder (same content as above) and `libshacccg.suprx` in `ur0/data/`.

## Installation

1. Get the Happy Wheels **1.1.2 armeabi-v7a** APK and open it with any zip tool
   (rename it to `.zip` if needed).
2. Create the folder `ux0:data/happywheels/` and copy into it:
   - `lib/armeabi-v7a/libMyGame.so` → `ux0:data/happywheels/libMyGame.so`
   - the whole `assets` folder → `ux0:data/happywheels/assets/`

   The result should look like this:
   ```
   ux0:data/happywheels/libMyGame.so
   ux0:data/happywheels/assets/large/...
   ux0:data/happywheels/assets/medium/...
   ux0:data/happywheels/assets/shared/...
   ux0:data/happywheels/assets/small/...
   ux0:data/happywheels/assets/sounds/...
   ux0:data/happywheels/assets/tiny/...
   ```
3. Install `happywheels.vpk` with VitaShell.

Your progress is saved to `ux0:data/happywheels/prefs.txt`.

## Controls

| Vita | In game |
|---|---|
| D-Pad Up / R / Left stick up | Accelerate |
| D-Pad Down / L / Left stick down | Brake / reverse |
| D-Pad Right / Left stick right | Lean forward |
| D-Pad Left / Left stick left | Lean back |
| Cross / Square | Special action (jump, boost...) |
| Triangle | Eject |
| Start / Circle | Pause / back |
| Touch screen | Works exactly like on the phone |

## Building

Requires [VitaSDK-softfp](https://github.com/vitasdk-softfp) with the packages
`opensles`, `libsndfile`, `lame`, `mpg123`, `libvorbis`, `libogg`, `flac`, `opus`,
`zlib`, `kubridge`, `taihen`, `libmathneon`, `SceShaccCgExt` and `vitaShaRK`.

```sh
export VITASDK=/path/to/vitasdk-softfp
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## Credits

- TheFloW for the original .so loader
- Rinnegatamante for vitaGL, so_util and FalsoJNI groundwork
- gl33ntwine (v-atamanenko) for soloader-boilerplate and FalsoJNI
- Fancy Force / Jim Bonacci for Happy Wheels

## License

The loader code is MIT licensed (see `LICENSE`). Happy Wheels and all of its
assets belong to their respective owners and are not included.
