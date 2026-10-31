; Inno Setup script for the VineAudio MixCore Windows installer.
; Built by .github/workflows/build.yml:
;   iscc /DAppVersion=0.2.0 /DSourceDir=<artefacts dir> /DOutputDir=<dir> packaging\windows\MixCore.iss
;
; Installs:
;   MixCore.vst3 -> C:\Program Files\Common Files\VST3\   (standard VST3 location)
;   MixCore.exe  -> C:\Program Files\VineAudio\MixCore\             (standalone app)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\plugins\mixcore\MixCore_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6E3B1F0A-8C2D-4F7E-9A51-3D0C4B2E7F19}
AppName=VineAudio MixCore
AppVersion={#AppVersion}
AppPublisher=VineAudio
DefaultDirName={autopf}\VineAudio\MixCore
DefaultGroupName=VineAudio
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=MixCore-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=VineAudio MixCore {#AppVersion}

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#SourceDir}\VST3\MixCore.vst3\*"; DestDir: "{commoncf64}\VST3\MixCore.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Standalone\MixCore.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\MixCore"; Filename: "{app}\MixCore.exe"; Components: standalone
Name: "{group}\Uninstall MixCore"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\MixCore.vst3"
