; SPDX-License-Identifier: GPL-3.0-or-later
; SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
;
; Per-user installer (NSIS 3, no administrator rights). Built by packaging/windows/build-installer.ps1:
;   makensis /DVERSION=1.2.3 /DSTAGE=<staged folder> /DOUTFILE=<setup.exe> /DHAS_UI [/DAPP_ICO=<ico>] installer.nsi
; Silent use:  setup.exe /S [/AUTOSTART=1] [/D=C:\target\folder]   (the /D= argument must come last; a folder that is
;              not empty and holds no dofscroll install is refused with exit code 2)
; Uninstall:   <install dir>\uninstall.exe  (also from Settings > Apps); it removes the autostart value.
!ifndef HAS_UI
  !error "HAS_UI must be defined: every installer contains the interface and the daemon (no daemon-only installer)"
!endif
Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "VERSION is not defined"
!endif
!ifndef STAGE
  !error "STAGE is not defined"
!endif
!ifndef OUTFILE
  !error "OUTFILE is not defined"
!endif

!define APP "dofscroll"
; Windows PowerShell by its full path: a "powershell.exe" planted next to setup.exe would otherwise run.
!define PS_EXE "$WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"
; Same Run value as packaging/windows/install-autostart.ps1 (value name "dofscroll").
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define RUN_NAME "dofscroll"
; The UI's "Start at login" switch writes this second value (the UI program).
!define RUN_UI_NAME "dofscroll-ui"

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "Sections.nsh"

Name "dofscroll ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
BrandingText "dofscroll ${VERSION} (unsigned)"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "dofscroll"
VIAddVersionKey "FileDescription" "dofscroll installer (per user)"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "GPL-3.0-or-later"

!ifdef APP_ICO
  !define MUI_ICON "${APP_ICO}"
  !define MUI_UNICON "${APP_ICO}"
!endif
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION OpenUiFromFinish
!define MUI_FINISHPAGE_RUN_TEXT "Open dofscroll now (it starts the daemon and shows a short setup guide)"

!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var AutostartFlag
Var UiWasRunning
Var DaemonWasRunning
Var DaemonStarted

; Finish-page box "Open dofscroll now": the first start is simply opening the program (as from the Start
; menu). The interface starts the daemon by itself when none runs and, on the first start, shows the setup
; guide. A second start of an interface that is already open only brings its window to the front.
Function OpenUiFromFinish
  Exec '"$INSTDIR\dofscroll-ui.exe"'
FunctionEnd

; The uninstaller removes the whole install folder: refuse a folder that is not empty and not a dofscroll
; install (no uninstall.exe), such as Documents, so neither the folder picker nor /D=<dir> of a silent install
; can lead to data loss. CheckInstDir leaves "1" (usable) or "0" on the stack.
Function CheckInstDir
  Push $0
  Push $1
  IfFileExists "$INSTDIR\uninstall.exe" 0 checkScan
    StrCpy $0 "1"
    Goto checkDone
  checkScan:
  FindFirst $0 $1 "$INSTDIR\*.*"
  checkLoop:
    StrCmp $1 "" checkEmpty
    StrCmp $1 "." checkNext
    StrCmp $1 ".." checkNext
    FindClose $0
    StrCpy $0 "0"
    Goto checkDone
  checkNext:
    FindNext $0 $1
    Goto checkLoop
  checkEmpty:
    FindClose $0
    StrCpy $0 "1"
  checkDone:
  Pop $1
  Exch $0
FunctionEnd

Function .onVerifyInstDir
  Call CheckInstDir
  Pop $0
  StrCmp $0 "1" +2
    Abort
FunctionEnd

