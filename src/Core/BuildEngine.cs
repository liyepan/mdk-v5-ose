using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace UvisionOse.Core;

// 把 uVision 工程变成一串 armcc / armasm / armlink / armar / fromelf 调用，
// 按 uVision 的格式回放输出。
//
// 执行顺序：
//   [BeforeCompile 用户命令]（串行）
//   [编译 / 汇编]（并行，-j N）
//   [BeforeMake 用户命令]（串行）
//   [链接 或 armar]（串行）
//   [fromelf 生成 hex]（串行）
//   [AfterMake 用户命令]（串行）
// 串行任务要等并行批次全部结束才开始，所以链接永远拿到完整的 .o 集合。
//
// 增量判断三部分：
//   1. 目标文件比源文件新
//   2. 目标文件比所有被包含的本地头文件新（自己扫 #include）
//   3. 编译参数指纹没变（存在 <Objects>/.mdk-ose/<target>.sig）
public sealed class BuildEngine
{
    public enum Mode { Build, Translate, Clean }

    public sealed record Job(
        string Program,
        IReadOnlyList<string> Args,
        string Title,
        bool Parallel = false,
        bool IsLink = false,
        bool IsHook = false,
        bool HookStopsOnError = false,
        bool SkipIfErrors = false,
        string HookCommand = "");

    public event Action<string>? Output;
    public event Action<Diagnostic>? DiagnosticFound;
    public event Action<string>? Status;
    public event Action<bool, int, int>? Finished;   // success, errors, warnings
    public event Action<string>? MapFileReady;
    public event Action<string>? ProgramSize;        // "Code=... RO-data=..."

    private Toolchain _toolchain = new();
    private UvProject _project = new();
    private int _targetIndex;
    private Mode _mode = Mode.Build;

    private int _jobLimit = Math.Max(1, Environment.ProcessorCount);
    private int _errors;
    private int _warnings;
    private bool _running;
    private volatile bool _cancelled;

    private readonly Dictionary<string, string> _signatures = new(StringComparer.OrdinalIgnoreCase);
    private string _signaturePath = "";
    private bool _signaturesDirty;
    private string _generatedScatter = "";
    private string _axfForSize = "";
    private DateTime _startedAt;

    public void SetToolchain(Toolchain tc) => _toolchain = tc;
    public Toolchain Toolchain => _toolchain;

    public void SetProject(UvProject project, int targetIndex)
    {
        _project = project;
        _targetIndex = targetIndex;
    }

    public void SetJobs(int jobs) => _jobLimit = Math.Max(1, jobs);
    public int Jobs => _jobLimit;

    public bool IsRunning => _running;
    public bool HasProject => _project.IsValid;

    // ---------------------------------------------------------------- 路径

    private UvTarget? Target => _project.Target(_targetIndex);
    private string ProjectDir => _project.Directory;

    public string OutputDir
    {
        get
        {
            var raw = string.IsNullOrWhiteSpace(Target?.OutputDirectory)
                ? ".\\Objects\\"
                : Target!.OutputDirectory;
            return _project.ToAbsolute(raw);
        }
    }

    public string AxfPath => Path.Combine(OutputDir, Target?.ArtifactName ?? "Project") + ".axf";
    public string MapPath => Path.Combine(OutputDir, Target?.ArtifactName ?? "Project") + ".map";
    public string LibPath => Path.Combine(OutputDir, Target?.ArtifactName ?? "Project") + ".lib";
    private string CpuName => Target?.DisplayCpu() is { Length: > 0 } cpu ? cpu : "Cortex-M3";

    // ---------------------------------------------------------------- 选项合并

    private static string[] SplitList(string raw) => UvProject.SplitOptionList(raw);

    private List<string> TargetIncludeDirs()
    {
        var list = new List<string>();
        foreach (var inc in SplitList(Target?.IncludePaths ?? ""))
            list.Add(_project.ToAbsolute(inc));
        return list;
    }

    private List<string> TargetAsmIncludeDirs()
    {
        var list = new List<string>();
        foreach (var inc in SplitList(Target?.AsmIncludePaths ?? ""))
            list.Add(_project.ToAbsolute(inc));
        return list;
    }

