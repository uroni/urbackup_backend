!define MUI_BRANDINGTEXT "UrBackup Server v1.2"
!include "${NSISDIR}\Contrib\Modern UI\System.nsh"
!include WinVer.nsh
!include "x64.nsh"
!define MUI_ICON "backup-ok.ico"

SetCompressor /FINAL /SOLID lzma

CRCCheck On
Name "UrBackup Server 1.2"
OutFile "UrBackup Server 1.2-1.exe"
InstallDir "$PROGRAMFILES\UrBackupServer"
RequestExecutionLevel highest

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!define MUI_LANGDLL_ALLLANGUAGES

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!define MUI_LANGDLL_REGISTRY_ROOT "HKCU" 
!define MUI_LANGDLL_REGISTRY_KEY "Software\UrBackupServer" 
!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"


!define MUI_CUSTOMFUNCTION_GUIINIT myGuiInit


!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "French"

!insertmacro MUI_RESERVEFILE_LANGDLL

!define Unicode2Ansi "!insertmacro Unicode2Ansi"

!macro Unicode2Ansi String outVar
 System::Call 'kernel32::WideCharToMultiByte(i 0, i 0, w "${String}", i -1, t .s, i ${NSIS_MAX_STRLEN}, i 0, i 0) i'
 Pop "${outVar}"
!macroend  
 

Section "install"
	${If} ${RunningX64}
		!insertmacro DisableX64FSRedirection
		SetRegView 64
	${EndIf}
	
	SetOutPath "$TEMP"
	${If} ${RunningX64}
		; Push $R0
   		; ClearErrors
   		; ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{DA5E371C-6333-3D8A-93A4-6FD5B20BCC6E}" "Version"
	   	; IfErrors 0 VSRedistInstalled64
		; inetc::get "http://www.urserver.de/vc10/vcredist_x64.exe" $TEMP\vcredist_x64.exe
		; Pop $0
		; ExecWait '"$TEMP\vcredist_x64.exe" /q'  
		; Delete '$TEMP\vcredist_x64.exe'
; VSRedistInstalled64:
		; Push $R0
   		; ClearErrors
   		; ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{8220EEFE-38CD-377E-8595-13398D740ACE}" "Version"
	   	; IfErrors 0 VSRedist90Installed64
		; inetc::get "http://www.urserver.de/vc90/vcredist_x64.exe" $TEMP\vcredist_x64.exe
		; Pop $0
		; ExecWait '"$TEMP\vcredist_x64.exe" /q'  
		; Delete '$TEMP\vcredist_x64.exe'
; VSRedist90Installed64:
		File "vcredist\vcredist_2008_x64.exe"
		ExecWait '"$TEMP\vcredist_2008_x64.exe" /q /norestart'
		File "vcredist\vcredist_2010sp1_x64.exe"
		ExecWait '"$TEMP\vcredist_2010sp1_x64.exe" /q /norestart'
		File "vcredist\idndl.amd64.exe"
		${If} ${IsWinXP}
			ExecWait '"$TEMP\idndl.amd64.exe" /q /norestart'
		${EndIf}
		${If} ${IsWin2003}
			ExecWait '"$TEMP\idndl.amd64.exe" /q /norestart'
		${EndIf}
	${Else}
		; ReadRegStr $0 HKLM "SOFTWARE\Microsoft\VisualStudio\10.0\VC\Runtimes\x86" 'Installed'
		; ${If} $0 != '1'
			; ReadRegStr $0 HKLM "SOFTWARE\Microsoft\VisualStudio\10.0\VC\VCRedist\x86" 'Installed'
			; ${If} $0 != '1'
				; inetc::get "http://www.urserver.de/vc10/vcredist_x86.exe" $TEMP\vcredist_x86.exe
				; Pop $0
				; ExecWait '"$TEMP\vcredist_x86.exe" /q'   
				; Delete '$TEMP\vcredist_x86.exe'
			; ${EndIf}
		; ${EndIf}
		; Push $R0
   		; ClearErrors
   		; ReadRegDword $R0 HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{9A25302D-30C0-39D9-BD6F-21E6EC160475}" "Version"
	   	; IfErrors 0 VSRedist90Installed86
		; inetc::get "http://www.urserver.de/vc90/vcredist_x86.exe" $TEMP\vcredist_x86.exe
		; Pop $0
		; ExecWait '"$TEMP\vcredist_x86.exe" /q'  
		; Delete '$TEMP\vcredist_x86.exe'