Function .onInit
  ; Silent installs: /AUTOSTART=1 selects the autostart section (default: off).
  ${GetParameters} $0
  ${GetOptions} $0 "/AUTOSTART=" $1
  StrCpy $AutostartFlag $1
  ; A silent install has no directory page, so the check of .onVerifyInstDir would never run for /D=<dir>.
  ${If} ${Silent}
    Call CheckInstDir
    Pop $0
    ${If} $0 != "1"
      SetErrorLevel 2
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "dofscroll (required)" SecMain
  SectionIn RO
  ; Stop a running interface of this install first (exit code 10 = it was running; it is started
  ; again after an interactive update, see the last section).
  StrCpy $UiWasRunning "0"
  ; The script comes from this installer (not from $INSTDIR): an update of an install that predates it
  ; stops the interface too.
  InitPluginsDir
  File "/oname=$PLUGINSDIR\stop-ui.ps1" "${STAGE}\scripts\stop-ui.ps1"
  nsExec::Exec '"${PS_EXE}" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\stop-ui.ps1" -ExePath "$INSTDIR\dofscroll-ui.exe"'
  Pop $0
  StrCmp $0 "10" 0 +2
    StrCpy $UiWasRunning "1"
  ; Stop a running daemon of this install (an update replaces dofscroll.exe). Exit code 10 = it was
  ; running; it is started again after the update, also for silent installs (see "-restart daemon").
  ; The script comes from this installer, like stop-ui.ps1: an update of an older install uses the new one.
  StrCpy $DaemonWasRunning "0"
  File "/oname=$PLUGINSDIR\stop-daemon.ps1" "${STAGE}\scripts\stop-daemon.ps1"
  nsExec::ExecToStack '"${PS_EXE}" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\stop-daemon.ps1" -ExePath "$INSTDIR\dofscroll.exe"'
  Pop $0
  Pop $1
  ; the output of the stop script goes to a small log, for support and for the CI check
  FileOpen $2 "$TEMP\dofscroll-installer.log" w
  FileWrite $2 "stop-daemon exit code $0$\r$\n$1$\r$\n"
  FileClose $2
  StrCmp $0 "10" 0 +2
    StrCpy $DaemonWasRunning "1"
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "dofscroll"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Lukas Hois-Zelinka"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/HoZe-Lukas/dofScroll"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Start menu shortcuts" SecMenu
  CreateDirectory "$SMPROGRAMS\dofscroll"
  CreateShortCut "$SMPROGRAMS\dofscroll\dofscroll.lnk" "$INSTDIR\dofscroll-ui.exe" "" "$INSTDIR\dofscroll-ui.exe" 0
  CreateShortCut "$SMPROGRAMS\dofscroll\Uninstall dofscroll.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section /o "Start the daemon at logon" SecAutostart
  WriteRegStr HKCU "${RUN_KEY}" "${RUN_NAME}" '"$INSTDIR\dofscroll.exe"'
SectionEnd

; Silent install with /AUTOSTART=1 (the components page does not run then).
Section "-silent autostart"
  ${If} ${Silent}
  ${AndIf} $AutostartFlag == "1"
    WriteRegStr HKCU "${RUN_KEY}" "${RUN_NAME}" '"$INSTDIR\dofscroll.exe"'
  ${EndIf}
SectionEnd

; Update with the daemon running before (silent or interactive): start it again, daemon first, then
; the interface. A fresh install starts nothing here (the finish-page box, the Start menu entry or the
; autostart entry does: opening the interface starts the daemon).
Section "-restart daemon"
  StrCpy $DaemonStarted "0"
  ${If} $DaemonWasRunning == "1"
    IfFileExists "$INSTDIR\dofscroll.exe" 0 +3
      Exec '"$INSTDIR\dofscroll.exe"'
      StrCpy $DaemonStarted "1"
  ${EndIf}
SectionEnd

; Interactive update with the interface open before: open it again (silent installs never start it).
Section "-restart ui"
  ${If} $UiWasRunning == "1"
  ${AndIfNot} ${Silent}
    IfFileExists "$INSTDIR\dofscroll-ui.exe" 0 +2
      Exec '"$INSTDIR\dofscroll-ui.exe"'
  ${EndIf}
SectionEnd

LangString DESC_Main ${LANG_ENGLISH} "The daemon, the user interface, devices, examples and licenses."
LangString DESC_Menu ${LANG_ENGLISH} "Start menu folder with the program and an uninstall entry."
LangString DESC_Auto ${LANG_ENGLISH} "Start dofscroll.exe at every logon of the current user (Run key value dofscroll)."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMenu} $(DESC_Menu)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAutostart} $(DESC_Auto)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  IfFileExists "$INSTDIR\scripts\stop-ui.ps1" 0 +2
    nsExec::Exec '"${PS_EXE}" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\scripts\stop-ui.ps1" -ExePath "$INSTDIR\dofscroll-ui.exe"'
  IfFileExists "$INSTDIR\scripts\stop-daemon.ps1" 0 +2
    nsExec::Exec '"${PS_EXE}" -NoProfile -ExecutionPolicy Bypass -File "$INSTDIR\scripts\stop-daemon.ps1" -ExePath "$INSTDIR\dofscroll.exe"'
  ; autostart: the Run value (and a Startup shortcut of the autostart script, if any)
  DeleteRegValue HKCU "${RUN_KEY}" "${RUN_NAME}"
  DeleteRegValue HKCU "${RUN_KEY}" "${RUN_UI_NAME}"
  Delete "$SMSTARTUP\${RUN_NAME}.lnk"
  Delete "$SMPROGRAMS\dofscroll\dofscroll.lnk"
  Delete "$SMPROGRAMS\dofscroll\Uninstall dofscroll.lnk"
  RMDir "$SMPROGRAMS\dofscroll"
  DeleteRegKey HKCU "${UNINST_KEY}"
  ; the program folder only; the user's configuration in %APPDATA%\dofscroll stays. Only when it is an
  ; install of dofscroll (dofscroll.exe is there): a copy of uninstall.exe in another folder, for example
  ; Downloads, must not delete that folder.
  IfFileExists "$INSTDIR\dofscroll.exe" 0 +2
    RMDir /r "$INSTDIR"
SectionEnd