    private List<string> MergeIncludeDirs(List<string> baseDirs, ProjGroup? group, ProjFile? file)
    {
        var list = new List<string>(baseDirs);
        if (group is { HasOwnOptions: true })
            foreach (var inc in SplitList(group.IncludePaths))
                list.Add(_project.ToAbsolute(inc));
        if (file is { HasOwnOptions: true })
            foreach (var inc in SplitList(file.IncludePaths))
                list.Add(_project.ToAbsolute(inc));
        return list.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
    }

    private List<string> MergeDefines(ProjGroup? group, ProjFile? file)
    {
        var list = new List<string>(SplitList(Target?.Defines ?? ""));
        if (group is { HasOwnOptions: true })
            list.AddRange(SplitList(group.Defines));
        if (file is { HasOwnOptions: true })
            list.AddRange(SplitList(file.Defines));
        return list.Distinct().ToList();
    }

    // uVision 的 MiscControls 允许写 --xxx -Dyyy 这种，按空白切但要照顾引号
    private static IEnumerable<string> SplitArgsLoose(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
            yield break;

        var current = new StringBuilder();
        var quote = '\0';
        foreach (var ch in raw)
        {
            if (quote != 0)
            {
                if (ch == quote) quote = '\0';
                else current.Append(ch);
            }
            else if (ch == '"' || ch == '\'')
            {
                quote = ch;
            }
            else if (char.IsWhiteSpace(ch))
            {
                if (current.Length > 0)
                {
                    yield return current.ToString();
                    current.Clear();
                }
            }
            else
            {
                current.Append(ch);
            }
        }
        if (current.Length > 0)
            yield return current.ToString();
    }

    // ---------------------------------------------------------------- 参数构造

    private List<string> CompileArgs(ProjGroup? group, ProjFile file, string sourcePath, string objectPath)
    {
        var t = Target;
        var a = new List<string>();
        var isC = file.Kind == FileKind.CSource;

        if (_toolchain.Family == "AC6")
        {
            a.Add("--target=arm-arm-none-eabi");
            var cpu = CpuName.ToLowerInvariant().Replace("+", "plus");
            a.Add("-mcpu=" + cpu);
            a.Add("-mthumb");
            a.Add("-g");
            a.Add("-O" + (t?.Optimization ?? 1));
            a.Add("-c");
        }
        else
        {
            a.Add("--cpu");
            a.Add(CpuName);
            if (isC)
                a.Add(t is { C99: true } ? "--c99" : "--c90");
            if (t is { GnuExtensions: true })
                a.Add("--gnu");
            a.Add("-g");
            a.Add("-O" + (t?.Optimization ?? 1));
            if (t is { Interwork: true })
                a.Add("--apcs=interwork");
            if (t is { OneElfSectionPerFunction: true })
                a.Add("--split_sections");
            a.Add("-c");
        }

        // Use MicroLIB 时 Keil 会同时定义 __MICROLIB：
        // 启动文件里 __initial_sp / __heap_base / __heap_limit 的 EXPORT
        // 就在 IF :DEF:__MICROLIB 里面，不定义它链接时就会报 L6218E。
        var defines = MergeDefines(group, file);
        if (t is { UseMicrolib: true } && !defines.Contains("__MICROLIB"))
            defines.Insert(0, "__MICROLIB");

        foreach (var d in defines)
            a.Add("-D" + d);

        // C/C++ 用 Target 的 C/C++ 页 include，汇编用 Asm 页的
        foreach (var inc in MergeIncludeDirs(TargetIncludeDirs(), group, file))
            a.Add("-I" + inc);

        if (t is not null) a.AddRange(SplitArgsLoose(t.MiscControls));
        if (group is { HasOwnOptions: true }) a.AddRange(SplitArgsLoose(group.Misc));
        if (file.HasOwnOptions) a.AddRange(SplitArgsLoose(file.Misc));

        a.Add("-o");
        a.Add(objectPath);
        a.Add(sourcePath);
        return a;
    }

