using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace UvisionOse.Core;

// 全局配置，刻意写成 JSON 文件而不是注册表，方便排查和迁移：
//   Windows: %APPDATA%\linuxacme\mdk-v5-ose.json
//   Linux:   ~/.config/linuxacme/mdk-v5-ose.json
public sealed class Settings
{
    private static Settings? _instance;
    public static Settings Instance => _instance ??= Load();

    // ---- 工具链 ----
    public string ToolchainDir { get; set; } = "";
    public string ToolchainFamily { get; set; } = "";   // "AC5" / "AC6"

    // ---- 构建 ----
    // 并行编译任务数，默认取本机逻辑处理器数；1 表示串行
    public int BuildJobs { get; set; } = Math.Max(1, Environment.ProcessorCount);

    // ---- 界面 ----
    public string Language { get; set; } = "zh";        // "en" / "zh"

    // ---- 编辑器 ----
    public bool FormatOnSave { get; set; }
    public string ClangFormatPath { get; set; } = "";

    // ---- 最近打开的工程 ----
    public string LastProject { get; set; } = "";
    public List<string> RecentProjects { get; set; } = new();

    // ---- 窗口状态 ----
    public string WindowGeometry { get; set; } = "";

    public static string FilePath
    {
        get
        {
            // ApplicationData 在某些精简环境里会是空串，那样会往当前目录里写，
            // 所以按 配置目录 -> 用户目录 -> 程序目录 依次退
            var root = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            if (string.IsNullOrEmpty(root))
                root = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            if (string.IsNullOrEmpty(root))
                root = AppContext.BaseDirectory;

            return Path.Combine(root, "linuxacme", "mdk-v5-ose.json");
        }
    }

    private static Settings Load()
    {
        try
        {
            var path = FilePath;
            if (File.Exists(path))
            {
                var json = File.ReadAllText(path);
                var loaded = JsonSerializer.Deserialize<Settings>(json, JsonOptions);
                if (loaded is not null)
                {
                    loaded.RecentProjects ??= new List<string>();
                    if (loaded.BuildJobs < 1)
                        loaded.BuildJobs = 1;
                    return loaded;
                }
            }
        }
        catch
        {
            // 配置坏了就当没配过，别拦着程序启动
        }
        return new Settings();
    }

    public void Save()
    {
        try
        {
            var path = FilePath;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, JsonSerializer.Serialize(this, JsonOptions));
        }
        catch
        {
            // 写不进去也不影响本次运行
        }
    }

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never
    };

    // 最近工程最多留 10 个，重的挪到最前
    public void AddRecentProject(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
            return;

        RecentProjects.RemoveAll(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
        RecentProjects.Insert(0, path);
        while (RecentProjects.Count > 10)
            RecentProjects.RemoveAt(RecentProjects.Count - 1);
        LastProject = path;
    }
}
