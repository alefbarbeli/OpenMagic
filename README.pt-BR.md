<p align="center">
  <img src="assets/openmagic.png" width="112" alt="OpenMagic">
</p>

<h1 align="center">OpenMagic</h1>

<p align="center">
  Drivers de código aberto para usar o Apple Magic Keyboard e o Magic Mouse 2 no Windows.
  <br>
  Teclas de mídia com Fn e teclas configuráveis no teclado, rolagem suave no mouse.
</p>

<p align="center"><a href="README.md">Read in English</a></p>

---

## Recursos

**Magic Keyboard**

- **Fn + F7…F12** controlam mídia e volume: anterior, play/pause, próxima, mudo, volume - e volume +.
- **Atalhos com Fn**: Fn+F3 abre a Visão de Tarefas, Fn+F4 abre a Pesquisa, Fn+Backspace apaga para a frente (Delete), Fn+setas viram Page Up, Page Down, Home e End.
- **F13–F15** funcionam como Print Screen, Scroll Lock e Pause. F16–F19 ficam livres para os seus atalhos.
- **Qualquer tecla pode ser remapeada**, com ou sem Fn, inclusive para combinações de teclas.
- Funciona por **Bluetooth e USB**.

**Magic Mouse 2**

- **Rolagem com um dedo**, vertical e horizontal, com eventos de alta resolução (rolagem suave).
- Direção **natural ou tradicional** e velocidade ajustável.
- O movimento do cursor e os cliques continuam usando o driver de mouse padrão do Windows.

## Dispositivos suportados

| Dispositivo | Modelo | Conexão | Status |
| --- | --- | --- | --- |
| Magic Keyboard com teclado numérico | A1843 (`05AC:026C`) | Bluetooth, USB | Testado |
| Magic Mouse 2 | A1657 (`004C:0269`) | Bluetooth | Testado |

## Instalação

1. Baixe o `OpenMagic-Setup-<versão>.exe` da [versão mais recente](https://github.com/alefbarbeli/OpenMagic/releases/latest).
2. Execute e escolha os drivers.
3. Reinicie quando o instalador pedir.

> [!IMPORTANT]
> Os drivers do OpenMagic são assinados com o certificado de teste do próprio projeto, não pela
> Microsoft: a assinatura de produção de drivers de kernel exige um certificado EV e a atestação da
> Microsoft. O Windows só carrega drivers assinados para teste no **modo de teste de assinatura**, então o instalador:
>
> - ativa o modo de teste (a marca "Modo de Teste" aparece na área de trabalho);
> - adiciona o certificado de teste do OpenMagic aos certificados confiáveis do PC;
> - instala os drivers na hora, ou depois da reinicialização se o modo de teste estava desligado.
>
> O modo de teste exige o **Secure Boot desativado**, e alguns jogos com anti-cheat não rodam com ele
> ligado. Se o BitLocker estiver ativo, ele é suspenso por uma reinicialização para a mudança na
> configuração de boot não pedir a chave de recuperação. A desinstalação oferece desligar o modo de teste.

Instalação silenciosa: `OpenMagic-Setup-<versão>.exe /VERYSILENT /ENABLETESTMODE=1`

Se outro driver de Magic Keyboard ou Magic Mouse estiver instalado (Magic Utilities, drivers do Boot
Camp), desinstale-o antes.

## Configuração

As ferramentas ficam em `C:\Program Files\OpenMagic\tools` e alteram as configurações num PowerShell
como administrador. O dispositivo reinicia para aplicar.

### Mapas de teclas do teclado

**Map** vale sempre; **FnMap** vale com o Fn apertado e tem prioridade. Padrões:

| Tecla | Com Fn | Sem Fn |
| --- | --- | --- |
| F3 | Win+Tab (Visão de Tarefas) | F3 |
| F4 | Win+S (Pesquisar) | F4 |
| F7 / F8 / F9 | Anterior / Play-Pause / Próxima | F7 / F8 / F9 |
| F10 / F11 / F12 | Mudo / Volume - / Volume + | F10 / F11 / F12 |
| Backspace | Delete | Backspace |
| ↑ ↓ ← → | Page Up / Page Down / Home / End | setas |
| F13 / F14 / F15 | F13 / F14 / F15 | Print Screen / Scroll Lock / Pause |

```powershell
cd 'C:\Program Files\OpenMagic\tools'
.\OpenMagic-Keymap.ps1                        # mostra os dois mapas
.\OpenMagic-Keymap.ps1 set F16 MediaStop      # F16 -> Stop
.\OpenMagic-Keymap.ps1 set F1 F21 -Fn         # Fn+F1 -> F21, para um atalho seu
.\OpenMagic-Keymap.ps1 set F17 Win+LShift+S   # até 3 teclas (captura de tela)
.\OpenMagic-Keymap.ps1 set F18 None           # desativa uma tecla
.\OpenMagic-Keymap.ps1 remove F16             # volta ao padrão
.\OpenMagic-Keymap.ps1 keys                   # nomes de teclas
```

### Mouse

```powershell
.\OpenMagic-Mouse.ps1                              # mostra as configurações
.\OpenMagic-Mouse.ps1 -ScrollSpeed 150             # 10 a 400 %
.\OpenMagic-Mouse.ps1 -NaturalScrolling 0          # direção tradicional
.\OpenMagic-Mouse.ps1 -SmoothScrolling 0           # passos clássicos da roda
.\OpenMagic-Mouse.ps1 -HorizontalScrolling 0       # sem rolagem horizontal
```

## Desinstalação

Em **Configurações > Aplicativos > Aplicativos instalados**, selecione **OpenMagic** e **Desinstalar**.
O teclado e o mouse voltam aos drivers padrão do Windows, o certificado é removido e o desinstalador
pergunta se deve desligar o modo de teste. Depois, desligue e ligue o Magic Mouse: ele continua no modo
multitoque, que o driver padrão não entende, até ser reiniciado.

## Como funciona e compilação

Veja [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (em inglês) e a seção
[Building from source](README.md#building-from-source).

## Aviso

O OpenMagic é um projeto independente, sem qualquer vínculo, endosso ou patrocínio da Apple Inc.
Apple, Magic Keyboard e Magic Mouse são marcas da Apple Inc. Os drivers rodam no kernel do Windows e são
fornecidos no estado em que se encontram, sem garantias; veja a [licença](LICENSE).

## Licença

[MIT](LICENSE) © 2026 Alef Barbeli
