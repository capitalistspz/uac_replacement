# UAC Replacement Module
A reimplementation of the GamePad audio library `uac.rpl`, which underlies `mic.rpl`.

## Dependencies
- https://github.com/wiiu-env/WiiUModuleSystem
- https://github.com/devkitPro/wut

## Building
```bash
cmake --preset wums-rel-dbg-info
cd build/wums/rel-dbg-info
cmake --build .
```
All presets can be found via `cmake --list-presets`