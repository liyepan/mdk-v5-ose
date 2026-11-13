using System;
using System.IO;
using System.Text.RegularExpressions;

namespace UvisionOse.Core;

public enum Severity
{
    Error,
    Warning,
    Info
}

public sealed class Diagnostic
{
    public Severity Severity { get; set; } = Severity.Error;
    public string File { get; set; } = "";     // 绝对路径（解析成功后）
    public int Line { get; set; }
    public int Column { get; set; }
    public string Code { get; set; } = "";     // 例如 #20 / L6218E
    public string Message { get; set; } = "";
    public string Raw { get; set; } = "";

    public bool HasLocation => File.Length > 0 && Line > 0;

    public string SeverityText => Severity switch
    {
        Severity.Error => "Error",
        Severity.Warning => "Warning",
        _ => "Info"
    };

    // 列表里显示的一行
    public string Display => HasLocation
        ? $"{System.IO.Path.GetFileName(File)}({Line}): {SeverityText} {Code} {Message}".Replace("  ", " ").Trim()
        : $"{SeverityText} {Code} {Message}".Replace("  ", " ").Trim();
}

public static class Diagnostics
{
    // armcc / armasm:  ".\main.c", line 45: Error:  #20: identifier "x" is undefined
    private static readonly Regex ArmccRe = new(
        @"^""?(?<file>[^""]+?)""?,\s*line\s+(?<line>\d+)\s*:\s*" +
        @"(?<sev>Fatal error|Error|Warning|Note)\s*:\s*" +
        @"(?:#\s*(?<code>\d+(?:-[A-Za-z]+)?)\s*:\s*)?(?<msg>.*)$",
        RegexOptions.IgnoreCase | RegexOptions.Compiled);

    // gcc: /src/main.c:45:12: error: 'x' undeclared   (also tolerates Windows drive colon)
    private static readonly Regex GccRe = new(
        @"^(?<file>.+?):(?<line>\d+):(?:(?<col>\d+):)?\s*" +
        @"(?<sev>fatal error|error|warning|note|remark)\s*:\s*(?<msg>.*)$",
        RegexOptions.IgnoreCase | RegexOptions.Compiled);

    // armlink: Error: L6218E: Undefined symbol xxx (referred from main.o).
    private static readonly Regex ArmlinkRe = new(
        @"^(?<sev>Error|Warning|Fatal error)\s*:\s*(?<code>L\d+[A-Z]?)\s*:\s*(?<msg>.*)$",
        RegexOptions.IgnoreCase | RegexOptions.Compiled);

    private static Severity ToSeverity(string text)
    {
        if (text.Equals("Warning", StringComparison.OrdinalIgnoreCase))
            return Severity.Warning;
        if (text.Equals("Note", StringComparison.OrdinalIgnoreCase)
            || text.Equals("remark", StringComparison.OrdinalIgnoreCase))
            return Severity.Info;
        return Severity.Error;
    }

    private static string AbsoluteFile(string raw, string projectDir)
    {
        var p = raw.Trim().Replace('\\', '/');
        if (p.Length == 0)
            return "";

        if (Path.IsPathRooted(p))
            return Path.GetFullPath(p);

        if (p.StartsWith("./") || p.StartsWith("../"))
            return Path.GetFullPath(Path.Combine(projectDir, p));

        // armcc 有时给出相对于当前目录的裸文件名
        var inProject = Path.GetFullPath(Path.Combine(projectDir, p));
        if (File.Exists(inProject))
            return inProject;

        return Path.GetFullPath(p);
    }

    public static bool TryParse(string line, string projectDir, out Diagnostic diagnostic)
    {
        diagnostic = new Diagnostic();
        if (string.IsNullOrWhiteSpace(line))
            return false;

        var d = new Diagnostic { Raw = line };

        var m = ArmccRe.Match(line);
        if (m.Success)
        {
            d.Severity = ToSeverity(m.Groups["sev"].Value);
            d.File = AbsoluteFile(m.Groups["file"].Value, projectDir);
            d.Line = m.Groups["line"].Value.Length > 0 ? int.Parse(m.Groups["line"].Value) : 0;
            d.Code = m.Groups["code"].Value;
            d.Message = m.Groups["msg"].Value.Trim();
            diagnostic = d;
            return true;
        }

        m = GccRe.Match(line);
        if (m.Success)
        {
            d.Severity = ToSeverity(m.Groups["sev"].Value);
            d.File = AbsoluteFile(m.Groups["file"].Value, projectDir);
            d.Line = m.Groups["line"].Value.Length > 0 ? int.Parse(m.Groups["line"].Value) : 0;
            d.Column = m.Groups["col"].Value.Length > 0 ? int.Parse(m.Groups["col"].Value) : 0;
            d.Message = m.Groups["msg"].Value.Trim();
            diagnostic = d;
            return true;
        }

        m = ArmlinkRe.Match(line);
        if (m.Success)
        {
            d.Severity = ToSeverity(m.Groups["sev"].Value);
            d.Code = m.Groups["code"].Value;
            d.Message = m.Groups["msg"].Value.Trim();
            diagnostic = d;
            return true;
        }

        return false;
    }
}
