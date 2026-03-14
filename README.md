## DCaSTaway

This is YET another fork of DCaSTawy (which is fork of Castaway & Hatari for DC) with basic SDL & Libretro implementation of a "Lite" Atari-ST Emulator.

## Native build

```
no instructions available
```

## Cross-Compiling instructions (miyoo)

1. Clone this repo & set up your environment with docker:
```
git clone -b master --single-branch https://github.com/Apaczer/dcastaway
docker run --volume ./:/src/ -it miyoocfw/toolchain-shared-uclibc:latest
cd /src
```
2. Compile 

- `dcastaway_libretro.so` libretro core
``` 
make -j$(nproc) -f Makefile.libretro platform=miyoo
```

- `dcastaway` standalone binary
``` 
make -j$(nproc) -f Makefile.miyoo
```

## Controls

|RetroPad button|Action|
|---|---|
|B|Fire button 1 (JOYSTICK)/ Left Button (MOUSE)|
|A|--- (JOYSTICK) / Left Button (MOUSE|
|Y| Space |
|X| Enter |
|L|Left Mouse|
|R|Right Mouse|
|L+R|Toggle between JOYSTICK/MOUSE emulation|
|L2| --- |
|R2| --- |
|Start|Toggle virtual keyboard|
|Select|---|

In mouse emulation dpad and fire buttons controls the mouse.

## Bios

TOS ROM supported:

|System|Version|Filename|MD5|
|---|---|---|---|
|ATARI-ST|TOS v1.04 (1989)(Atari Corp)(Mega ST)(UK)|**tos.rom**|036c5ae4f885cbf62c9bed651c6c58a8|
|ATARI-ST|EmuTOS 192k 0.9.12|**tos.rom**|eaa828c288a0c9208a425b472a34e8b5|

## Credits

To Chui & others authors for the DCaSTaway, 
to Joachim Hoenig for the "Castaway", 
to maintainers of the Hatari emulator, 
to angree for LIBRETRO first implementation, 
to Salvacam for Miyoo port.