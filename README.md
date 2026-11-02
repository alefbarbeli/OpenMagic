# OpenMagic

Drivers abertos para usar o Apple Magic Keyboard e o Magic Mouse 2 no Windows,
sem o Magic Utilities.

| Dispositivo | Recursos | Status |
| --- | --- | --- |
| Magic Keyboard with Numeric Keypad (`05AC:026C`), USB e Bluetooth | Fn + teclas de mídia, atalhos com Fn, F13–F19 configuráveis | em desenvolvimento |
| Magic Mouse 2 (`004C:0269`), Bluetooth | rolagem vertical e horizontal, suave, configurável | em desenvolvimento |

É uma implementação independente: não usa código do Magic Utilities. O
comportamento dos dispositivos vem dos próprios descritores HID e da
documentação pública do protocolo (drivers `hid-apple` e `hid-magicmouse` do
kernel Linux).

## Como funciona (teclado)

O Fn do Magic Keyboard não é uma tecla comum: ele chega como um bit de uso do
fabricante (páginas `0xFF00`/`0xFF01`, uso `0x03`) dentro do relatório do
teclado. O driver de teclado do Windows (`kbdhid`) descarta esse bit, e nenhum
programa consegue ler a coleção do teclado, então é preciso um filtro de kernel.

`OpenMagicKbd.sys` é instalado duas vezes na pilha do teclado:

```
kbdclass
OpenMagicKbd  (upper)  troca scancodes conforme os mapas
kbdhid
OpenMagicKbd  (lower)  lê os relatórios HID e acompanha o estado do Fn
HidBth / HidUsb
```

## Mapas de teclas

Com Fn:

| Tecla | Vira |
| --- | --- |
| F7 / F8 / F9 | faixa anterior / play-pause / próxima |
| F10 / F11 / F12 | mudo / volume - / volume + |
| F3 | Win+Tab (Visão de Tarefas) |
| F4 | Win+S (Pesquisar) |
| Backspace | Delete |
| ↑ ↓ ← → | Page Up, Page Down, Home, End |

Sem Fn: F13 = Print Screen, F14 = Scroll Lock, F15 = Pause. F16–F19 ficam livres.

Para mudar (PowerShell como administrador):

```powershell
.\tools\OpenMagic-Keymap.ps1                        # mostra os mapas
.\tools\OpenMagic-Keymap.ps1 set F16 MediaStop      # F16 -> Stop
.\tools\OpenMagic-Keymap.ps1 set F1 F21 -Fn         # Fn+F1 -> F21
.\tools\OpenMagic-Keymap.ps1 set F17 None           # desativa F17
.\tools\OpenMagic-Keymap.ps1 remove F16             # volta ao normal
.\tools\OpenMagic-Keymap.ps1 keys                   # nomes de teclas
```

## Como funciona (mouse)

O Magic Mouse 2 só manda as posições dos dedos depois de receber o relatório de
configuração `0xF1 = 02 01`, que não está no seu descritor HID, e aí acrescenta
os dedos ao relatório `0x12`, que o descritor declara com 8 bytes. Por isso
`OpenMagicMouse.sys` fica em dois lugares:

```
mouclass
OpenMagicMouse  (mouse)      injeta eventos de rolagem
mouhid
HidBth
OpenMagicMouse  (bluetooth)  liga o multitoque, lê os dedos, corta o relatório para 8 bytes
BthEnum
```

A rolagem usa um dedo, com zona morta, trava de eixo e ganho que cresce com a
velocidade. Cliques ou movimento do mouse bloqueiam a rolagem até o dedo
levantar. Ajustes (PowerShell como administrador):

```powershell
.\tools\OpenMagic-Mouse.ps1                                   # mostra
.\tools\OpenMagic-Mouse.ps1 -ScrollSpeed 150                  # 10 a 400 %
.\tools\OpenMagic-Mouse.ps1 -NaturalScrolling 0               # direção tradicional
.\tools\OpenMagic-Mouse.ps1 -SmoothScrolling 0                # passos de 120
```

## Compilar e instalar

Os drivers não são assinados pela Microsoft, então o Windows precisa estar em
modo de teste (`bcdedit /set testsigning on` e reiniciar; exige Secure Boot
desligado).

1. `scripts\setup-dev.ps1` (como administrador): ponto de restauração, modo de
   teste, Visual Studio 2026 + Windows SDK/WDK 10.0.28000.
2. `scripts\build.ps1`: compila e gera os pacotes assinados com certificado de teste.
3. `scripts\install-drivers.ps1` (como administrador): confia no certificado de
   teste e instala os pacotes. `-KeyboardOnly` / `-MouseOnly` instalam só um.

Para remover: `scripts\uninstall-drivers.ps1` (como administrador). Os
dispositivos voltam para os drivers do Windows.
