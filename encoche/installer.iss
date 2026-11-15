[Setup]
AppName=Encoche
AppVersion=1.0.0
AppPublisher=Encoche
AppMutex=Encoche.SingleInstance
DefaultDirName={autopf}\Encoche
DefaultGroupName=Encoche
PrivilegesRequired=lowest
OutputDir=dist
OutputBaseFilename=EncocheSetup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=Encoche

[Tasks]
Name: "startup"; Description: "Lancer Encoche au demarrage de Windows"

[Files]
Source: "build\Release\Encoche.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Encoche"; Filename: "{app}\Encoche.exe"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Encoche"; ValueData: """{app}\Encoche.exe"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\Encoche.exe"; Description: "Lancer Encoche"; Flags: nowait postinstall skipifsilent
