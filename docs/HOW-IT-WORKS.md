# How it works

## The login-file technique (both versions)
The 3.3.5a client builds its login screen from files in `Interface\GlueXML`. The launcher installs a modified `AccountLogin.xml`/`AccountLogin.lua` (plus the originals as `.origin` copies) that fill in the account name and password fields when they hold values.

- **Writing credentials:** `AccountLogin.xml` has three spots (`AccountLoginAccountEdit:SetText("…")`, `AccountLoginPasswordEdit:SetText("…")`, and a `gsub("…", "@@", "@")` call) inside an `<OnUpdate>` block. The launcher replaces the text inside those three string literals. Values are escaped for Lua first (`\`, `"`, newline) and then for XML (`&`, `<`, `>`), so any password round-trips safely.
- **Missing or edited file:** if the existing `AccountLogin.xml` has no recognizable login block, the launcher copies it to `AccountLogin.xml.bak` and writes the bundled version instead.
- **Reading back:** "Remember me" compares the saved account name with the one in the game file, so the launcher never shows a stale login.
- **Log out:** writes empty values, so the password does not stay on disk.
- **Folder names:** `Interface` and `GlueXML` are matched case-insensitively, so an existing `interface/gluexml` folder is reused instead of creating a second one (this matters on Linux).

The password is stored in plain text in that file while you are logged in. See the security note in the README.

## Finding Wow.exe
1. The path saved in the launcher's settings (`config.ini`), if the file still exists.
2. A `Wow.exe` next to the launcher (or next to the AppImage).
3. Windows: `C:\Program Files (x86)\World of Warcraft 3.3.5a (no install)\Wow.exe`.
   Linux: a Wow.exe entry in the Faugus library.
4. Otherwise the setup box asks you to locate it. Only a file named `Wow.exe` is accepted.

## Starting the game on Linux
The Linux version runs the game the way Faugus does. Faugus keeps its data in:

| File | Used for |
| --- | --- |
| `~/.local/share/faugus-launcher/games.json` | The list of games: `path`, `prefix`, `runner`, `protonfix` (game ID), `launch_arguments`, `game_arguments`. |
| `~/.config/faugus-launcher/config.json` | Settings; `default-runner` is used when a game's runner is "Default". |
| `~/.local/share/faugus-launcher/umu-run` | The `umu-run` build Faugus uses. |
| `~/.local/share/Steam/compatibilitytools.d/` | Installed Proton builds. |

(The Flatpak locations under `~/.var/app/…` are also checked.)

PLAY NOW then runs, with the game folder as the working directory:

```
GAMEID=<protonfix> WINEPREFIX=<prefix> PROTONPATH=<runner> [launch_arguments] umu-run <Wow.exe> [game_arguments]
```

- If the entry's runner is a folder, that folder is used; a name such as `GE-Proton10-4` is looked up in the Proton folders; "… Latest" names are passed to Faugus's own `umu-run`.
- If there is no Faugus entry for this `Wow.exe`, umu-run's defaults are used with the newest installed GE-Proton, or `GE-Proton` (which umu downloads).
- If no `umu-run` is found at all, the launcher falls back to `wine`, and shows a message if that is missing too.
- The launcher pauses its background video while the game runs, and refuses to start a second copy.

## The window
The whole window is one custom-drawn 1226×467 surface (Win32 + GDI+ on Windows, GTK4 + Cairo + Pango on Linux) with the same layout in both, so the two versions look the same. Only the two login text fields are real controls. The background video is a pre-rendered JPEG frame sequence embedded in the program, drawn under a cached overlay so each frame only blends the parts of the interface that are not transparent.

## Settings
- Windows: `%APPDATA%\WarmaneLauncher\config.ini`
- Linux: `~/.config/unofficial-warmane-launcher/config.ini`

Keys: `wow_exe=` (path to `Wow.exe`) and `account=` (only if "Remember me" is on). The password is never saved here.

## Uninstall (Linux)

Every folder and file the launcher newly creates inside the game folder is recorded as a `created=` line in `config.ini`. The uninstaller removes only those entries, and only if they are strictly inside the game folder (paths outside it, containing `..`, or symlinks are ignored). Folders are removed only when empty, so an `Interface` folder that now holds your add-ons stays. The saved login in `AccountLogin.xml` is blanked first. The settings and cache folders are deleted only if they are named `unofficial-warmane-launcher`. If an older version left no record, files that are byte-identical to the launcher's bundled ones are recognised instead. Run `--uninstall` from a terminal for the same thing without the window.
