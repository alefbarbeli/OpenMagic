; OpenMagic installer (Inno Setup 6).
;
; Built by scripts\build.ps1, which stages the signed driver packages in
; out\package and passes AppVersion and CertThumbprint. System changes are
; made by helpers\OpenMagicSetup.ps1 (64-bit PowerShell).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef CertThumbprint
  #error CertThumbprint is not defined; build with scripts\build.ps1
#endif
#define RepoUrl "https://github.com/alefbarbeli/OpenMagic"

[Setup]
AppId={{DC1AE4EE-F841-4C1A-A7B2-DE6B4B0DDE84}
AppName=OpenMagic
AppVersion={#AppVersion}
AppVerName=OpenMagic {#AppVersion}
AppPublisher=OpenMagic Project
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
AppCopyright=Copyright (c) 2026 Alef Barbeli
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany=OpenMagic Project
VersionInfoDescription=OpenMagic Setup
VersionInfoProductName=OpenMagic
VersionInfoProductVersion={#AppVersion}
DefaultDirName={autopf}\OpenMagic
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
SetupIconFile=..\assets\openmagic.ico
UninstallDisplayIcon={app}\openmagic.ico
UninstallDisplayName=OpenMagic
WizardStyle=modern
ShowLanguageDialog=auto
WizardImageFile=wizard-100.bmp,wizard-200.bmp
WizardSmallImageFile=wizard-small-100.bmp,wizard-small-200.bmp
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
CloseApplications=no
SetupLogging=yes
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=OpenMagic-Setup-{#AppVersion}

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "keyboard"; Description: "{cm:ComponentKeyboard}"; Types: full custom
Name: "mouse"; Description: "{cm:ComponentMouse}"; Types: full custom

[Files]
Source: "..\out\package\keyboard\*"; DestDir: "{app}\drivers\keyboard"; Components: keyboard; Flags: ignoreversion
Source: "..\out\package\mouse\*"; DestDir: "{app}\drivers\mouse"; Components: mouse; Flags: ignoreversion
Source: "..\tools\OpenMagic-Keymap.ps1"; DestDir: "{app}\tools"; Components: keyboard; Flags: ignoreversion
Source: "..\tools\Watch-Keys.ps1"; DestDir: "{app}\tools"; Components: keyboard; Flags: ignoreversion
Source: "..\tools\OpenMagic-Mouse.ps1"; DestDir: "{app}\tools"; Components: mouse; Flags: ignoreversion
Source: "helpers\OpenMagicSetup.ps1"; DestDir: "{app}\setup"; Flags: ignoreversion
Source: "..\certs\OpenMagic-TestSigning.cer"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\openmagic.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\OpenMagic\{cm:IconProject}"; Filename: "{#RepoUrl}"
Name: "{autoprograms}\OpenMagic\{cm:IconUninstall}"; Filename: "{uninstallexe}"

[Run]
Filename: "{#RepoUrl}#readme"; Description: "{cm:RunReadme}"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: files; Name: "{app}\setup\setup.log"

[CustomMessages]
en.TypeFull=Full installation
en.TypeCustom=Custom installation
en.ComponentKeyboard=Magic Keyboard driver: Fn media keys, Fn shortcuts and configurable F13-F19
en.ComponentMouse=Magic Mouse 2 driver: smooth vertical and horizontal scrolling
en.IconProject=OpenMagic on GitHub
en.IconUninstall=Uninstall OpenMagic
en.RunReadme=Open the OpenMagic documentation (key maps and settings)
en.SecureBootOn=Secure Boot is turned on.%n%nOpenMagic's drivers are test-signed, and Windows only loads test-signed drivers in test-signing mode, which requires Secure Boot to be off.%n%nTurn off Secure Boot in your PC's firmware (UEFI) settings and run Setup again.
en.TestModeCaption=Driver test-signing mode
en.TestModeDescription=OpenMagic needs Windows test-signing mode to load its drivers.
en.TestModeText=OpenMagic's drivers are signed with the project's own test certificate, not by Microsoft. Windows only loads such drivers when test-signing mode is on.%n%nSetup will turn on test-signing mode (a "Test Mode" watermark appears on the desktop), trust the OpenMagic test certificate on this PC and finish installing the drivers after the restart.%n%nSome games with anti-cheat software refuse to run in test-signing mode. You can turn it off again when you uninstall OpenMagic.
en.TestModeConsent=Turn on test-signing mode (a restart is required)
en.TestModeRequired=OpenMagic's drivers cannot load without test-signing mode. Select the option to continue, or cancel Setup.
en.ConflictFound=Another driver for Apple Magic devices is installed:%n%n%1%nOpenMagic may not work while it is present. Uninstall it first, or click OK to continue anyway.
en.StatusCertificate=Trusting the OpenMagic certificate...
en.StatusTestMode=Turning on test-signing mode...
en.StatusDrivers=Installing the drivers...
en.InstallFailed=Some steps did not complete: %1.%n%nDetails are in %2.
en.UninstallTestMode=Turn off Windows test-signing mode?%n%nChoose No if other test-signed drivers on this PC still need it. A restart is required.
en.UninstallMouseReset=Turn the Magic Mouse off and on again (switch on the bottom) so it leaves multitouch mode and works with the standard Windows driver.

ptbr.TypeFull=Instalação completa
ptbr.TypeCustom=Instalação personalizada
ptbr.ComponentKeyboard=Driver do Magic Keyboard: teclas de mídia com Fn, atalhos com Fn e F13-F19 configuráveis
ptbr.ComponentMouse=Driver do Magic Mouse 2: rolagem suave vertical e horizontal
ptbr.IconProject=OpenMagic no GitHub
ptbr.IconUninstall=Desinstalar o OpenMagic
ptbr.RunReadme=Abrir a documentação do OpenMagic (mapas de teclas e configurações)
ptbr.SecureBootOn=O Secure Boot está ativado.%n%nOs drivers do OpenMagic são assinados para teste, e o Windows só carrega drivers assim no modo de teste de assinatura, que exige o Secure Boot desativado.%n%nDesative o Secure Boot nas configurações de firmware (UEFI) do PC e execute a instalação novamente.
ptbr.TestModeCaption=Modo de teste de drivers
ptbr.TestModeDescription=O OpenMagic precisa do modo de teste de assinatura do Windows para carregar os drivers.
ptbr.TestModeText=Os drivers do OpenMagic são assinados com o certificado de teste do próprio projeto, não pela Microsoft. O Windows só carrega esses drivers com o modo de teste de assinatura ativado.%n%nA instalação vai ativar o modo de teste (a marca "Modo de Teste" aparece na área de trabalho), confiar no certificado de teste do OpenMagic neste PC e concluir a instalação dos drivers depois da reinicialização.%n%nAlguns jogos com anti-cheat não funcionam no modo de teste. Você pode desativá-lo de novo ao desinstalar o OpenMagic.
ptbr.TestModeConsent=Ativar o modo de teste de assinatura (requer reinicialização)
ptbr.TestModeRequired=Os drivers do OpenMagic não carregam sem o modo de teste de assinatura. Marque a opção para continuar ou cancele a instalação.
ptbr.ConflictFound=Outro driver para dispositivos Apple Magic está instalado:%n%n%1%nO OpenMagic pode não funcionar enquanto ele estiver presente. Desinstale-o primeiro ou clique em OK para continuar mesmo assim.
ptbr.StatusCertificate=Confiando no certificado do OpenMagic...
ptbr.StatusTestMode=Ativando o modo de teste de assinatura...
ptbr.StatusDrivers=Instalando os drivers...
ptbr.InstallFailed=Algumas etapas não foram concluídas: %1.%n%nOs detalhes estão em %2.
ptbr.UninstallTestMode=Desativar o modo de teste de assinatura do Windows?%n%nEscolha Não se outros drivers assinados para teste neste PC ainda precisam dele. É necessário reiniciar.
ptbr.UninstallMouseReset=Desligue e ligue o Magic Mouse (chave embaixo dele) para ele sair do modo multitoque e funcionar com o driver padrão do Windows.

[Code]
var
  TestModePage: TInputOptionWizardPage;
  RestartRequired: Boolean;
  UninstallRestartRequired: Boolean;

function IsSecureBootOn: Boolean;
var
  Value: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, 'SYSTEM\CurrentControlSet\Control\SecureBoot\State',
    'UEFISecureBootEnabled', Value) and (Value = 1);
end;

// Reflects the current boot: a mode turned on but not yet rebooted reads as off.
function IsTestSigningActive: Boolean;
var
  Options: String;
begin
  Result := RegQueryStringValue(HKLM64, 'SYSTEM\CurrentControlSet\Control', 'SystemStartOptions', Options)
    and (Pos('TESTSIGNING', Uppercase(Options)) > 0);
end;

function ConflictingDrivers: String;
begin
  Result := '';
  if RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\MagicMouse') then
    Result := Result + '- Magic Utilities (MagicMouse)' + #13#10;
  if RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\MagicKeyboard') then
    Result := Result + '- Magic Utilities (MagicKeyboard)' + #13#10;
  if RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\AppleWirelessMouse') then
    Result := Result + '- Apple Boot Camp (AppleWirelessMouse)' + #13#10;
  if RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\KeyMagic') then
    Result := Result + '- Apple Boot Camp (KeyMagic)' + #13#10;
end;

function RunHelper(const Arguments: String): Integer;
var
  ExitCode: Integer;
begin
  // A real working directory: by default it would be the {sysnative} path,
  // which does not exist for the 64-bit PowerShell it starts.
  if not Exec(ExpandConstant('{sysnative}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\setup\OpenMagicSetup.ps1') + '" ' + Arguments,
    ExpandConstant('{app}\setup'), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    ExitCode := -1;
  Log(Format('OpenMagicSetup.ps1 %s -> %d', [Arguments, ExitCode]));
  Result := ExitCode;
end;

function InitializeSetup: Boolean;
begin
  Result := True;
  if IsSecureBootOn then
  begin
    SuppressibleMsgBox(CustomMessage('SecureBootOn'), mbCriticalError, MB_OK, IDOK);
    Result := False;
  end;
end;

procedure InitializeWizard;
begin
  if not IsTestSigningActive then
  begin
    TestModePage := CreateInputOptionPage(wpLicense, CustomMessage('TestModeCaption'),
      CustomMessage('TestModeDescription'), CustomMessage('TestModeText'), False, False);
    TestModePage.Add(CustomMessage('TestModeConsent'));
    // Silent installs consent with /ENABLETESTMODE.
    TestModePage.Values[0] := ExpandConstant('{param:ENABLETESTMODE|0}') = '1';
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Conflicts: String;
begin
  Result := True;
  if CurPageID = wpWelcome then
  begin
    Conflicts := ConflictingDrivers;
    if (Conflicts <> '') and
       (SuppressibleMsgBox(FmtMessage(CustomMessage('ConflictFound'), [Conflicts]), mbInformation, MB_OKCANCEL, IDOK) = IDCANCEL) then
      Result := False;
  end
  else if (TestModePage <> nil) and (CurPageID = TestModePage.ID) and not TestModePage.Values[0] then
  begin
    SuppressibleMsgBox(CustomMessage('TestModeRequired'), mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

function SelectedInfs: String;
begin
  Result := '';
  if WizardIsComponentSelected('keyboard') then
    Result := ExpandConstant('{app}\drivers\keyboard\OpenMagicKbd.inf');
  if WizardIsComponentSelected('mouse') then
  begin
    if Result <> '' then
      Result := Result + ';';
    // The Bluetooth side first: it is the one that turns multitouch on.
    Result := Result + ExpandConstant('{app}\drivers\mouse\OpenMagicMouseBt.inf;{app}\drivers\mouse\OpenMagicMouse.inf');
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Infs, Failures: String;
  Code: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  Failures := '';
  Infs := SelectedInfs;

  WizardForm.StatusLabel.Caption := CustomMessage('StatusCertificate');
  if RunHelper('-Action TrustCertificate -Certificate "' + ExpandConstant('{app}\OpenMagic-TestSigning.cer') + '"') <> 0 then
    Failures := Failures + 'certificate ';

  if IsTestSigningActive then
  begin
    if Infs <> '' then
    begin
      WizardForm.StatusLabel.Caption := CustomMessage('StatusDrivers');
      Code := RunHelper('-Action InstallDrivers -Inf "' + Infs + '"');
      if Code = 3010 then
        RestartRequired := True
      else if Code <> 0 then
        Failures := Failures + 'drivers ';
    end;
  end
  else
  begin
    // Drivers installed now could not load before the restart and would take
    // the keyboard and mouse down with them, so their installation waits.
    WizardForm.StatusLabel.Caption := CustomMessage('StatusTestMode');
    if RunHelper('-Action EnableTestMode') = 3010 then
      RestartRequired := True
    else
      Failures := Failures + 'test-mode ';
    if Infs <> '' then
    begin
      WizardForm.StatusLabel.Caption := CustomMessage('StatusDrivers');
      if RunHelper('-Action ScheduleInstall -Inf "' + Infs + '"') <> 0 then
        Failures := Failures + 'drivers ';
    end;
  end;

  if Failures <> '' then
    SuppressibleMsgBox(FmtMessage(CustomMessage('InstallFailed'), [Trim(Failures), ExpandConstant('{app}\setup\setup.log')]),
      mbError, MB_OK, IDOK);
end;

function NeedRestart: Boolean;
begin
  Result := RestartRequired;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  RunHelper('-Action Uninstall -Thumbprint {#CertThumbprint}');
  if FileExists(ExpandConstant('{app}\drivers\mouse\OpenMagicMouse.inf')) then
    SuppressibleMsgBox(CustomMessage('UninstallMouseReset'), mbInformation, MB_OK, IDOK);
  if IsTestSigningActive and
     (SuppressibleMsgBox(CustomMessage('UninstallTestMode'), mbConfirmation, MB_YESNO, IDNO) = IDYES) and
     (RunHelper('-Action DisableTestMode') = 3010) then
    UninstallRestartRequired := True;
end;

function UninstallNeedRestart: Boolean;
begin
  Result := UninstallRestartRequired;
end;