    private List<string> AssembleArgs(ProjGroup? group, ProjFile file, string sourcePath, string objectPath)
    {
        var t = Target;
        var a = new List<string> { "--cpu", CpuName, "-g" };

        var defines = new List<string>(SplitList(t?.AsmDefines ?? ""));
        if (group is { HasOwnOptions: true })
            defines.AddRange(SplitList(group.Defines));
        if (file.HasOwnOptions)
            defines.AddRange(SplitList(file.Defines));

        // 启动文件靠这个开关决定导出哪一组堆栈符号，漏了就报 L6218E
        if (t is { UseMicrolib: true } && !defines.Contains("__MICROLIB"))
            defines.Insert(0, "__MICROLIB");

        foreach (var d in defines)
        {
            var eq = d.IndexOf('=');
            a.Add("--pd");
            a.Add(eq > 0 ? d[..eq] + " SETA " + d[(eq + 1)..] : d + " SETA 1");
        }

        foreach (var inc in MergeIncludeDirs(TargetAsmIncludeDirs(), group, file))
            a.Add("-I" + inc);

        if (t is not null) a.AddRange(SplitArgsLoose(t.AsmMisc));
        if (group is { HasOwnOptions: true }) a.AddRange(SplitArgsLoose(group.Misc));

        a.Add("-o");
        a.Add(objectPath);
        a.Add(sourcePath);
        return a;
    }

    private List<string> LinkArgs(List<string> objects)
    {
        var t = Target;
        var a = new List<string>();
        if (t is null)
            return a;

        a.Add("--cpu");
        a.Add(CpuName);

        if (t.UseMemoryLayout || t.ScatterFile.Length == 0)
        {
            if (_generatedScatter.Length > 0)
            {
                // 有 IROM/IRAM 信息就生成 scatter：这样 Image$$ER_IROM1$$xxx
                // 这些链接器符号才存在（--ro-base 不会产生命名执行域）
                a.Add("--scatter=" + _generatedScatter);
            }
            else
            {
                if (t.TextAddressRange.Trim().Length > 0)
                {
                    a.Add("--ro-base");
                    a.Add(t.TextAddressRange.Trim());
                }
                if (t.DataAddressRange.Trim().Length > 0)
                {
                    a.Add("--rw-base");
                    a.Add(t.DataAddressRange.Trim());
                }
            }
        }
        else
        {
            a.Add("--scatter=" + _project.ToAbsolute(t.ScatterFile));
        }

        if (t.NoStartLib)
            a.Add("--no_stdlib");
        if (t.UseMicrolib)
            a.Add("--library_type=microlib");

        // 关键：--map 单独用会把整张内存映射打到 stdout（刷屏），且不产生 .map 文件。
        // 必须配 --list=<文件> 才能重定向到文件里。
        a.Add("--map");
        a.Add("--list=" + MapPath);
        a.Add("-o");
        a.Add(AxfPath);

        a.AddRange(SplitArgsLoose(t.LinkerMisc));
        a.AddRange(objects);
        return a;
    }

    // 按 Target 对话框里的 IROM1/IRAM1 生成 scatter 文件，
    // 让 Image$$ER_IROM1$$Limit 这类符号存在（Keil 也是这么干的）
    private string WriteScatterFile()
    {
        var t = Target;
        if (t is null || !t.Irom1.IsValid || !t.Iram1.IsValid)
            return "";

        var dir = Path.Combine(OutputDir, ".mdk-ose");
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, SafeFileName(t.Name) + ".sct");

        static string Hex(uint v) => "0x" + v.ToString("X8", CultureInfo.InvariantCulture);

        var lines = new[]
        {
            "; *************************************************************",
            "; *** Scatter file generated by mdk-v5-ose (do not edit)   ***",
            "; *** 执行域必须叫 ER_IROM1, cm_backtrace 之类靠它取代码段范围 ***",
            "; *************************************************************",
            "",
            "LR_IROM1 " + Hex(t.Irom1.Start) + " " + Hex(t.Irom1.Size) + "  {    ; load region size_region",
            "  ER_IROM1 " + Hex(t.Irom1.Start) + " " + Hex(t.Irom1.Size) + "  {  ; load address = execution address",
            "   *.o (RESET, +First)",
            "   *(InRoot$$Sections)",
            "   .ANY (+RO)",
            "  }",
            "  RW_IRAM1 " + Hex(t.Iram1.Start) + " " + Hex(t.Iram1.Size) + "  {  ; RW data",
            "   .ANY (+RW +ZI)",
            "  }",
            "}"
        };

