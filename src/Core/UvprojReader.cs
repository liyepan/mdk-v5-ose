using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Xml.Linq;

namespace UvisionOse.Core;

// .uvprojx 是一个巨大的 XML，这里只取我们真正要用的部分。
// 和 Qt 版一一对应：Target 页、C/C++ 页、Asm 页、Linker 页、片上存储、Groups/Files。
public static class UvprojReader
{
    public static bool Load(string path, out UvProject project, out string error)
    {
        project = new UvProject();
        error = "";

        XDocument doc;
        try
        {
            doc = XDocument.Load(path, LoadOptions.None);
        }
        catch (Exception ex)
        {
            error = $"无法打开工程文件: {ex.Message}";
            return false;
        }

        var root = doc.Root;
        if (root is null || root.Name.LocalName != "Project")
        {
            error = "不是有效的 uVision 工程文件（根节点不是 Project）";
            return false;
        }

        project.FilePath = Path.GetFullPath(path);
        project.SchemaVersion = Text(root.Element("SchemaVersion"));

        var targets = root.Element("Targets");
        if (targets is not null)
        {
            foreach (var tEl in targets.Elements("Target"))
                project.Targets.Add(ReadTarget(tEl));
        }

        if (project.Targets.Count == 0)
        {
            error = "工程文件里没有任何 Target";
            return false;
        }

        return true;
    }

    private static UvTarget ReadTarget(XElement tEl)
    {
        var t = new UvTarget { Name = Text(tEl.Element("TargetName")) };
        if (string.IsNullOrEmpty(t.Name))
            t.Name = "Target";

        t.ToolsetName = Text(tEl.Element("ToolsetName"));

        var option = tEl.Element("TargetOption");
        var common = option?.Element("TargetCommonOption");
        var armAds = option?.Element("TargetArmAds");

        if (common is not null)
        {
            t.Device = Text(common.Element("Device"));
            t.Vendor = Text(common.Element("Vendor"));
            t.Cpu = Text(common.Element("Cpu"));
            t.OutputDirectory = Text(common.Element("OutputDirectory"));
            t.OutputName = Text(common.Element("OutputName"));
            t.ListingPath = Text(common.Element("ListingPath"));
            t.CreateExecutable = Flag(common.Element("CreateExecutable"), true);
            t.CreateLib = Flag(common.Element("CreateLib"), false);
            t.CreateHexFile = Flag(common.Element("CreateHexFile"), false);
            t.BrowseInformation = Flag(common.Element("BrowseInformation"), true);
            t.CompilerVersion = Text(common.Element("pCCUsed"));
            t.Ac6 = Flag(common.Element("uAC6"), false);
        }

        if (armAds is not null)
        {
            var cads = armAds.Element("Cads");
            if (cads is not null)
            {
                // uVision 的 Optim 是 1..4 -> O0..O3
                var optim = Int(cads.Element("Optim"), 1);
                t.Optimization = Math.Clamp(optim - 1, 0, 3);
                t.C99 = Flag(cads.Element("uC99"), true);
                t.GnuExtensions = Flag(cads.Element("uGnu"), false);
                t.OneElfSectionPerFunction = Flag(cads.Element("OneElfS"), false);
                t.Interwork = Flag(cads.Element("interw"), true);
                t.WarningLevel = Int(cads.Element("wLevel"), 2);

                var various = cads.Element("VariousControls");
                t.Defines = Text(various?.Element("Define"));
                t.Undefines = Text(various?.Element("Undefine"));
                t.IncludePaths = Text(various?.Element("IncludePath"));
                t.MiscControls = Text(various?.Element("MiscControls"));
            }

            var aads = armAds.Element("Aads");
            if (aads is not null)
            {
                var various = aads.Element("VariousControls");
                t.AsmDefines = Text(various?.Element("Define"));
                t.AsmIncludePaths = Text(various?.Element("IncludePath"));
                t.AsmMisc = Text(various?.Element("MiscControls"));
            }

            var ldads = armAds.Element("LDads");
            if (ldads is not null)
            {
                t.UseMemoryLayout = Flag(ldads.Element("umfTarg"), true);
                t.ScatterFile = Text(ldads.Element("ScatterFile"));
                t.LinkerMisc = Text(ldads.Element("Misc"));
                t.TextAddressRange = Text(ldads.Element("TextAddressRange"));
                t.DataAddressRange = Text(ldads.Element("DataAddressRange"));
                t.NoStartLib = Flag(ldads.Element("noStLib"), false);
            }

            var misc = armAds.Element("ArmAdsMisc");
            if (misc is not null)
            {
                t.UseMicrolib = Flag(misc.Element("useUlib"), false);

                var ocm = misc.Element("OnChipMemories");
                t.Irom1 = ReadRegion(ocm?.Element("IROM"));
                t.Iram1 = ReadRegion(ocm?.Element("IRAM"));
            }

            if (!t.Irom1.IsValid)
                t.Irom1 = RegionFromCpu(t.Cpu, "IROM");
            if (!t.Iram1.IsValid)
                t.Iram1 = RegionFromCpu(t.Cpu, "IRAM");
        }

        // <BeforeCompile><RunUserProg1>1</RunUserProg1><UserProg1Name>...</UserProg1Name>
        //                 <nStopU1X>0</nStopU1X> ...
        t.BeforeCompile = ReadUserPrograms(common?.Element("BeforeCompile"), "nStopU");
        t.BeforeMake = ReadUserPrograms(common?.Element("BeforeMake"), "nStopB");
        t.AfterMake = ReadUserPrograms(common?.Element("AfterMake"), "nStopA");

        var groups = tEl.Element("Groups");
        if (groups is not null)
        {
            foreach (var gEl in groups.Elements("Group"))
            {
                var g = new ProjGroup { Name = Text(gEl.Element("GroupName")) };
                if (string.IsNullOrEmpty(g.Name))
                    g.Name = "Group";

                var gOption = gEl.Element("GroupOption");
                g.Excluded = !Flag(gOption?.Element("CommonProperty")?.Element("IncludeInBuild"), true);

                var gVarious = gOption?.Element("GroupArmAds")?.Element("Cads")?.Element("VariousControls");
                if (gVarious is not null)
                {
                    g.HasOwnOptions = true;
                    g.Defines = Text(gVarious.Element("Define"));
                    g.IncludePaths = Text(gVarious.Element("IncludePath"));
                    g.Misc = Text(gVarious.Element("MiscControls"));
                }

                var files = gEl.Element("Files");
                if (files is not null)
                {
                    foreach (var fEl in files.Elements("File"))
                    {
                        var f = new ProjFile
                        {
                            Name = Text(fEl.Element("FileName")),
                            Path = Text(fEl.Element("FilePath")),
                            FileType = Int(fEl.Element("FileType"), 0),
                            GroupName = g.Name
                        };
                        var fOption = fEl.Element("FileOption");
                        f.Excluded = !Flag(fOption?.Element("CommonProperty")?.Element("IncludeInBuild"), true);

                        var fVarious = fOption?.Element("FileArmAds")?.Element("Cads")?.Element("VariousControls");
                        if (fVarious is not null)
                        {
                            f.HasOwnOptions = true;
                            f.Defines = Text(fVarious.Element("Define"));
                            f.IncludePaths = Text(fVarious.Element("IncludePath"));
                            f.Misc = Text(fVarious.Element("MiscControls"));
                        }
                        g.Files.Add(f);
                    }
                }

                t.Groups.Add(g);
            }
        }

        return t;
    }

