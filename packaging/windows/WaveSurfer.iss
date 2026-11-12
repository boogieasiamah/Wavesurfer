; Windows installer for WaveSurfer (VST3, 64-bit). Same layout as Flux 512.
; Built by CI:  iscc /DAppVersion=1.1.1 /DSourceDir=<...\Release\VST3> /DOutDir=<out> WaveSurfer.iss

#ifndef AppVersion
  #define AppVersion "1.1.1"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build\WaveSurfer_artefacts\Release\VST3"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif

[Setup]
AppId=QRA.WaveSurfer
AppName=WaveSurfer
AppVersion={#AppVersion}
AppPublisher=Quantum Research Audio
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
CreateUninstallRegKey=yes
UninstallDisplayName=WaveSurfer (QRA)
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir={#OutDir}
OutputBaseFilename=WaveSurfer-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#SourceDir}\WaveSurfer.vst3\*"; DestDir: "{commoncf64}\VST3\WaveSurfer.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\WaveSurfer.vst3"
