using System;
using System.Linq;
using System.Threading.Tasks;
using UvisionOse.Core;

namespace UvisionOse;

// 命令行构建：mdk-v5-ose --build <uvprojx> [--toolchain <dir>] [--target N] [--rebuild] [-j N]
// 输出和 uVision 一样的文本，方便进 CI 或者在命令行里快速验证。
internal static class Cli
{
    public static int RunBuild(string[] args)
    {
        var projectPath = ValueOf(args, "--build");
        if (string.IsNullOrEmpty(projectPath))
        {
            Console.Error.WriteLine("用法: uvision5-ose --build <uvprojx> [--toolchain <dir>] [--target N] [--rebuild] [-j N]");
            return 2;
        }

        if (!UvprojReader.Load(projectPath, out var project, out var error))
        {
            Console.Error.WriteLine(error);
            return 2;
        }

        var settings = Settings.Instance;
        var binDir = ValueOf(args, "--toolchain") ?? settings.ToolchainDir;
        if (string.IsNullOrEmpty(binDir))
            binDir = Toolchain.GuessBinDir(settings.ToolchainFamily);

        var toolchain = Toolchain.Detect(binDir, settings.ToolchainFamily);
        if (!toolchain.IsValid)
        {
            Console.Error.WriteLine("没有配置 ARM 编译器（armcc/armclang）。用 --toolchain <bin 目录> 指定。");
            return 3;
        }

        var targetIndex = int.TryParse(ValueOf(args, "--target"), out var t) ? t : 0;
        targetIndex = Math.Clamp(targetIndex, 0, project.Targets.Count - 1);

        var jobs = int.TryParse(ValueOf(args, "-j") ?? ValueOf(args, "--jobs"), out var j) ? j : settings.BuildJobs;

        var engine = new BuildEngine();
        engine.SetToolchain(toolchain);
        engine.SetJobs(jobs);
        engine.SetProject(project, targetIndex);

        engine.Output += Console.WriteLine;
        engine.DiagnosticFound += d => { };   // 已经混在输出里了

        var rebuild = args.Contains("--rebuild");
        var ok = engine.BuildAsync(rebuild).GetAwaiter().GetResult();
        return ok ? 0 : 1;
    }

    private static string? ValueOf(string[] args, string name)
    {
        var i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
    }
}
