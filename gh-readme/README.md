# Unofficial Warmane Launcher

A small, free Windows launcher for the World of Warcraft **3.3.5a** client, made for players of the Warmane server.
It finds your game, fills in the game's own login screen for you, and starts `Wow.exe` with one click.
It also works on Linux through Wine / Proton (for example Faugus).

> **This is an unofficial community project. It is not affiliated with or endorsed by Warmane.**
> It does not connect to Warmane, patch the game or change any game data.

| Log in | Logged in |
| --- | --- |
| ![Login screen](docs/login.png) | ![Logged-in screen](docs/logged-in.png) |

## Download and install

1. Go to the [**Releases**](../../releases) page and download `unofficial-warmane-launcher-setup.exe`.
2. Run it. It asks for administrator rights because the default folder is under Program Files.
3. The installer suggests your WoW 3.3.5a folder:
   `C:\Program Files (x86)\World of Warcraft 3.3.5a (no install)`.
   Use **Browse** to pick another folder. If there is no `Wow.exe` in the folder you pick, the installer warns you but lets you continue.
4. Finish the installer and start the launcher from the desktop or Start-menu shortcut.

Installing into the game folder is the easiest option, because the launcher then finds `Wow.exe` on its own.

## What the launcher does

- **Finds your game.** On start it looks for `Wow.exe` next to itself, then at the path it saved last time, then at the default path above.
  If it finds nothing, it asks you to point it at your `Wow.exe` once. It only accepts a file named `Wow.exe`, and remembers your choice.
- **Prepares the login screen.** It copies four small files into the game's `Interface\GlueXML` folder (`AccountLogin.lua`, `AccountLogin.xml` and their `.origin` copies). They let the game's own login screen fill in your account name and password. Files that already exist are left alone.
- **Logs you in.** Type your account name and password and press **LOG IN**. The launcher writes them into `AccountLogin.xml`, so when the game starts, its login screen is already filled in. Special characters in the password are handled safely.
- **Remembers your account name (optional).** With "Remember me" ticked it saves only the account name, never the password. On the next start it checks the name against the game file, and if they do not match you get the login screen again.
- **Starts the game.** **PLAY NOW** repairs any missing login files, then starts `Wow.exe` from the game folder. If Windows says the game needs administrator rights, the launcher asks for them and retries. Status and error messages appear in the right-hand column of the panel.
- **Opens your game folders.** When you are logged in, **Open game folder** opens the folder that contains `Wow.exe`, and **Open AddOns folder** opens `Interface\AddOns` (it creates the folder if it is missing). This is where you install add-ons and edit config files.
- **Logs out cleanly.** **LOG OUT** forgets the saved account name and blanks the account name and password in the game file.
- **Window extras.** A fixed-size borderless window you can drag, with minimize and close buttons, and a silent, looping video background that pauses when the window is minimized and while the game is running. The menu links open in your normal web browser.

## What the launcher does not do

- It makes **no network connections of its own**. It does not contact Warmane, check your password, download or patch the game, or send any data anywhere.
- It does not read or change the game's memory or data files. It only touches `Interface\GlueXML` inside your game folder, and its own settings file.
- The realm player counts shown when you are logged in are **placeholder numbers**, not live data.

## Security note

While you are logged in, your account name **and password are stored in plain text** in
`Interface\GlueXML\AccountLogin.xml` inside your game folder. This is how the game's login screen auto-fill works.

- Press **LOG OUT** when you are done, especially on a shared computer. It blanks both values.
- Do not share your game folder while you are logged in.
- Your password is never saved in the launcher's own settings.

## Files the launcher creates

| Where | What |
| --- | --- |
| `<game folder>\Interface\GlueXML\` | The four login-screen files described above. |
| `%APPDATA%\WarmaneLauncher\config.ini` | The path to `Wow.exe` and, if you chose "Remember me", your account name. |

## Uninstall

Use "Apps / Installed apps" in Windows, or the Start-menu uninstall shortcut.
The uninstaller removes only the files the installer added (the launcher, the uninstaller, shortcuts and registry entries).
It never deletes your game files, and it asks whether to also delete the launcher's settings.
The login files in `Interface\GlueXML` are left in place.

## Linux (Wine / Proton / Faugus)

The launcher is a native Windows program with no browser component, so it runs under Wine.
Run `unofficial-warmane-launcher-setup.exe` through your Wine/Faugus prefix, or run `unofficial-warmane-launcher.exe` from the game folder.
Under Wine, the folder links open Wine's simple file window rather than your normal file manager.

## Status

Tested under Wine 9 on Linux: finding the game, the locate box, login with special characters, "Remember me", log out, PLAY NOW, the video background, and the installer (default folder, custom folder, uninstall).
It has **not yet been tested on real Windows**, so feedback is welcome. Please open an [issue](../../issues).

## Build from source

The launcher is C++ (Win32 + GDI+) and is cross-compiled from Linux with mingw-w64; the installer is built with NSIS.

```sh
sh build.sh                 # -> build/unofficial-warmane-launcher.exe
makensis installer.nsi      # -> build/unofficial-warmane-launcher-setup.exe
g++ -std=c++17 -Isrc -DASSETS='"assets"' tests/test_logic.cpp && ./a.out   # logic tests
```

## Disclaimer

World of Warcraft and Blizzard Entertainment are trademarks of Blizzard Entertainment, Inc.
Warmane and its logos belong to their respective owners. This project is made by a player for players, is provided as is, with no warranty, and use is at your own risk.
