!include "MUI2.nsh"

Name "BlockForge Launcher"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\\BlockForge"
RequestExecutionLevel user

!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

!ifndef VERSION
  !define VERSION "dev"
!endif

!ifndef VIPRODUCTVERSION
  !define VIPRODUCTVERSION "0.0.0.0"
!endif

VIProductVersion "${VIPRODUCTVERSION}"
VIAddVersionKey /LANG=1033 "ProductName" "BlockForge Launcher"
VIAddVersionKey /LANG=1033 "FileDescription" "BlockForge Launcher"
VIAddVersionKey /LANG=1033 "CompanyName" "BlockForge"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"

Section "MainSection" SEC01
  SetOutPath "$INSTDIR"
  File /r "${APPDIR}\\*"
  CreateShortcut "$DESKTOP\\BlockForge Launcher.lnk" "$INSTDIR\\BlockForge.exe"
  CreateDirectory "$SMPROGRAMS\\BlockForge"
  CreateShortcut "$SMPROGRAMS\\BlockForge\\BlockForge Launcher.lnk" "$INSTDIR\\BlockForge.exe"
  WriteUninstaller "$INSTDIR\\Uninstall.exe"

  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "DisplayName" "BlockForge Launcher"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "Publisher" "BlockForge"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "DisplayIcon" "$INSTDIR\\BlockForge.exe"
  WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "UninstallString" "$INSTDIR\\Uninstall.exe"
  WriteRegDWORD HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "NoModify" 1
  WriteRegDWORD HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\\BlockForge Launcher.lnk"
  Delete "$SMPROGRAMS\\BlockForge\\BlockForge Launcher.lnk"
  RMDir "$SMPROGRAMS\\BlockForge"
  DeleteRegKey HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BlockForge"
  RMDir /r "$INSTDIR"
SectionEnd
