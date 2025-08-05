# g923mac

Adds realistic force feedback to your Logitech G923 when playing Euro Truck Simulator 2 or American Truck Simulator on Mac. The G923's force feedback doesn't work natively on macOS - this plugin fixes that.

## What it does

- **Realistic steering feel** - Self-centering force that changes with speed
- **Road surface effects** - Feel bumps, curbs, and different road textures
- **Vehicle dynamics** - Understeer/oversteer feedback helps you drive better
- **Engine effects** - Heavy steering when engine is off, lighter with power steering
- **LED dashboard** - RPM and speed indication on the wheel's LED strip

## Requirements

- **Hardware**: Logitech G923 steering wheel
- **System**: macOS 10.15 or newer
- **Games**: Euro Truck Simulator 2 or American Truck Simulator

## Installation

### Option 1: Download Release (Easiest)
1. Download the latest `libg923mac.dylib` from [Releases](https://github.com/ivonunes/g923mac/releases)
2. Copy it to your game's plugins folder:

**For American Truck Simulator:**
```bash
~/Library/Application Support/Steam/steamapps/common/American Truck Simulator/American Truck Simulator.app/Contents/MacOS/plugins/
```

**For Euro Truck Simulator 2:**
```bash
~/Library/Application Support/Steam/steamapps/common/Euro Truck Simulator 2/Euro Truck Simulator 2.app/Contents/MacOS/plugins/
```

3. Create the `plugins` folder if it doesn't exist

### Option 2: Build from Source
```bash
git clone https://github.com/ivonunes/g923mac
cd g923mac
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
# Then copy libg923mac.dylib to your game's plugins folder
```

## Usage

1. **Connect your G923** to your Mac
2. **Launch the game** (ETS2 or ATS)
3. **Click OK** when you see the "Advanced SDK features" popup
4. **Look for the test** - Your wheel's LEDs should flash and the wheel should briefly turn then center

If it works, you're all set!
