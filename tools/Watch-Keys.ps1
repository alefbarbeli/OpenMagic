<#
.SYNOPSIS
  Prints every key event Windows receives, to check the OpenMagic keymaps.

.DESCRIPTION
  Uses a low-level keyboard hook (no admin needed) and shows the virtual key,
  the set-1 scancode (E0-prefixed keys as E0xx) and whether the key went down
  or up. Events are also appended to -LogFile. Press Ctrl+C or wait -Seconds.

.EXAMPLE
  .\Watch-Keys.ps1 -Seconds 60
#>
param(
  [int]$Seconds = 60,
  [string]$LogFile = (Join-Path $PSScriptRoot 'watch-keys.log')
)

Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

public static class KeyWatcher {
  [StructLayout(LayoutKind.Sequential)]
  struct KBDLLHOOKSTRUCT { public uint vkCode, scanCode, flags, time; public IntPtr extra; }
  delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);

  [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr mod, uint thread);
  [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr hook);
  [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
  [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandle(string name);

  static IntPtr hook;
  static HookProc proc = Callback;
  static string log;

  static IntPtr Callback(int code, IntPtr wParam, IntPtr lParam) {
    if (code >= 0) {
      var k = (KBDLLHOOKSTRUCT)Marshal.PtrToStructure(lParam, typeof(KBDLLHOOKSTRUCT));
      bool up = (k.flags & 0x80) != 0, extended = (k.flags & 0x01) != 0, injected = (k.flags & 0x10) != 0;
      string scan = extended ? string.Format("E0{0:X2}", k.scanCode) : string.Format("{0:X2}", k.scanCode);
      string line = string.Format("{0:HH:mm:ss.fff}  {1,-4}  {2,-22} VK 0x{3:X2}  scan {4}{5}",
        DateTime.Now, up ? "up" : "down", (Keys)k.vkCode, k.vkCode, scan, injected ? "  (injected)" : "");
      Console.WriteLine(line);
      File.AppendAllText(log, line + Environment.NewLine);
    }
    return CallNextHookEx(hook, code, wParam, lParam);
  }

  public static void Run(int seconds, string logPath) {
    log = logPath;
    hook = SetWindowsHookEx(13, proc, GetModuleHandle(null), 0);   // WH_KEYBOARD_LL
    var timer = new Timer { Interval = seconds * 1000 };
    timer.Tick += (s, e) => Application.ExitThread();
    timer.Start();
    Application.Run();
    UnhookWindowsHookEx(hook);
  }
}
'@

"Watching keys for $Seconds s (log: $LogFile). Press the keys you want to check."
[KeyWatcher]::Run($Seconds, $LogFile)
'Done.'
