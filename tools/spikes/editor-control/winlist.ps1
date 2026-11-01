# Lists every window (top level and child) owned by a process: used by the editor control spike
# to tell an embedded game window (WS_CHILD of the editor) from a normal top-level window.
param([int]$ProcId)
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WinList {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint f);
    [DllImport("user32.dll")] static extern IntPtr GetDesktopWindow();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    public static List<string> Run(uint pid) {
        var res = new List<string>();
        IntPtr desktop = GetDesktopWindow();
        EnumProc visit = null;
        visit = (h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid) {
                var c = new StringBuilder(128); GetClassName(h, c, 128);
                var t = new StringBuilder(128); GetWindowText(h, t, 128);
                RECT r; GetWindowRect(h, out r);
                int style = GetWindowLong(h, -16);
                IntPtr par = GetParent(h);
                bool top = GetAncestor(h, 1) == desktop;
                res.Add(string.Format("hwnd=0x{0:X} class={1} title='{2}' toplevel={3} ws_child={4} parent=0x{5:X} visible={6} rect={7},{8},{9},{10}",
                    h.ToInt64(), c, t, top, (style & 0x40000000) != 0, par.ToInt64(), IsWindowVisible(h), r.L, r.T, r.R, r.B));
            }
            return true;
        };
        EnumWindows((h, l) => {
            visit(h, l);
            EnumChildWindows(h, visit, IntPtr.Zero);
            return true;
        }, IntPtr.Zero);
        return res;
    }
}
"@
[WinList]::Run([uint32]$ProcId) | ForEach-Object { $_ }
