; ============================================================================
;  KovaLive! — Windows installer (Inno Setup 6.3+)
;  Build:  iscc installer\KovaLive.iss /DSourceDir=<staged dist folder> /DAppVersion=0.1.0
;  The staged folder is produced by tools\package_windows.ps1:
;     bin\64bit\KovaLive.exe (+ Qt + libobs runtime)
;     obs-plugins\64bit\*.dll, data\libobs, data\obs-plugins\..., docs\
; ============================================================================

#if Ver < EncodeVer(6,6,0)
  #error KovaLive's installer needs Inno Setup 6.6 or newer (dark wizard style).
#endif

#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\KovaLive"
#endif
#define AppName "KovaLive!"
#define AppExe  "KovaLive.exe"
#define AppPublisher "KovaLive"
; The previous name of this app. Its installer used this AppId; we offer to remove that
; install first (settings are carried over by the app itself from %APPDATA%\HeartCast).
#define LegacyAppId "{6E3C1F8A-4B7D-4F2E-9C35-8A1D2B7E4C91}"

[Setup]
; NEVER change AppId — it is how upgrades find the existing installation.
AppId={{5CB7B6F2-5BE1-42EB-A52F-824C338FBEDA}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} Setup
DefaultDirName={autopf}\KovaLive
DefaultGroupName=KovaLive
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
; Let people choose "Install for me only" (no administrator rights; goes to
; %LOCALAPPDATA%\Programs) or "Install for all users" (needs admin; enables the virtual camera).
PrivilegesRequiredOverridesAllowed=dialog commandline
OutputDir=..\dist
OutputBaseFilename=KovaLive-{#AppVersion}-Setup
SetupIconFile=assets\setup.ico
UninstallDisplayIcon={app}\bin\64bit\{#AppExe}
UninstallDisplayName={#AppName}
; Dark wizard (Setup and Uninstall) — needs Inno Setup 6.6 or newer.
WizardStyle=modern dark
WizardSizePercent=110
WizardImageFile=assets\wizard_side_100.bmp,assets\wizard_side_125.bmp,assets\wizard_side_150.bmp,assets\wizard_side_200.bmp
WizardSmallImageFile=assets\wizard_small_100.bmp,assets\wizard_small_125.bmp,assets\wizard_small_150.bmp,assets\wizard_small_200.bmp
WizardImageStretch=no
LicenseFile=..\resources\LICENSE.txt
Compression=lzma2/ultra64
SolidCompression=yes
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
; ~ installed size hint; Inno checks free disk space before copying and shows a clear error.
ExtraDiskSpaceRequired=104857600

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Messages]
en.WelcomeLabel1=Enter the studio
en.WelcomeLabel2=This will install [name/ver] on your computer.%n%nKovaLive! is a live-streaming and recording studio built on the OBS engine. Close other streaming apps before continuing.
en.ButtonNext=&Next
en.ButtonInstall=&Install
en.FinishedHeadingLabel=The studio is ready
en.FinishedLabel=[name] has been installed. The night is yours.
en.SelectDirLabel3=Choose where KovaLive! should be installed. Your settings and scenes are stored separately in %APPDATA%\KovaLive and are kept during upgrades.
en.DiskSpaceWarning=KovaLive! needs at least %1 KB of free space, but only %2 KB is available on this drive.%n%nFree up some space or choose another drive.

[Types]
Name: "full";   Description: "Everything (recommended)"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "core";     Description: "KovaLive! (required)"; Types: full custom; Flags: fixed
Name: "vcam";     Description: "Virtual Camera (use your stream as a webcam in Discord, Zoom, Teams…)"; Types: full
Name: "packs";    Description: "Gothic overlay packs, starter scenes & stinger transitions"; Types: full
Name: "docs";     Description: "Getting Started guide & documentation"; Types: full

[Tasks]
Name: "desktopicon";   Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "startmenuicon"; Description: "Create a &Start menu shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#SourceDir}\bin\*";         DestDir: "{app}\bin";         Components: core; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\obs-plugins\*"; DestDir: "{app}\obs-plugins"; Components: core; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\data\*";        DestDir: "{app}\data";        Components: core; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "kovalive\overlays\*,kovalive\scenes\*,kovalive\stingers\*"
Source: "{#SourceDir}\data\kovalive\overlays\*"; DestDir: "{app}\data\kovalive\overlays"; Components: packs; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\data\kovalive\scenes\*";   DestDir: "{app}\data\kovalive\scenes";   Components: packs; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\data\kovalive\stingers\*"; DestDir: "{app}\data\kovalive\stingers"; Components: packs; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
Source: "{#SourceDir}\docs\*";        DestDir: "{app}\docs";        Components: docs; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\THIRD-PARTY-LICENSES.md"; DestDir: "{app}"; Components: core; Flags: ignoreversion
Source: "..\resources\LICENSE.txt";   DestDir: "{app}"; Components: core; Flags: ignoreversion
; Microsoft Visual C++ runtime — installed only if missing (see [Run])
Source: "{#SourceDir}\redist\vc_redist.x64.exe"; DestDir: "{tmp}"; Components: core; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: startmenuicon
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\bin\64bit\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime…"; Check: NeedsVCRedist and IsAdminInstallMode; Flags: waituntilterminated skipifdoesntexist
; Virtual camera: register the DirectShow camera filter (64- and 32-bit so 32-bit apps like some Zoom builds see it)
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module64.dll"""; Components: vcam; Check: IsAdminInstallMode; StatusMsg: "Registering the virtual camera…"; Flags: runhidden waituntilterminated skipifdoesntexist
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module32.dll"""; Components: vcam; Check: IsAdminInstallMode; Flags: runhidden waituntilterminated skipifdoesntexist
Filename: "{app}\bin\64bit\{#AppExe}"; Description: "Launch KovaLive!"; WorkingDir: "{app}\bin\64bit"; Flags: postinstall nowait skipifsilent
Filename: "{app}\docs\USER-GUIDE.html"; Description: "View the Getting Started guide"; Components: docs; Flags: postinstall shellexec skipifsilent unchecked skipifdoesntexist

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module64.dll"""; RunOnceId: "UnregVcam64"; Flags: runhidden skipifdoesntexist
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/u /s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module32.dll"""; RunOnceId: "UnregVcam32"; Flags: runhidden skipifdoesntexist

[UninstallDelete]
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\obs-plugins"
Type: filesandordirs; Name: "{app}\data"

[Code]
const
  { Nocturne heading colours. Inno colours are BGR. }
  ColCrimson = $3F28C8;   { #C8283F }
  ColGold    = $5A93B8;   { #B8935A }

var
  DepsPage: TOutputMsgMemoWizardPage;
  DepsReport: String;

function VCRedistInstalled: Boolean;
var Installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1);
end;

function NeedsVCRedist: Boolean;
begin
  Result := not VCRedistInstalled;
end;

function OtherStreamingAppsRunning: String;
begin
  Result := '';
  { Advisory only — the virtual camera DLL can't be replaced while a video app holds it. }
end;

procedure BuildDependencyReport;
var
  WinVer: TWindowsVersion;
begin
  GetWindowsVersionEx(WinVer);
  DepsReport := '';
  DepsReport := DepsReport + 'Windows ' + IntToStr(WinVer.Major) + '.' + IntToStr(WinVer.Minor) + ' build ' + IntToStr(WinVer.Build);
  if WinVer.Build >= 17763 then DepsReport := DepsReport + '   ✓ supported' + #13#10
  else DepsReport := DepsReport + '   ✗ Windows 10 version 1809 or newer is required' + #13#10;
  if IsWin64 then DepsReport := DepsReport + '64-bit system   ✓' + #13#10;
  if IsAdminInstallMode then
    DepsReport := DepsReport + 'Installing for all users   ✓' + #13#10
  else
    DepsReport := DepsReport + 'Installing just for you (no administrator rights needed)   ✓' + #13#10;
  if VCRedistInstalled then
    DepsReport := DepsReport + 'Microsoft Visual C++ 2015-2022 runtime   ✓ already installed' + #13#10
  else if IsAdminInstallMode then
    DepsReport := DepsReport + 'Microsoft Visual C++ 2015-2022 runtime   → will be installed automatically' + #13#10
  else
    DepsReport := DepsReport + 'Microsoft Visual C++ 2015-2022 runtime   ✗ missing — it needs administrator rights.' + #13#10 +
                  '      Go back and choose "Install for all users", or install it from https://aka.ms/vs/17/release/vc_redist.x64.exe' + #13#10;
  if not IsAdminInstallMode then
    DepsReport := DepsReport + 'Virtual Camera   → needs "Install for all users"; skipped for a just-for-me install' + #13#10;
  DepsReport := DepsReport + 'DirectX 11 graphics   → checked when KovaLive starts (update your GPU driver if it reports a problem)' + #13#10;
  DepsReport := DepsReport + #13#10 + 'Your settings, scenes and stream keys live in %APPDATA%\KovaLive and are never removed by upgrades.';
end;

procedure InitializeWizard;
begin
  { WizardStyle=modern dark gives every control a dark theme; we only tint the headings. }
  WizardForm.WelcomeLabel1.Font.Color := ColCrimson;
  WizardForm.FinishedHeadingLabel.Font.Color := ColCrimson;
  WizardForm.PageNameLabel.Font.Color := ColGold;
  WizardForm.ProgressGauge.Height := ScaleY(18);

  BuildDependencyReport;
  DepsPage := CreateOutputMsgMemoPage(wpSelectTasks,
    'Checking your system', 'KovaLive! verified what it needs.',
    'Here is what we found. Missing components are installed for you.', DepsReport);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var WinVer: TWindowsVersion;
begin
  Result := True;
  if CurPageID = DepsPage.ID then begin
    GetWindowsVersionEx(WinVer);
    if WinVer.Build < 17763 then begin
      MsgBox('KovaLive! needs Windows 10 version 1809 or newer.', mbCriticalError, MB_OK);
      Result := False;
    end else if (not IsAdminInstallMode) and (not VCRedistInstalled) then begin
      Result := MsgBox('The Microsoft Visual C++ runtime is missing, and installing it needs administrator rights.' + #13#10#13#10 +
                       'KovaLive will not start without it. Continue anyway and install the runtime yourself afterwards?',
                       mbConfirmation, MB_YESNO) = IDYES;
    end;
  end;
end;

{ ---- previous name: HeartCast Studio (different AppId) ---- }
function LegacyUninstallString: String;
var S: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#LegacyAppId}_is1', 'UninstallString', S) or
     RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#LegacyAppId}_is1', 'UninstallString', S) then
    Result := RemoveQuotes(S);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Uninst: String; Code: Integer;
begin
  Result := '';
  Uninst := LegacyUninstallString;
  if Uninst = '' then exit;
  if WizardSilent or (MsgBox('HeartCast Studio (the previous name of KovaLive!) is installed on this PC.' + #13#10#13#10 +
                             'Remove it now? Your scenes, settings and saved stream keys are kept, and KovaLive! picks them up automatically.',
                             mbConfirmation, MB_YESNO) = IDYES) then
  begin
    { /SUPPRESSMSGBOXES answers its "also delete settings?" question with the default: No. }
    if not ShellExec('', Uninst, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
      Log('HeartCast Studio uninstall did not complete (code ' + IntToStr(Code) + '); continuing.');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then begin
    if MsgBox('Also remove your KovaLive settings, scenes, logs and saved stream keys?'#13#10#13#10 +
              '(Choose No to keep them for a future reinstall. Your recordings are never deleted.)',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{userappdata}\KovaLive'), True, True, True);
  end;
end;
