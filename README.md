# g923mac

Adds force feedback support for your Logitech G923 when playing games with CrossOver/Wine on macOS.

The project contains:

- `G923Mac.app`: native macOS app that talks to the wheel over HID
- `dinput8.dll`: Windows proxy loaded by the game under CrossOver/Wine, forwarding force feedback to `G923Mac.app`

## Requirements

- Logitech G923 steering wheel
- macOS

## Use Prebuilt Releases

Use this path if you just want to run it.

1. Download `G923Mac.app` and `dinput8.dll` from the latest GitHub release for this repository.
2. Copy `dinput8.dll` next to the game executable inside your CrossOver/Wine bottle.
3. Launch `G923Mac.app` on macOS.
4. Start your game through CrossOver/Wine.

## Build From Source

Use this path if you want to build your own binaries.

### Build requirements

- Xcode command line tools
- CMake
- `mingw-w64` for building the Windows DLL on macOS (`brew install mingw-w64`)

### 1. Build the macOS app

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MAC_APP=ON
cmake --build build --target G923Mac
```

Output:

```bash
build/G923Mac.app
```

### 2. Build the Windows `dinput8.dll` proxy

```bash
cmake -S . -B build-windows \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_MAC_APP=OFF \
  -DBUILD_WINDOWS_PROXY=ON \
  -DCMAKE_TOOLCHAIN_FILE=bridge/windows/toolchains/x86_64-w64-mingw32.cmake

cmake --build build-windows --target g923mac_dinput8
```

Output:

```bash
build-windows/dinput8.dll
```

### Use local builds with CrossOver / Wine

1. Copy `build-windows/dinput8.dll` next to the game executable inside your CrossOver/Wine bottle.
2. Launch `build/G923Mac.app` on macOS.
3. Start your game through CrossOver/Wine.

## Wine / CrossOver DLL Override

If the proxy is not being loaded, open `winecfg` for the bottle and add a DLL override for `dinput8` as `native, builtin`.

## Optional Proxy Log

The Windows proxy appends logs to `g923mac_proxy.log` in the same folder as `dinput8.dll`, but only if that file already exists.

If you want logs, create an empty `g923mac_proxy.log` file first.
