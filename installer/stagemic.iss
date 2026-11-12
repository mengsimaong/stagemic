; SPDX-License-Identifier: AGPL-3.0-or-later
;
; StageMic Windows installer (Inno Setup 6).
; Built by GitHub Actions:  ISCC /DAppVersion=x.y.z /DSourceDir=<package folder> stagemic.iss
;
; Where the OBS plugin goes:
;   OBS installed normally  -> C:\ProgramData\obs-studio\plugins\stagemic\bin\64bit\stagemic.dll
;                              (loaded by OBS 31, 32 and 33)
;   OBS from a zip/portable -> <OBS>\obs-plugins\64bit\stagemic.dll
;                              <OBS>\data\obs-plugins\stagemic\...
;                              (OBS loads this location in both portable and normal mode,
;                               on OBS 31-33; checked against OBS 33 source)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\package"
#endif

[Setup]
AppId={{6B3D2E4A-8C1F-4F7B-9E2D-5A1C7B3E9F40}
AppName=Paopoa StageMic
AppVersion={#AppVersion}
AppVerName=Paopoa StageMic {#AppVersion}
AppPublisher=mengsimaong
UninstallDisplayName=Paopoa StageMic for OBS Studio
DefaultDirName={commonappdata}\StageMic
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=StageMic-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
LicenseFile={#SourceDir}\LICENSE.txt
CloseApplications=no
SetupLogging=yes

[Types]
Name: "standard"; Description: "Paopoa StageMic for OBS Studio"
Name: "full"; Description: "Paopoa StageMic for OBS Studio + VST3 plugin for music apps"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "obs"; Description: "Paopoa StageMic for OBS Studio"; Types: standard full custom; Flags: fixed
Name: "vst3"; Description: "Paopoa StageMic VST3 (optional, for music apps such as Reaper)"; Types: full

[Files]
Source: "{#SourceDir}\obs-plugins\stagemic\bin\64bit\*"; DestDir: "{code:PluginBinDir}"; Components: obs; Flags: ignoreversion
Source: "{#SourceDir}\obs-plugins\stagemic\data\*"; DestDir: "{code:PluginDataDir}"; Components: obs; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\vst3\StageMic.vst3\*"; DestDir: "{commoncf64}\VST3\StageMic.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: dirifempty; Name: "{code:PluginDataDir}"
Type: dirifempty; Name: "{commonappdata}\obs-studio\plugins\stagemic\bin\64bit"
Type: dirifempty; Name: "{commonappdata}\obs-studio\plugins\stagemic\bin"
Type: dirifempty; Name: "{commonappdata}\obs-studio\plugins\stagemic"

[Registry]
; Remember where we installed, so updates and the uninstaller use the same place.
Root: HKLM; Subkey: "Software\StageMic"; ValueType: string; ValueName: "ObsFolder"; ValueData: "{code:SavedObsFolder}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\StageMic"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"; Flags: uninsdeletekey

[Messages]
FinishedLabel=Paopoa StageMic is installed.%n%nStart OBS, open your microphone's Filters (the three-dots menu next to it in the Audio Mixer), click + and choose Paopoa StageMic.

[Code]
var
  ObsTypePage: TInputOptionWizardPage;
  ObsFolderPage: TInputDirWizardPage;

function IsProcessRunning(const ExeName: String): Boolean;
var
  Locator, Service, Found: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('', 'root\CIMV2', '', '');
    Found := Service.ExecQuery('SELECT Name FROM Win32_Process WHERE Name="' + ExeName + '"');
    Result := Found.Count > 0;
  except
    Result := False;
  end;
end;

function AskToCloseObs(): Boolean;
begin
  Result := True;
  while IsProcessRunning('obs64.exe') do
  begin
    if MsgBox('OBS Studio is open.' + #13#10#13#10 +
              'Please close OBS completely (also from the system tray near the clock), then click Retry.',
              mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := AskToCloseObs();
end;

function InitializeUninstall(): Boolean;
begin
  Result := AskToCloseObs();
end;

function InstalledObsFolder(): String;
begin
  Result := '';
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Result);
end;

function UsePortable(): Boolean;
begin
  Result := (ObsTypePage <> nil) and ObsTypePage.Values[1];
end;

function PortableFolder(): String;
begin
  Result := RemoveBackslashUnlessRoot(ObsFolderPage.Values[0]);
end;

function PluginBinDir(Param: String): String;
begin
  if UsePortable() then
    Result := PortableFolder() + '\obs-plugins\64bit'
  else
    Result := ExpandConstant('{commonappdata}') + '\obs-studio\plugins\stagemic\bin\64bit';
end;

function PluginDataDir(Param: String): String;
begin
  if UsePortable() then
    Result := PortableFolder() + '\data\obs-plugins\stagemic'
  else
    Result := ExpandConstant('{commonappdata}') + '\obs-studio\plugins\stagemic\data';
end;

function SavedObsFolder(Param: String): String;
begin
  if UsePortable() then
    Result := PortableFolder()
  else
    Result := '';
end;

procedure InitializeWizard();
var
  Previous: String;
begin
  ObsTypePage := CreateInputOptionPage(wpLicense,
    'Your OBS Studio', 'How did you get OBS Studio?',
    'Choose the option that matches your computer. If you are not sure, keep the first one.',
    True, False);
  ObsTypePage.Add('I installed OBS Studio normally (from obsproject.com, Steam, or the Microsoft Store)');
  ObsTypePage.Add('I use a zip ("portable") copy of OBS Studio that I unzipped into a folder myself');

  ObsFolderPage := CreateInputDirPage(ObsTypePage.ID,
    'Portable OBS folder', 'Where is your OBS Studio folder?',
    'Choose the OBS folder that contains the "bin" and "data" folders, then click Next.',
    False, '');
  ObsFolderPage.Add('');

  // Defaults: reuse the choice from a previous install, otherwise detect installed OBS.
  Previous := '';
  RegQueryStringValue(HKLM, 'Software\StageMic', 'ObsFolder', Previous);
  if Previous <> '' then
  begin
    ObsTypePage.Values[1] := True;
    ObsFolderPage.Values[0] := Previous;
  end
  else
  begin
    ObsTypePage.Values[0] := True;
    ObsFolderPage.Values[0] := ExpandConstant('{userdesktop}');
  end;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = ObsFolderPage.ID) and not UsePortable();
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if (CurPageID = ObsTypePage.ID) and (not UsePortable()) and (InstalledObsFolder() = '') then
    Result := MsgBox('Setup could not find an installed OBS Studio on this computer.' + #13#10#13#10 +
                     'If you use a zip/portable copy, click No and choose the second option.' + #13#10 +
                     'Continue anyway?', mbConfirmation, MB_YESNO) = IDYES;

  if (CurPageID = ObsFolderPage.ID) and
     not FileExists(PortableFolder() + '\bin\64bit\obs64.exe') then
  begin
    MsgBox('That folder doesn''t look like OBS Studio.' + #13#10#13#10 +
           'Choose the folder that contains "bin\64bit\obs64.exe" (the one with the bin and data folders).',
           mbError, MB_OK);
    Result := False;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := 'Paopoa StageMic will be installed into:' + NewLine + Space + PluginBinDir('') + NewLine;
  if MemoComponentsInfo <> '' then
    Result := Result + NewLine + MemoComponentsInfo;
end;