    private static List<UserProgram> ReadUserPrograms(XElement? el, string stopTagPrefix)
    {
        var list = new List<UserProgram>();
        if (el is null)
            return list;

        for (var i = 1; i <= 2; i++)
        {
            var cmd = Text(el.Element($"UserProg{i}Name"));
            if (cmd.Length == 0)
                continue;

            list.Add(new UserProgram
            {
                Command = cmd,
                Enabled = Flag(el.Element($"RunUserProg{i}"), false),
                StopOnExitCode = Text(el.Element($"{stopTagPrefix}{i}X")) != "0"
            });
        }
        return list;
    }

    private static MemoryRegion ReadRegion(XElement? el)
    {
        if (el is null)
            return default;
        return new MemoryRegion(ParseUInt(Text(el.Element("StartAddress"))), ParseUInt(Text(el.Element("Size"))));
    }

    // <Cpu>IRAM(0x20000000,0x00010000) IROM(0x08000000,0x00080000) ...</Cpu>
    private static MemoryRegion RegionFromCpu(string cpu, string tag)
    {
        var at = cpu.IndexOf(tag + "(", StringComparison.OrdinalIgnoreCase);
        if (at < 0)
            return default;

        var open = cpu.IndexOf('(', at);
        var close = cpu.IndexOf(')', open + 1);
        if (open < 0 || close < 0)
            return default;

        var parts = cpu.Substring(open + 1, close - open - 1).Split(',');
        if (parts.Length < 2)
            return default;

        return new MemoryRegion(ParseUInt(parts[0]), ParseUInt(parts[1]));
    }

    private static uint ParseUInt(string s)
    {
        s = s.Trim();
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            return uint.TryParse(s[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var hex) ? hex : 0;
        return uint.TryParse(s, out var v) ? v : 0;
    }

    private static string Text(XElement? el) => el?.Value.Trim() ?? "";

    private static bool Flag(XElement? el, bool fallback)
    {
        if (el is null)
            return fallback;
        var v = el.Value.Trim();
        return v == "1" || v.Equals("true", StringComparison.OrdinalIgnoreCase);
    }

    private static int Int(XElement? el, int fallback)
    {
        if (el is null)
            return fallback;
        return int.TryParse(el.Value.Trim(), out var v) ? v : fallback;
    }
}
