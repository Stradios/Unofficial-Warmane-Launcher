; Unofficial Warmane Launcher installer (NSIS). Build: makensis installer.nsi  (needs build/unofficial-warmane-launcher.exe)
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"

!define APP      "Unofficial Warmane Launcher"
!define EXE      "unofficial-warmane-launcher.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\WarmaneLauncher"

Name "${APP}"
OutFile "build\unofficial-warmane-launcher-setup.exe"
; Default: the folder where the WoW 3.3.5a client is expected to live.
InstallDir "$PROGRAMFILES32\World of Warcraft 3.3.5a (no install)"
; Remember the folder of a previous install (upgrade in place).
InstallDirRegKey HKLM "Software\WarmaneLauncher" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APP}"

!define MUI_ICON   "assets\app.ico"
!define MUI_UNICON "assets\app.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "assets\welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "assets\welcome.bmp"

!define MUI_WELCOMEPAGE_TITLE "${APP} Setup"
!define MUI_WELCOMEPAGE_TEXT "This installs the ${APP} into your World of Warcraft 3.3.5a folder, so it can find the game and set up its login screen automatically.$\r$\n$\r$\nYou can pick a different folder on the next page."
!insertmacro MUI_PAGE_WELCOME

!define MUI_DIRECTORYPAGE_TEXT_TOP "The launcher will be installed in the folder below, which is where World of Warcraft 3.3.5a is expected to be (it should contain Wow.exe).$\r$\n$\r$\nTo install somewhere else, click Browse and choose another folder."
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE DirLeave
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Warn (don't block) when the chosen folder has no Wow.exe.
Function DirLeave
  IfFileExists "$INSTDIR\Wow.exe" done
  MessageBox MB_YESNO|MB_ICONEXCLAMATION "Wow.exe was not found in:$\r$\n$INSTDIR$\r$\n$\r$\nThe launcher will ask you to locate Wow.exe the first time it starts.$\r$\n$\r$\nInstall here anyway?" IDYES done
  Abort
  done:
FunctionEnd

Section "Launcher" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; tidy up files from the earlier "Warmane Launcher" builds (only our own old files)
  Delete "$INSTDIR\warmane-launcher.exe"
  Delete "$INSTDIR\Uninstall Warmane Launcher.exe"
  Delete "$DESKTOP\Warmane Launcher.lnk"
  Delete "$SMPROGRAMS\Warmane Launcher\Warmane Launcher.lnk"
  Delete "$SMPROGRAMS\Warmane Launcher\Uninstall Warmane Launcher.lnk"
  RMDir "$SMPROGRAMS\Warmane Launcher"
  File "build\${EXE}"
  WriteUninstaller "$INSTDIR\Uninstall Unofficial Warmane Launcher.exe"

  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortcut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APP}\Uninstall ${APP}.lnk" "$INSTDIR\Uninstall Unofficial Warmane Launcher.exe"
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}" "" "$INSTDIR\${EXE}" 0

  WriteRegStr HKLM "Software\WarmaneLauncher" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APP}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall Unofficial Warmane Launcher.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  ; Only our own files: the folder is usually the game's folder, never delete it recursively.
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\Uninstall Unofficial Warmane Launcher.exe"
  RMDir "$INSTDIR"   ; only succeeds if empty
  Delete "$DESKTOP\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\Uninstall ${APP}.lnk"
  RMDir "$SMPROGRAMS\${APP}"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\WarmaneLauncher"
  MessageBox MB_YESNO|MB_ICONQUESTION "Also remove the launcher's saved settings (game path and remembered account name)?" IDNO keep
  RMDir /r "$APPDATA\WarmaneLauncher"
  keep:
SectionEnd