        File.WriteAllText(path, string.Join(Environment.NewLine, lines) + Environment.NewLine);
        return path;
    }

    private List<string> LibArgs(List<string> objects)
    {
        var a = new List<string> { "--create", LibPath };
        a.AddRange(objects);
        return a;
    }

    private static string SafeFileName(string name)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder(name.Length);
        foreach (var ch in name)
            sb.Append(Array.IndexOf(invalid, ch) >= 0 ? '_' : ch);
        return sb.Length == 0 ? "target" : sb.ToString();
    }

    // ---------------------------------------------------------------- 依赖扫描

    private static readonly Regex IncludeRe = new(
        @"^\s*#\s*include\s*[<""]([^>""\r\n]+)[>""]",
        RegexOptions.Compiled);

    private List<string> ScanDependencies(string sourcePath, List<string> includeDirs)
    {
        var result = new List<string>();
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var queue = new Queue<string>();
        queue.Enqueue(Path.GetFullPath(sourcePath));

        while (queue.Count > 0 && visited.Count < 2000)
        {
            var current = queue.Dequeue();
            if (!visited.Add(current))
                continue;

            string[] lines;
            try
            {
                lines = File.ReadAllLines(current);
            }
            catch
            {
                continue;
            }

            var currentDir = Path.GetDirectoryName(current) ?? "";
            foreach (var line in lines)
            {
                var m = IncludeRe.Match(line);
                if (!m.Success)
                    continue;

                var name = m.Groups[1].Value.Trim();
                if (name.Length == 0)
                    continue;

                // 1) 与当前文件同目录
                var found = Path.GetFullPath(Path.Combine(currentDir, name));
                if (!File.Exists(found))
                {
                    // 2) -I 目录
                    found = "";
                    foreach (var dir in includeDirs)
                    {
                        var cand = Path.GetFullPath(Path.Combine(dir, name));
                        if (File.Exists(cand))
                        {
                            found = cand;
                            break;
                        }
                    }
                }

                if (found.Length == 0 || visited.Contains(found))
                    continue;

                result.Add(found);
                queue.Enqueue(found);
            }
        }

        return result;
    }

    private bool IsUpToDate(string source, string objectPath, string signature, List<string> includeDirs)
    {
        var objInfo = new FileInfo(objectPath);
        if (!objInfo.Exists)
            return false;

        // 编译选项变了（包括切 Target 导致 include/define 变化）就必须重编
        if (!_signatures.TryGetValue(objectPath, out var known) || known != signature)
            return false;

        var objTime = objInfo.LastWriteTimeUtc;
        if (File.GetLastWriteTimeUtc(source) > objTime)
            return false;

        foreach (var dep in ScanDependencies(source, includeDirs))
        {
            if (File.GetLastWriteTimeUtc(dep) > objTime)
                return false;
        }
        return true;
    }

    // ---------------------------------------------------------------- 签名文件

    private void LoadSignatures()
    {
        _signatures.Clear();
        if (_signaturePath.Length == 0 || !File.Exists(_signaturePath))
            return;

        try
        {
            foreach (var line in File.ReadAllLines(_signaturePath))
            {
                var tab = line.IndexOf('\t');
                if (tab <= 0)
                    continue;
                _signatures[line[(tab + 1)..]] = line[..tab];
            }
        }
        catch
        {
            // 读不了就当没有，反正会全量重编
        }
    }

    private void SaveSignatures()
    {
        if (_signaturePath.Length == 0 || !_signaturesDirty)
            return;

        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_signaturePath)!);
            var lines = _signatures.Select(kv => kv.Value + "\t" + kv.Key);
            File.WriteAllLines(_signaturePath, lines);
            _signaturesDirty = false;
        }
        catch
        {
            // 存不下就下次全量编
        }
    }

    private static string SignatureOf(IEnumerable<string> args)
    {
        using var sha = System.Security.Cryptography.SHA1.Create();
        var bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(string.Join("\u0001", args)));
        return Convert.ToHexString(bytes);
    }

    // ---------------------------------------------------------------- 对外入口

    public async Task<bool> BuildAsync(bool rebuildAll, CancellationToken ct = default)
    {
        if (_running)
            return false;

        var t = Target;
        if (t is null)
            return false;

        ResetRun(Mode.Build);
        _startedAt = DateTime.UtcNow;
        _running = true;
        _cancelled = false;

        try
        {
            PrepareBuild(rebuildAll);
            await ExecuteJobsAsync(ct);
        }
        catch (OperationCanceledException)
        {
            Emit("*** 构建已取消");
        }
        finally
        {
            FinishRun(_errors == 0);
        }

        return _errors == 0;
    }

    public async Task<bool> TranslateAsync(string absoluteFilePath, CancellationToken ct = default)
    {
        if (_running || Target is null)
            return false;

        ResetRun(Mode.Translate);
        _startedAt = DateTime.UtcNow;
        _running = true;

        try
        {
            PrepareTranslate(absoluteFilePath);
            await ExecuteJobsAsync(ct);
        }
        finally
        {
            FinishRun(_errors == 0);
        }

        return _errors == 0;
    }

    public void Clean()
    {
        var t = Target;
        ResetRun(Mode.Clean);
        Emit($"Clean started: Project: {_project.BaseName}, Configuration: {t?.Name}");
        PrepareClean();
        Emit("Clean finished.");
        Finished?.Invoke(true, 0, 0);
    }

    public void Cancel()
    {
        if (!_running)
            return;
        _cancelled = true;
        _activeProcesses.ForEach(p =>
        {
            try { p.Kill(true); } catch { }
        });
    }

    private void ResetRun(Mode mode)
    {
        _mode = mode;
        _errors = 0;
        _warnings = 0;
        _cancelled = false;
    }

    private void Emit(string line) => Output?.Invoke(line);

    // ---------------------------------------------------------------- 组装任务

    private void PrepareBuild(bool rebuildAll)
    {
        var t = Target!;

        Emit($"Build started: Project: {_project.BaseName}, Configuration: {t.Name}");

        var version = _toolchain.QueryVersion();
        Emit($"*** Using Compiler '{(version.Length > 0 ? version : _toolchain.Family)}', folder: '{_toolchain.BinDir}'");
        Emit($"Build target '{t.Name}'");

        Directory.CreateDirectory(OutputDir);

        // scatter 要在组装链接参数之前生成，LinkArgs() 里会用到
        _generatedScatter = "";
        if (t.UseMemoryLayout && !(t.CreateLib && !t.CreateExecutable))
            _generatedScatter = WriteScatterFile();

        var sigDir = Path.Combine(OutputDir, ".mdk-ose");
        Directory.CreateDirectory(sigDir);
        _signaturePath = Path.Combine(sigDir, SafeFileName(t.Name) + ".sig");
        LoadSignatures();

        foreach (var p in t.BeforeCompile)
        {
            if (p.Enabled)
                Emit($"*** 用户命令: {p.Command}");
        }

        var makeLibrary = t.CreateLib && !t.CreateExecutable;
        var objects = new List<string>();
        var usedNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var compileJobs = new List<Job>();
        var total = 0;

        foreach (var g in t.Groups)
        {
            if (g.Excluded)
                continue;

            foreach (var f in g.Files)
            {
                var kind = f.Kind;
                var src = _project.ToAbsolute(f.Path);

                if (kind is FileKind.Object or FileKind.Library)
                {
                    if (!f.Excluded)
                        objects.Add(src);
                    continue;
                }

                if (kind is not (FileKind.CSource or FileKind.CppSource or FileKind.Assembly))
                    continue;
                if (f.Excluded)
                    continue;

                ++total;

                if (!File.Exists(src))
                {
                    Emit($"*** Error: 找不到源文件 \"{src}\"");
                    ++_errors;
                    continue;
                }

                var baseName = Path.GetFileNameWithoutExtension(src);
                if (!usedNames.Add(baseName))
                {
                    var n = 2;
                    while (!usedNames.Add(baseName + "_" + n))
                        ++n;
                    baseName = baseName + "_" + n;
                }

                var obj = Path.Combine(OutputDir, baseName + ".o");
                objects.Add(obj);

                var isAsm = kind == FileKind.Assembly;
                var args = isAsm ? AssembleArgs(g, f, src, obj) : CompileArgs(g, f, src, obj);
                var program = isAsm ? _toolchain.As : _toolchain.Cc;

                if (program.Length == 0)
                {
                    Emit("*** Error: 找不到 armcc / armasm，请检查编译器路径");
                    ++_errors;
                    return;
                }

                var signature = SignatureOf(args);
                var upToDate = !rebuildAll
                    && IsUpToDate(src, obj, signature,
                        MergeIncludeDirs(isAsm ? TargetAsmIncludeDirs() : TargetIncludeDirs(), g, f));

                _signatures[obj] = signature;
                _signaturesDirty = true;

                if (upToDate)
                    continue;

                compileJobs.Add(new Job(
                    program, args,
                    (isAsm ? "assembling " : "compiling ") + Path.GetFileName(src) + "...",
                    Parallel: true));
            }
        }

        SaveSignatures();

        if (total == 0)
            Emit("*** Warning: 这个 Target 没有任何参与编译的源文件");
        if (compileJobs.Count == 0 && total > 0)
            Emit("Target is up to date.");

        if (objects.Count == 0)
        {
            if (_errors == 0)
            {
                Emit("*** Error: 没有可链接的目标文件");
                ++_errors;
            }
            return;
        }

        _pendingJobs.AddRange(t.BeforeCompile
            .Where(p => p.Enabled)
            .Select(p => HookJob(p, stopsOnError: false)));

        _pendingJobs.AddRange(compileJobs);

        _pendingJobs.AddRange(t.BeforeMake
            .Where(p => p.Enabled)
            .Select(p => HookJob(p, stopsOnError: true)));

        if (makeLibrary)
        {
            _pendingJobs.Add(new Job(_toolchain.Ar, LibArgs(objects), "creating library...",
                IsLink: true, SkipIfErrors: true));
        }
        else
        {
            _pendingJobs.Add(new Job(_toolchain.Ld, LinkArgs(objects), "linking...",
                IsLink: true, SkipIfErrors: true));
        }

        if (!makeLibrary && t.CreateHexFile && _toolchain.From.Length > 0)
        {
            var hex = Path.Combine(OutputDir, t.ArtifactName + ".hex");
            _pendingJobs.Add(new Job(_toolchain.From,
                new[] { "--i32", "--output=" + hex, AxfPath },
                "creating hex file...", SkipIfErrors: true));
        }

        _pendingJobs.AddRange(t.AfterMake
            .Where(p => p.Enabled)
            .Select(p => HookJob(p, stopsOnError: true)));

        _axfForSize = makeLibrary ? "" : AxfPath;
    }

    private Job HookJob(UserProgram p, bool stopsOnError)
    {
        var parts = SplitArgsLoose(p.Command).ToList();
        var program = parts.Count > 0 ? parts[0] : "";
        var args = parts.Skip(1).ToList();
        return new Job(program, args, "*** user command: " + p.Command,
            IsHook: true, HookStopsOnError: stopsOnError, HookCommand: p.Command);
    }

    private void PrepareTranslate(string absoluteFilePath)
    {
        var t = Target!;
        Emit($"Build started: Project: {_project.BaseName}, Configuration: {t.Name}");

        var version = _toolchain.QueryVersion();
        Emit($"*** Using Compiler '{(version.Length > 0 ? version : _toolchain.Family)}', folder: '{_toolchain.BinDir}'");

        Directory.CreateDirectory(OutputDir);

        var isAsm = UvProject.KindOfPath(absoluteFilePath) == FileKind.Assembly;
        var obj = Path.Combine(OutputDir, Path.GetFileNameWithoutExtension(absoluteFilePath) + ".o");
        var args = isAsm
            ? AssembleArgs(null, new ProjFile { Path = absoluteFilePath }, absoluteFilePath, obj)
            : CompileArgs(null, new ProjFile { Path = absoluteFilePath }, absoluteFilePath, obj);

        _pendingJobs.Add(new Job(isAsm ? _toolchain.As : _toolchain.Cc, args,
            (isAsm ? "assembling " : "compiling ") + Path.GetFileName(absoluteFilePath) + "..."));
        _axfForSize = "";
    }

    private void PrepareClean()
    {
        var dir = OutputDir;
        if (Directory.Exists(dir))
        {
            var filters = new[] { "*.o", "*.axf", "*.lib", "*.hex", "*.bin", "*.map", "*.lst", "*.crf", "*.d", "*.lnp" };
            foreach (var f in Directory.EnumerateFiles(dir))
            {
                var name = Path.GetFileName(f);
                if (!filters.Any(p => name.EndsWith(p[1..], StringComparison.OrdinalIgnoreCase)))
                    continue;
                try
                {
                    File.Delete(f);
                    Emit($"\".\\{name}\" - Deleted.");
                }
                catch
                {
                    // 删不掉（被占用）就算了
                }
            }
        }

        // 顺手清掉增量签名，保证下次一定全量
        try
        {
            var sigDir = Path.Combine(dir, ".mdk-ose");
            if (Directory.Exists(sigDir))
                Directory.Delete(sigDir, recursive: true);
        }
        catch
        {
            // ignore
        }
    }

    // ---------------------------------------------------------------- 执行

    private readonly List<Job> _pendingJobs = new();
    private readonly List<Process> _activeProcesses = new();

    private async Task ExecuteJobsAsync(CancellationToken ct)
    {
        var jobs = _pendingJobs.ToList();
        _pendingJobs.Clear();

        var totalToCompile = jobs.Count(j => j.Parallel);
        var doneCompile = 0;

        for (var i = 0; i < jobs.Count; i++)
        {
            var job = jobs[i];

            if (_cancelled || ct.IsCancellationRequested)
                throw new OperationCanceledException();

            if (job.SkipIfErrors && _errors > 0)
                return;

            if (job.Parallel)
            {
                // 收集连续的并行批次，一起跑（受 _jobLimit 限制）
                var batch = new List<Job>();
                while (i < jobs.Count && jobs[i].Parallel)
                    batch.Add(jobs[i++]);
                i--;

                await RunParallelAsync(batch, () =>
                {
                    ++doneCompile;
                    Status?.Invoke($"compiling {doneCompile}/{totalToCompile}");
                }, ct);

                if (_errors > 0)
                    return;
            }
            else
            {
                await RunOneAsync(job, buffered: false, ct);
                if (job.IsLink && _errors == 0 && _axfForSize.Length > 0 && File.Exists(_axfForSize))
                {
                    EmitProgramSize();
                    if (File.Exists(MapPath))
                        MapFileReady?.Invoke(MapPath);
                }
            }
        }
    }

    private async Task RunParallelAsync(List<Job> batch, Action onDone, CancellationToken ct)
    {
        using var semaphore = new SemaphoreSlim(_jobLimit);
        var tasks = batch.Select(async job =>
        {
            await semaphore.WaitAsync(ct);
            try
            {
                await RunOneAsync(job, buffered: true, ct);
                onDone();
            }
            finally
            {
                semaphore.Release();
            }
        });
        await Task.WhenAll(tasks);
    }

    private async Task RunOneAsync(Job job, bool buffered, CancellationToken ct)
    {
        if (job.Program.Length == 0)
        {
            Emit($"*** Error: 程序路径为空（{job.Title}）");
            ++_errors;
            return;
        }

        var lines = new List<string>();
        var errorDiagnostics = 0;

        void HandleLine(string line)
        {
            if (line.Length == 0)
                return;

            if (Diagnostics.TryParse(line, ProjectDir, out var d))
            {
                if (d.Severity == Severity.Error)
                {
                    ++_errors;
                    ++errorDiagnostics;
                }
                else if (d.Severity == Severity.Warning)
                {
                    ++_warnings;
                }
                DiagnosticFound?.Invoke(d);
            }
        }

        if (!buffered && job.Title.Length > 0)
            Emit(job.Title);

        var psi = new ProcessStartInfo(job.Program)
        {
            WorkingDirectory = ProjectDir,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };
        foreach (var a in job.Args)
            psi.ArgumentList.Add(a);

        // 让 fromelf.exe 之类能在 PATH 里找到
        if (_toolchain.BinDir.Length > 0)
        {
            var path = psi.Environment.TryGetValue("PATH", out var p) ? p : "";
            psi.Environment["PATH"] = _toolchain.BinDir + Path.PathSeparator + path;
        }

        Process? proc = null;
        try
        {
            proc = Process.Start(psi);
        }
        catch (Exception ex)
        {
            Emit($"*** Error: 无法启动 \"{job.Program}\"（{ex.Message}）");
            ++_errors;
            return;
        }

        if (proc is null)
        {
            Emit($"*** Error: 无法启动 \"{job.Program}\"");
            ++_errors;
            return;
        }

        lock (_activeProcesses)
            _activeProcesses.Add(proc);

        var stdoutTask = PumpAsync(proc.StandardOutput, lines, buffered);
        var stderrTask = PumpAsync(proc.StandardError, lines, buffered);

        await proc.WaitForExitAsync(CancellationToken.None);
        await Task.WhenAll(stdoutTask, stderrTask);

        lock (_activeProcesses)
            _activeProcesses.Remove(proc);

        foreach (var line in lines)
            HandleLine(line);

        var ok = proc.ExitCode == 0;
        if (buffered && job.Title.Length > 0)
            Emit(job.Title);
        foreach (var line in lines)
            Emit(line);

        if (!ok && !_cancelled)
        {
            if (job.IsHook)
            {
                if (job.HookStopsOnError)
                {
                    ++_errors;
                    Emit($"*** 用户命令失败: {job.HookCommand}");
                }
                else
                {
                    ++_warnings;
                    Emit($"*** 用户命令返回 {proc.ExitCode}（已忽略）: {job.HookCommand}");
                }
            }
            else
            {
                // 编译器自己打过 Error 行时不要重复计数
                if (errorDiagnostics == 0)
                {
                    ++_errors;
                    Emit($"*** {(job.IsLink ? "链接" : "编译命令")} 失败（退出码 {proc.ExitCode}）");
                }
                if (job.IsLink)
                    Emit("*** 链接失败");
            }
        }

        proc.Dispose();
    }

    private static async Task PumpAsync(StreamReader reader, List<string> sink, bool collect)
    {
        while (true)
        {
            var line = await reader.ReadLineAsync();
            if (line is null)
                break;
            if (collect)
                lock (sink) sink.Add(line);
        }
    }

    // ---------------------------------------------------------------- 收尾

    private void EmitProgramSize()
    {
        var mapPath = Path.Combine(Path.GetDirectoryName(_axfForSize) ?? "",
                                   Path.GetFileNameWithoutExtension(_axfForSize) + ".map");
        if (!File.Exists(mapPath))
            return;

        // armlink 的 map 里，Totals 行是：
        //    Code (inc. data)  RO Data  RW Data  ZI Data  Debug   Grand Totals
        // 也就是 6 列；某些版本只给 5 列，所以要按列数分别解释。
        var totalsRe = new Regex(
            @"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*(\d+)?\s*(?:Object|Library|Grand)\s+Totals.*$",
            RegexOptions.Compiled);

        long code = 0, ro = 0, rw = 0, zi = 0;
        var found = false;

        foreach (var line in File.ReadLines(mapPath))
        {
            var m = totalsRe.Match(line);
            if (!m.Success)
                continue;

            var sixColumns = m.Groups[6].Value.Length > 0;
            code = long.Parse(m.Groups[1].Value);
            if (sixColumns)
            {
                ro = long.Parse(m.Groups[3].Value);
                rw = long.Parse(m.Groups[4].Value);
                zi = long.Parse(m.Groups[5].Value);
            }
            else
            {
                ro = long.Parse(m.Groups[2].Value);
                rw = long.Parse(m.Groups[3].Value);
                zi = long.Parse(m.Groups[4].Value);
            }
            found = true;

            if (line.Contains("Grand Totals"))
                break;
        }

        if (!found)
            return;

        var text = $"Program Size: Code={code} RO-data={ro} RW-data={rw} ZI-data={zi}";
        Emit(text);
        ProgramSize?.Invoke(text);
    }

    private void FinishRun(bool success)
    {
        if (!_running)
            return;

        _running = false;
        SaveSignatures();
        _pendingJobs.Clear();

        if (_mode is Mode.Build or Mode.Translate)
        {
            var t = Target;
            var name = t?.ArtifactName ?? "Project";
            var makeLibrary = t is { CreateLib: true, CreateExecutable: false };

            var dir = t is { OutputDirectory.Length: > 0 } ? t.OutputDirectory : ".\\Objects\\";
            dir = dir.Replace('/', '\\');
            if (!dir.EndsWith('\\'))
                dir += "\\";

            var artifact = dir + name + (makeLibrary ? ".lib" : ".axf");
            Emit($"\"{artifact}\" - {_errors} Error(s), {_warnings} Warning(s).");
            Emit($"Build Time Elapsed:  {FormatElapsed(DateTime.UtcNow - _startedAt)}");
        }

        Finished?.Invoke(success && _errors == 0, _errors, _warnings);
    }

    private static string FormatElapsed(TimeSpan span)
    {
        var total = (long)span.TotalSeconds;
        return $"{(total / 3600):00}:{(total % 3600 / 60):00}:{(total % 60):00}";
    }
}
