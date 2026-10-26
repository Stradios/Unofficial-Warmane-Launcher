# Building from source

Both versions are built from Linux. The Windows program is cross-compiled with mingw-w64.

## Windows (`windows/`)
Needs: `g++-mingw-w64-x86-64` (with the win32 threads/posix variants as installed by your distro), `binutils-mingw-w64` (for `windres`), and `nsis` for the installer.

```sh
sh windows/build.sh                 # -> windows/build/unofficial-warmane-launcher.exe
makensis windows/installer.nsi      # -> windows/build/unofficial-warmane-launcher-setup.exe
```
`build.sh` embeds all images, the video, the font and the login files into the `.exe` through `resources.rc`, so it is a single file.

## Linux (`linux/`)
Needs: `g++`, `pkg-config`, `libgtk-4-dev` (GTK 4.10+) and `libfontconfig-dev` (package names for Debian/Ubuntu; use your distro's equivalents).

```sh
sh linux/build.sh                   # -> linux/build/unofficial-warmane-launcher
```
The images, video, font and login files are embedded into the binary with `.incbin`.

To make an AppImage, put the binary in an AppDir (`usr/bin/`), add a `.desktop` file, a 256x256 icon and an `AppRun` script, then run `appimagetool` on it:
```
AppDir/
  AppRun                              (script: exec "$HERE/usr/bin/unofficial-warmane-launcher" "$@")
  unofficial-warmane-launcher.desktop
  unofficial-warmane-launcher.png
  usr/bin/unofficial-warmane-launcher
```
```sh
ARCH=x86_64 appimagetool AppDir Unofficial-Warmane-Launcher-x86_64.AppImage
```
The AppImage does not bundle GTK; it uses the one on the user's system.

## Tests
```sh
g++ -std=c++17 -Ishared -DASSETS='"shared/assets"' shared/tests/test_logic.cpp -o /tmp/test_logic && /tmp/test_logic
g++ -std=c++17 -Ishared -Ilinux/src linux/tests/test_faugus.cpp -o /tmp/test_faugus && /tmp/test_faugus
```
The first covers the login-file logic (escaping, patching, restoring, config). The second covers reading the Faugus library and building the launch command. Neither needs a display.

## Changing the background video
`shared/tools/pack_video.py` turns an `.mp4` into `shared/assets/woods.bin` (a 1216x452 JPEG frame sequence):
```sh
python3 shared/tools/pack_video.py woods.mp4 shared/assets/woods.bin 20 6   # needs ffmpeg
```