; VSRedist90Installed86:
		File "vcredist\vcredist_2008_x86.exe"
		ExecWait '"$TEMP\vcredist_2008_x86.exe" /q /norestart'
		File "vcredist\vcredist_2010sp1_x86.exe"
		ExecWait '"$TEMP\vcredist_2010sp1_x86.exe" /q /norestart'
		File "vcredist\idndl.x86.exe"
		${If} ${IsWinXP}
			ExecWait '"$TEMP\idndl.x86.exe" /q /norestart'
		${EndIf}
		${If} ${IsWin2003}
			ExecWait '"$TEMP\idndl.x86.exe" /q /norestart'
		${EndIf}
	${EndIf}
	
	${Unicode2Ansi} "UrBackupWinServer" $R0
	SimpleSC::ExistsService "$R0"
	Pop $0
	${If} $0 == '0'
		SimpleSC::StopService "$R0"
		Pop $0
	${EndIf}
	
	Sleep 500
	
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" "DisplayName" "UrBackupServer (remove only)"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" "UninstallString" "$INSTDIR\Uninstall.exe"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" "Path" "$INSTDIR"
	
	SetOutPath "$INSTDIR"
	File "data_common\args.txt"
	File "data_common\args_prevista.txt"
	File "data_common\license.txt"
	File "data_common\cleanup.bat"
	File "data_common\remove_unknown.bat"
	File "data_common\reset_pw.bat"
	File "data_common\cleanup_database.bat"
	
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	
	${IfNot} ${RunningX64} 
		File "data\downloadplugin.dll"
		File "data\fsimageplugin.dll"
		File "data\urbackupserver.dll"
		File "data\urbackupserver_prevista.dll"
		File "data\pychart.dll"
		File "data\httpserver.dll"
		File "data\cryptoplugin.dll"
		File "data_service\urbackup_srv.exe"
		File "data\curllib.dll"
		File "data\libssl32.dll"
		File "data\libeay32.dll"
		File "data\urlplugin.dll"
		File /r "data_python\*"
		SetOutPath "$INSTDIR\lib"
		File "data_common\lib\site.py"
		SetOutPath "$INSTDIR"
	${Else}
		File "data_x64\downloadplugin.dll"
		File "data_x64\fsimageplugin.dll"
		File "data_x64\urbackupserver.dll"
		File "data_x64\urbackupserver_prevista.dll"
		File "data_x64\pychart.dll"
		File "data_x64\httpserver.dll"
		File "data_service_x64\urbackup_srv.exe"
		File "data_x64\libcurl.dll"
		File "data_x64\cryptoplugin.dll"
		File "data_x64\zlib1.dll"
		File "data_x64\ssleay32.dll"
		File "data_x64\libeay32.dll"
		File "data_x64\urlplugin.dll"
		File /r "data_python_x64\*"
		SetOutPath "$INSTDIR\lib"
		File "data_common\lib\site.py"
		SetOutPath "$INSTDIR"
	${EndIf}
	SetOutPath "$INSTDIR\pychart"
	File "data_common\pychart\pychart.py"
	SetOutPath "$INSTDIR\urbackup"
	File "data_common\urbackup\backup_server_init.sql"
	File "data_common\urbackup\status.htm"
	SetOutPath "$INSTDIR\urbackup\www"
	File "data_common\urbackup\www\arr.png"
	File "data_common\urbackup\www\favico.ico"
	File "data_common\urbackup\www\header.png"
	File "data_common\urbackup\www\index.htm"
	File "data_common\urbackup\www\indicator.gif"
	File "data_common\urbackup\www\layout.css"
	File "data_common\urbackup\www\md5.js"
	File "data_common\urbackup\www\progress.png"
	File "data_common\urbackup\www\prototype.js"
	File "data_common\urbackup\www\templates.js"
	File "data_common\urbackup\www\translation.js"
	File "data_common\urbackup\www\urbackup.js"
	File "data_common\urbackup\www\urbackup_functions.js"
	File "data_common\urbackup\www\tabber-minimized.js"
	File "data_common\urbackup\www\stopwatch.png"
	
	
	${IfNot} ${RunningX64}
		${If} ${IsWinXP}
			StrCpy $0 "$INSTDIR\args_prevista.txt" ;Path of copy file from
			StrCpy $1 "$INSTDIR\args.txt"   ;Path of copy file to
			StrCpy $2 0 ; only 0 or 1, set 0 to overwrite file if it already exists
			System::Call 'kernel32::CopyFile(t r0, t r1, b r2) l'
			Pop $0
			;SetRebootFlag true
		${EndIf}
		${If} ${IsWin2003}
			StrCpy $0 "$INSTDIR\args_prevista.txt" ;Path of copy file from
			StrCpy $1 "$INSTDIR\args.txt"   ;Path of copy file to
			StrCpy $2 0 ; only 0 or 1, set 0 to overwrite file if it already exists
			System::Call 'kernel32::CopyFile(t r0, t r1, b r2) l'
			Pop $0
			;SetRebootFlag true
		${EndIf}
	${Else}
		${If} ${IsWin2003}
			StrCpy $0 "$INSTDIR\args_prevista.txt" ;Path of copy file from
			StrCpy $1 "$INSTDIR\args.txt"   ;Path of copy file to
			StrCpy $2 0 ; only 0 or 1, set 0 to overwrite file if it already exists
			System::Call 'kernel32::CopyFile(t r0, t r1, b r2) l'
			Pop $0
			;SetRebootFlag true
		${EndIf}
	${EndIf}
	
	CreateDirectory "$SMPROGRAMS\UrBackup Server"
	CreateShortCut "$SMPROGRAMS\UrBackup Server\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
	CreateShortCut "$SMPROGRAMS\UrBackup Server\UrBackup Server Interface.lnk" "http://localhost:55414" "" "$INSTDIR\urbackup\www\favico.ico" 0
	;SetOutPath "$SMPROGRAMS\UrBackup Server"
	;File "data_common\UrBackup Server Interface.htm"
	
	${If} ${IsWinXP}
		nsisFirewallW::AddAuthorizedApplication "$INSTDIR\urbackup_srv.exe" "UrBackup Windows Server"
	${ElseIf} ${IsWin2003}
		nsisFirewallW::AddAuthorizedApplication "$INSTDIR\urbackup_srv.exe" "UrBackup Windows Server"
	${Else}
		liteFirewallW::AddRule "$INSTDIR\urbackup_srv.exe" "UrBackup Windows Server"
	${EndIf}
	Pop $0
	
	
	${Unicode2Ansi} "UrBackupWinServer" $R0
	${Unicode2Ansi} "UrBackup Windows Server" $R1
	${Unicode2Ansi} "16" $R2
	${Unicode2Ansi} "2" $R3
	${Unicode2Ansi} "$INSTDIR\urbackup_srv.exe" $R4
	SimpleSC::ExistsService "$R0"
	Pop $0
	${If} $0 != '0'
		SimpleSC::InstallService "$R0" "$R1" "$R2" "$R3" "$R4" "" "" ""
		Pop $0
	${EndIf}	
	SimpleSC::StartService "$R0" ""
	Pop $0
	
	${If} ${RunningX64}
		!insertmacro EnableX64FSRedirection
		SetRegView 32
	${EndIf}
	
