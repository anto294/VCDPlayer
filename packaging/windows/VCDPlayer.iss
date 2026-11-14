; Inno Setup script for the Windows installer (built by CI, see
; .github/workflows/build.yml). Packs the same self-contained folder as the
; portable zip (ci/package-windows.sh). Defines passed on the command line:
;   AppVersion  e.g. 1.1.0
;   SourceDir   the portable folder, e.g. dist\VCDPlayer-windows-x64-portable
;   Arch        x64 or arm64
;   OutputName  installer file name without .exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Arch
  #define Arch "x64"
#endif

[Setup]
; Never change AppId: it's how upgrades and the uninstaller find the
; existing installation.
AppId={{C894A054-7CD5-47BB-8A81-FC1593A1AD85}
AppName=VCD Player
AppVersion={#AppVersion}
AppVerName=VCD Player {#AppVersion}
AppPublisher=anto294
AppPublisherURL=https://github.com/anto294/VCDPlayer
AppSupportURL=https://github.com/anto294/VCDPlayer/issues
DefaultDirName={autopf}\VCD Player
DefaultGroupName=VCD Player
DisableProgramGroupPage=yes
; Per-user install without admin rights is offered as well.
PrivilegesRequiredOverridesAllowed=dialog
LicenseFile=..\..\LICENSE
SetupIconFile=..\..\img\icon.ico
UninstallDisplayIcon={app}\VCDPlayer.exe
OutputDir=..\..\dist
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
#if Arch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
ChangesAssociations=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[CustomMessages]
en.AssocCue=Open .cue disc images with VCD Player
it.AssocCue=Apri le immagini disco .cue con VCD Player
en.CueFileType=Video CD disc image
it.CueFileType=Immagine disco Video CD

[Tasks]
Name: "cueassoc"; Description: "{cm:AssocCue}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\..\{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\VCD Player"; Filename: "{app}\VCDPlayer.exe"
Name: "{autodesktop}\VCD Player"; Filename: "{app}\VCDPlayer.exe"; Tasks: desktopicon

[Registry]
; .cue -> VCDPlayer.cue ProgID. Windows still lets the user pick another
; default app; OpenWithProgids keeps VCD Player in "Open with" either way.
Root: HKA; Subkey: "Software\Classes\.cue"; ValueType: string; ValueName: ""; ValueData: "VCDPlayer.cue"; Flags: uninsdeletevalue; Tasks: cueassoc
Root: HKA; Subkey: "Software\Classes\.cue\OpenWithProgids"; ValueType: string; ValueName: "VCDPlayer.cue"; ValueData: ""; Flags: uninsdeletevalue; Tasks: cueassoc
Root: HKA; Subkey: "Software\Classes\VCDPlayer.cue"; ValueType: string; ValueName: ""; ValueData: "{cm:CueFileType}"; Flags: uninsdeletekey; Tasks: cueassoc
Root: HKA; Subkey: "Software\Classes\VCDPlayer.cue\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\VCDPlayer.exe,0"; Tasks: cueassoc
Root: HKA; Subkey: "Software\Classes\VCDPlayer.cue\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\VCDPlayer.exe"" ""%1"""; Tasks: cueassoc
Root: HKA; Subkey: "Software\Classes\Applications\VCDPlayer.exe\SupportedTypes"; ValueType: string; ValueName: ".cue"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\VCDPlayer.exe"; Description: "{cm:LaunchProgram,VCD Player}"; Flags: nowait postinstall skipifsilent
