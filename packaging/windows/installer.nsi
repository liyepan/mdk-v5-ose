; uVision5-OSE 安装包脚本（NSIS 3.x / Unicode）
;
; CI 会传进来：
;   /DAPP_VERSION   版本号，例如 0.2.0
;   /DDEPLOY_DIR    windeployqt 之后的目录（可执行文件 + Qt 运行库 + plugins）
;   /DOUTPUT_FILE   输出 exe 的完整路径
;   /DICON_FILE     可选，安装包图标
;   /DARCH_SUFFIX   可选，arm64 时用来区分说明文字
;
; 本地手动打：makensis /DAPP_VERSION=0.2.0 /DDEPLOY_DIR=deploy installer.nsi

Unicode true

!include "MUI2.nsh"
!include "LogicLib.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "0.0.0"
!endif
!ifndef DEPLOY_DIR
  !error "DEPLOY_DIR 未定义（用 /DDEPLOY_DIR=... 指定 windeployqt 的输出目录）"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "uVision5-OSE-${APP_VERSION}-win64-setup.exe"
!endif
!ifndef ARCH_SUFFIX
  !define ARCH_SUFFIX "win64"
!endif

!define APP_NAME      "uVision5-OSE"
!define APP_EXE       "mdk-v5-ose.exe"
!define APP_PUBLISHER "linuxacme"
!define APP_REGKEY    "Software\uVision5OSE"
!define APP_PROGID    "uVision5OSE.uvprojx"
!define APP_EXT       ".uvprojx"
!define UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\uVision5OSE"

Name "${APP_NAME} ${APP_VERSION} (${ARCH_SUFFIX})"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "${APP_REGKEY}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "启动 ${APP_NAME}"
!ifdef ICON_FILE
  !define MUI_ICON "${ICON_FILE}"
  !define MUI_UNICON "${ICON_FILE}"
!endif

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetRegView 64          ; 只能写在 Section / Function 里
  SetOutPath "$INSTDIR"
  File /r "${DEPLOY_DIR}\*.*"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; 开始菜单
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"
  ; 桌面
  CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  ; 安装位置
  WriteRegStr HKLM "${APP_REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${APP_REGKEY}" "Version" "${APP_VERSION}"

  ; 关联 .uvprojx：只登记 OpenWithProgids，不抢默认程序（Keil 还是默认打开）
  WriteRegStr HKLM "Software\Classes\${APP_PROGID}" "" "uVision 工程文件"
  WriteRegStr HKLM "Software\Classes\${APP_PROGID}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKLM "Software\Classes\${APP_PROGID}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKLM "Software\Classes\${APP_EXT}\OpenWithProgids" "${APP_PROGID}" ""

  ; 控制面板卸载项
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayName"     "${APP_NAME}"
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayVersion"  "${APP_VERSION}"
  WriteRegStr   HKLM "${UNINST_KEY}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr   HKLM "${UNINST_KEY}" "DisplayIcon"     "$INSTDIR\${APP_EXE},0"
  WriteRegStr   HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr   HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

SectionEnd

Section "Uninstall"
  SetRegView 64
  ; 先删文件，再删目录（Qt 的 plugins 子目录也要一起清）
  RMDir /r "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\卸载 ${APP_NAME}.lnk"
  RMDir  "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  DeleteRegKey HKLM "Software\Classes\${APP_PROGID}"
  DeleteRegValue HKLM "Software\Classes\${APP_EXT}\OpenWithProgids" "${APP_PROGID}"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "${APP_REGKEY}"
SectionEnd
