using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace UvisionOse.Core;

// 手工配置的 ARM 编译器。目录形如：
//   C:/Keil_v5/ARM/ARMCC/bin      (AC5)
//   C:/Keil_v5/ARM/ARMCLANG/bin   (AC6)
public sealed class Toolchain
{
    public string BinDir { get; set; } = "";
    public string Family { get; set; } = "";   // "AC5" / "AC6"
    public string Cc { get; set; } = "";       // armcc / armclang
    public string As { get; set; } = "";       // armasm
    public string Ld { get; set; } = "";       // armlink
    public string Ar { get; set; } = "";       // armar
    public string From { get; set; } = "";     // fromelf

    public bool IsValid => Cc.Length > 0 && Ld.Length > 0;

    public string DisplayName =>
        BinDir.Length == 0 ? "(未配置编译器)" : $"ARM Compiler {Family}";

    public IEnumerable<string> MissingTools()
    {
        if (Cc.Length == 0) yield return "armcc/armclang";
        if (As.Length == 0) yield return "armasm";
        if (Ld.Length == 0) yield return "armlink";
        if (From.Length == 0) yield return "fromelf";
    }

    private static string ExeSuffix => OperatingSystem.IsWindows() ? ".exe" : "";

    private static string FindTool(string dir, string baseName)
    {
        if (string.IsNullOrEmpty(dir))
            return "";
        foreach (var name in new[] { baseName + ExeSuffix, baseName })
        {
            var candidate = Path.Combine(dir, name);
            if (File.Exists(candidate))
                return candidate;
        }
        return "";
    }

    public static Toolchain Detect(string binDir, string preferFamily = "")
    {
        var tc = new Toolchain { BinDir = string.IsNullOrEmpty(binDir) ? "" : Path.GetFullPath(binDir) };
        if (tc.BinDir.Length == 0)
            return tc;

        var armcc = FindTool(binDir, "armcc");
        var armclang = FindTool(binDir, "armclang");
        var wantAc6 = string.Equals(preferFamily, "AC6", StringComparison.OrdinalIgnoreCase);

        if (armcc.Length > 0 && (!wantAc6 || armclang.Length == 0))
        {
            tc.Family = "AC5";
            tc.Cc = armcc;
        }
        else if (armclang.Length > 0)
        {
            tc.Family = "AC6";
            tc.Cc = armclang;
        }
        else if (armcc.Length > 0)
        {
            tc.Family = "AC5";
            tc.Cc = armcc;
        }
        else
        {
            return tc;
        }

        tc.As = FindTool(binDir, "armasm");
        tc.Ld = FindTool(binDir, "armlink");
        tc.Ar = FindTool(binDir, "armar");
        tc.From = FindTool(binDir, "fromelf");
        return tc;
    }

    // 猜一个 Keil 根目录（只做提示，不保证存在）。
    // 不读注册表：net10.0 这个 TFM 下 Microsoft.Win32.Registry 不在框架里，
    // 为这点小功能加一个包不划算，常见安装路径够用了。
    public static string GuessKeilRoot()
    {
        if (!OperatingSystem.IsWindows())
            return "";

        var candidates = new List<string>();
        var drives = new[] { "C:", "D:", "E:" };
        foreach (var d in drives)
        {
            candidates.Add(d + "\\Keil_v5");
            candidates.Add(d + "\\Keil");
        }

        foreach (var root in candidates)
        {
            if (File.Exists(Path.Combine(root, "UV4", "UV4.exe")))
                return root;
        }
        return "";
    }

    public static string GuessBinDir(string family)
    {
        var root = GuessKeilRoot();
        if (root.Length == 0)
            return "";
        var sub = string.Equals(family, "AC6", StringComparison.OrdinalIgnoreCase)
            ? Path.Combine("ARM", "ARMCLANG", "bin")
            : Path.Combine("ARM", "ARMCC", "bin");
        var dir = Path.Combine(root, sub);
        return Directory.Exists(dir) ? dir : "";
    }

    // 跑 cc --vsn / --version 取版本号文本（"5.06 update 6 (build 750)" 这种）
    public string QueryVersion()
    {
        if (Cc.Length == 0)
            return "";

        try
        {
            var psi = new ProcessStartInfo(Cc)
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            psi.ArgumentList.Add(Family == "AC6" ? "--version" : "--vsn");

            using var p = Process.Start(psi);
            if (p is null)
                return "";

            var stdout = p.StandardOutput.ReadToEnd();
            var stderr = p.StandardError.ReadToEnd();
            if (!p.WaitForExit(5000))
            {
                try { p.Kill(true); } catch { }
                return "";
            }

            var all = stdout + "\n" + stderr;

            // AC5:  Product: ARM Compiler 5.06 update 6 (build 750)
            var m = Regex.Match(all, @"Product:\s*(.+)", RegexOptions.IgnoreCase);
            if (m.Success)
            {
                var s = m.Groups[1].Value.Trim();
                s = s.Replace("ARM Compiler ", "");
                return s.Trim();
            }

            // AC6: armclang --version 里找 "Arm Compiler for Embedded 6.x"
            m = Regex.Match(all, @"Compiler for Embedded\s+([\d.]+)");
            if (m.Success)
                return m.Groups[1].Value;

            m = Regex.Match(all, @"clang version\s+([\d.]+)");
            if (m.Success)
                return m.Groups[1].Value;

            return "";
        }
        catch
        {
            return "";
        }
    }
}
