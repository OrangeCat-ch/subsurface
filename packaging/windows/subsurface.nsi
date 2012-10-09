#
# Subsurface NSIS installer script
#
# This installer creator needs to be run with:
# makensis subsurface.nsi
#
# It assumes that packaging/windows/dll is a symlink to
# the directory in which the required Windows DLLs are installed
# (in my case that's /usr/i686-w64-mingw32/sys-root/mingw/bin)
#

#--------------------------------
# Include Modern UI

    !include "MUI2.nsh"

#--------------------------------
# General

    # Program version
    !define SUBSURFACE_VERSION "2.0.1"

    # VIProductVersion requires version in x.x.x.x format
    !define SUBSURFACE_VIPRODUCTVERSION "2.0.1.0"

    # Installer name and filename
    Name "Subsurface"
    Caption "Subsurface ${SUBSURFACE_VERSION} Setup"
    OutFile "subsurface-${SUBSURFACE_VERSION}.exe"

    # Icon to use for the installer
    !define MUI_ICON "subsurface.ico"

    # Default installation folder
    InstallDir "$PROGRAMFILES\Subsurface"

    # Get installation folder from registry if available
    InstallDirRegKey HKCU "Software\Subsurface" ""

    # Request application privileges
    RequestExecutionLevel admin

#--------------------------------
# Version information

    VIProductVersion "${SUBSURFACE_VIPRODUCTVERSION}"
    VIAddVersionKey "ProductName" "Subsurface"
    VIAddVersionKey "FileDescription" "Subsurface - an open source dive log program."
    VIAddVersionKey "FileVersion" "${SUBSURFACE_VERSION}"
    VIAddVersionKey "LegalCopyright" "GPL v.2"
    VIAddVersionKey "ProductVersion" "${SUBSURFACE_VERSION}"

#--------------------------------
# Settings

    # Show a warn on aborting installation
    !define MUI_ABORTWARNING

    # Defines the target start menu folder
    !define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKCU"
    !define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Subsurface"
    !define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"

#--------------------------------
# Variables

    Var StartMenuFolder

#--------------------------------
# Pages

    # Installer pages
    !insertmacro MUI_PAGE_LICENSE "..\..\gpl-2.0.txt"
    !insertmacro MUI_PAGE_DIRECTORY
    !insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder
    !insertmacro MUI_PAGE_INSTFILES

    # Uninstaller pages
    !insertmacro MUI_UNPAGE_CONFIRM
    !insertmacro MUI_UNPAGE_INSTFILES

#--------------------------------
# Languages

    !insertmacro MUI_LANGUAGE "English"

#--------------------------------
# Default installer section

Section
    SetShellVarContext all

    # Installation path
    SetOutPath "$INSTDIR"

    # Delete any already installed DLLs to avoid buildup of various
    # versions of the same library when upgrading
    Delete "$INSTDIR\*.dll"

    # Files to include in installer
    File ..\..\subsurface.exe
    File ..\..\subsurface.svg
    File ..\..\xslt\jdivelog2subsurface.xslt
    File ..\..\xslt\SuuntoSDM.xslt
    File dll\iconv.dll
    File dll\libatk-1.0-0.dll
    File dll\libcairo-2.dll
    File dll\libdivecomputer-0.dll
    File dll\libffi-6.dll
    File dll\libfontconfig-1.dll
    File dll\libfreetype-6.dll
    File dll\libgdk-win32-2.0-0.dll
    File dll\libgdk_pixbuf-2.0-0.dll
    File dll\libgio-2.0-0.dll
    File dll\libglib-2.0-0.dll
    File dll\libgmodule-2.0-0.dll
    File dll\libgobject-2.0-0.dll
    File dll\libgthread-2.0-0.dll
    File dll\libgtk-win32-2.0-0.dll
    File dll\libintl-8.dll
    File dll\libjasper-1.dll
    File dll\libjpeg-62.dll
    File dll\libpango-1.0-0.dll
    File dll\libpangocairo-1.0-0.dll
    File dll\libpangoft2-1.0-0.dll
    File dll\libpangowin32-1.0-0.dll
    File dll\libpixman-1-0.dll
    File dll\libpng15-15.dll
    File dll\libtiff-3.dll
    File dll\libusb-1.0.dll
    File dll\libxml2-2.dll
    File dll\libxslt-1.dll
    File dll\pthreadGC2.dll
    File dll\zlib1.dll
    File subsurface.ico

    # Store installation folder in registry
    WriteRegStr HKCU "Software\Subsurface" "" $INSTDIR

    # Create shortcuts
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
        CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Subsurface.lnk" "$INSTDIR\subsurface.exe"
        CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Uninstall Subsurface.lnk" "$INSTDIR\Uninstall.exe"
    !insertmacro MUI_STARTMENU_WRITE_END

    # Create the uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

SectionEnd

#--------------------------------
# Uninstaller section

Section "Uninstall"
    SetShellVarContext all

    # Delete installed files
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\*.xslt"
    Delete "$INSTDIR\subsurface.exe"
    Delete "$INSTDIR\subsurface.ico"
    Delete "$INSTDIR\subsurface.svg"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"

    # Remove shortcuts
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
    Delete "$SMPROGRAMS\$StartMenuFolder\Subsurface.lnk"
    Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall Subsurface.lnk"
    RMDir "$SMPROGRAMS\$StartMenuFolder"

    # Remove registry entries
    DeleteRegKey /ifempty HKCU "Software\Subsurface"

SectionEnd