SectionEnd

Section "Uninstall"
	${If} ${RunningX64}
		!insertmacro DisableX64FSRedirection
		SetRegView 64
	${EndIf}

	${Unicode2Ansi} "UrBackupWinServer" $R0
	SimpleSC::StopService "$R0"
	Pop $0
	SimpleSC::RemoveService "$R0"
	Pop $0
	
	Sleep 500

	RMDir /r "$INSTDIR\*.*"
	RMDir "$INSTDIR"
	
	Delete "$SMPROGRAMS\UrBackup Server\*.*"
	RMDir  "$SMPROGRAMS\UrBackup Server"

	
	DeleteRegKey HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\UrBackupServer" 
	
	${If} ${RunningX64}
		!insertmacro EnableX64FSRedirection
		SetRegView 32
	${EndIf}
	
	DeleteRegKey /ifempty HKCU "Software\UrBackupServer"
	
SectionEnd

Function .onInstSuccess
	
FunctionEnd

Function un.onInit
	!insertmacro MUI_UNGETLANGUAGE
FunctionEnd
 
Function un.onUninstSuccess
  ;MessageBox MB_OK "UrBackup Server wurde erfolgreich deinstalliert."
FunctionEnd

Function myGuiInit
	;MessageBox MB_OK "blub"
	;${If} ${RunningX64}
	;	!insertmacro DisableX64FSRedirection
	;	SetRegView 64
	;${EndIf}
FunctionEnd

Function .onInit
	${If} ${RunningX64}
		strcpy $INSTDIR "$PROGRAMFILES64\UrBackupServer"
	${EndIf}
	!insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd


