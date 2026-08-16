# RSForceEnum

Minimal `xinput1_3.dll` proxy for Rocksmith 2014 Remastered.

## Function

Press **F8** to force Rocksmith to re-enumerate songs.

This keeps only the enumeration technique from RSMods:
- signature-scan the Rocksmith `.text` section
- hook the enumeration service to capture its flags pointer
- set the same two flags on F8

No RSMods GUI, INI, D3D overlay, MIDI, audio-device code, Crowd Control,
Wwise code, settings system, or bug-prevention patches.

## Build

AppVeyor / Visual Studio 2022:
- Release
- Win32
- `RSForceEnum/RSForceEnum.vcxproj`

Output: `RSForceEnum/Release/xinput1_3.dll`

## Install

Copy only `xinput1_3.dll` to the Rocksmith 2014 root folder.
Replace/remove the RSMods `xinput1_3.dll` first.

RS_ASIO remains separate (`avrt.dll`, `RS_ASIO.dll`, `RS_ASIO.ini`).

## Use

After Rocksmith has completed its normal startup song enumeration, press **F8**
after adding/removing a song.

The DLL creates no config or debug files.
