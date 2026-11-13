using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace UvisionOse.Core;

public enum FileKind
{
    CSource,
    CppSource,
    Assembly,
    Object,
    Library,
    Other
}

public sealed class ProjFile
{
    public string Name { get; set; } = "";
    public string Path { get; set; } = "";      // 相对 .uvprojx 目录，形如 ".\Source\main.c"
    public string GroupName { get; set; } = "";
    public int FileType { get; set; }
    public bool Excluded { get; set; }

    public FileKind Kind => KindOf(Path);

    public static FileKind KindOf(string path)
    {
        var ext = System.IO.Path.GetExtension(path).ToLowerInvariant();
        return ext switch
        {
            ".c" => FileKind.CSource,
            ".cpp" or ".cxx" or ".cc" or ".c++" => FileKind.CppSource,
            ".s" or ".asm" => FileKind.Assembly,
            ".o" or ".obj" => FileKind.Object,
            ".lib" or ".a" or ".ar" => FileKind.Library,
            _ => FileKind.Other
        };
    }
}

public sealed class ProjGroup
{
    public string Name { get; set; } = "";
    public bool Excluded { get; set; }
    public List<ProjFile> Files { get; set; } = new();
}

public readonly record struct MemoryRegion(uint Start, uint Size)
{
    public bool IsValid => Size > 0;
}

public sealed class UvTarget
{
    public string Name { get; set; } = "";
    public string ToolsetName { get; set; } = "ARM-ADS";
    public string CompilerVersion { get; set; } = "";
    public bool Ac6 { get; set; }

    public string Device { get; set; } = "";
    public string Vendor { get; set; } = "";
    public string Cpu { get; set; } = "";

    public string OutputDirectory { get; set; } = "";
    public string OutputName { get; set; } = "";
    public string ListingPath { get; set; } = "";
    public bool CreateExecutable { get; set; } = true;
    public bool CreateLib { get; set; }
    public bool CreateHexFile { get; set; }
    public bool BrowseInformation { get; set; } = true;

    public int Optimization { get; set; } = 1;
    public bool C99 { get; set; } = true;
    public bool GnuExtensions { get; set; }
    public bool OneElfSectionPerFunction { get; set; }
    public bool Interwork { get; set; } = true;
    public int WarningLevel { get; set; } = 2;
    public string Defines { get; set; } = "";
    public string Undefines { get; set; } = "";
    public string IncludePaths { get; set; } = "";
    public string MiscControls { get; set; } = "";

    public string AsmDefines { get; set; } = "";
    public string AsmIncludePaths { get; set; } = "";
    public string AsmMisc { get; set; } = "";

    public bool UseMemoryLayout { get; set; } = true;
    public string ScatterFile { get; set; } = "";
    public string LinkerMisc { get; set; } = "";
    public string TextAddressRange { get; set; } = "";
    public string DataAddressRange { get; set; } = "";
    public bool UseMicrolib { get; set; }

    public MemoryRegion Irom1 { get; set; }
    public MemoryRegion Iram1 { get; set; }

    public List<ProjGroup> Groups { get; set; } = new();

    public IEnumerable<ProjFile> AllFiles => Groups.Where(g => !g.Excluded).SelectMany(g => g.Files).Where(f => !f.Excluded);

    public string ArtifactName => string.IsNullOrWhiteSpace(OutputName) ? "Project" : OutputName;

    public string DisplayCpu()
    {
        var start = Cpu.IndexOf("CPUTYPE", StringComparison.OrdinalIgnoreCase);
        if (start < 0)
            return string.IsNullOrEmpty(Device) ? Cpu : Device;
        var open = Cpu.IndexOf('"', start);
        var close = open < 0 ? -1 : Cpu.IndexOf('"', open + 1);
        return open >= 0 && close > open ? Cpu.Substring(open + 1, close - open - 1) : Device;
    }
}

public sealed class UvProject
{
    public string FilePath { get; set; } = "";
    public string SchemaVersion { get; set; } = "";
    public List<UvTarget> Targets { get; set; } = new();
    public int ActiveTarget { get; set; }

    public bool IsValid => Targets.Count > 0;
    public string Directory => string.IsNullOrEmpty(FilePath) ? "" : (System.IO.Path.GetDirectoryName(FilePath) ?? "");
    public string BaseName => string.IsNullOrEmpty(FilePath) ? "" : System.IO.Path.GetFileNameWithoutExtension(FilePath);

    public UvTarget? Target(int index) =>
        index >= 0 && index < Targets.Count ? Targets[index] : null;

    // uvprojx 里的路径一律用反斜杠
    public string ToAbsolute(string raw)
    {
        var p = raw.Replace('\\', '/');
        if (System.IO.Path.IsPathRooted(p))
            return p;
        return System.IO.Path.GetFullPath(System.IO.Path.Combine(Directory, p));
    }

    // 逗号 / 分号 / 空格混写的列表
    public static string[] SplitOptionList(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
            return Array.Empty<string>();
        return raw.Split(new[] { ',', ';', ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
    }
}
