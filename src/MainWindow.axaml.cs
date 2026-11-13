using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Xml;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using AvaloniaEdit;
using AvaloniaEdit.Highlighting;
using AvaloniaEdit.Highlighting.Xshd;
using UvisionOse.Core;

namespace UvisionOse;

public partial class MainWindow : Window
{
    private UvProject _project = new();
    private int _activeTarget;
    private bool _loading;

    private readonly Dictionary<string, TextEditor> _openFiles = new(StringComparer.OrdinalIgnoreCase);
    private readonly ObservableCollection<Diagnostic> _errorsList = new();

    private readonly DispatcherTimer _barTimer;
    private readonly TranslateTransform _barTransform = new();
    private double _barPhase;

    private readonly BuildEngine _engine = new();
    private Toolchain _toolchain = new();
    private bool _building;
    private Task? _loadTask;

    // 截图验证模式下不自动恢复上次工程，免得和命令行传进来的工程打架
    public bool RestoreLastProject { get; set; } = true;

    public MainWindow()
    {
        InitializeComponent();

        LoadingBarFill.RenderTransform = _barTransform;
        _barTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
        _barTimer.Tick += (_, _) => AdvanceBar();

        ErrorList.ItemsSource = _errorsList;

        _engine.Output += line => Dispatcher.UIThread.Post(() => AppendOutput(line));
        _engine.Status += text => Dispatcher.UIThread.Post(() => StatusMessage.Text = text);
        _engine.DiagnosticFound += d => Dispatcher.UIThread.Post(() => _errorsList.Add(d));
        _engine.ProgramSize += size => Dispatcher.UIThread.Post(() => StatusSize.Text = size);
        _engine.Finished += (ok, errors, warnings) =>
            Dispatcher.UIThread.Post(() => OnBuildFinished(ok, errors, warnings));

        LoadToolchain();
        StatusPosition.Text = "Ln 1, Col 1";
        BuildRecentMenu();
    }

    protected override void OnOpened(EventArgs e)
    {
        base.OnOpened(e);

        var s = Settings.Instance;
        if (!string.IsNullOrEmpty(s.WindowGeometry))
        {
            try
            {
                var parts = s.WindowGeometry.Split(',');
                if (parts.Length == 4)
                {
                    Width = double.Parse(parts[0]);
                    Height = double.Parse(parts[1]);
                }
            }
            catch
            {
                // 解析不了就用默认尺寸
            }
        }

        // 自动打开上次的工程
        if (RestoreLastProject && !string.IsNullOrEmpty(s.LastProject) && File.Exists(s.LastProject))
            OpenProject(s.LastProject);
    }

    protected override void OnClosing(WindowClosingEventArgs e)
    {
        var s = Settings.Instance;
        s.WindowGeometry = $"{Width},{Height}";
        s.Save();
        base.OnClosing(e);
    }

    // F7 编译
    protected override void OnKeyDown(KeyEventArgs e)
    {
        if (e.Key == Key.F7)
        {
            _ = DoBuildAsync(rebuild: false);
            e.Handled = true;
            return;
        }
        base.OnKeyDown(e);
    }

    // ---------------------------------------------------------------- 工具链

    private void LoadToolchain()
    {
        var s = Settings.Instance;
        var dir = s.ToolchainDir;
        if (dir.Length == 0)
            dir = Toolchain.GuessBinDir(s.ToolchainFamily);

        _toolchain = Toolchain.Detect(dir, s.ToolchainFamily);
        _engine.SetToolchain(_toolchain);
        _engine.SetJobs(s.BuildJobs);
        UpdateToolchainStatus();
    }

    private void UpdateToolchainStatus()
    {
        if (_toolchain.IsValid)
        {
            StatusToolchain.Text = $"ARM Compiler {_toolchain.Family}";
            ToolTip.SetTip(StatusToolchain, _toolchain.Cc);
        }
        else
        {
            StatusToolchain.Text = "未配置编译器";
            ToolTip.SetTip(StatusToolchain, "工具 → 配置编译器…");
        }
    }

    private async void OnConfigureToolchainClick(object? sender, RoutedEventArgs e)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = "选择 ARM 编译器目录（armcc.exe / armclang.exe 所在目录）",
            AllowMultiple = false
        });

        if (folders.Count == 0)
            return;

        var dir = folders[0].Path.LocalPath;
        var detected = Toolchain.Detect(dir);
        if (!detected.IsValid)
        {
            StatusMessage.Text = $"这个目录里没有 armcc/armclang：{dir}";
            return;
        }

        var s = Settings.Instance;
        s.ToolchainDir = detected.BinDir;
        s.ToolchainFamily = detected.Family;
        s.Save();

        _toolchain = detected;
        _engine.SetToolchain(detected);
        UpdateToolchainStatus();
        StatusMessage.Text = $"已配置编译器：{detected.Family} @ {detected.BinDir}";
    }

    // ---------------------------------------------------------------- 工程

    public void OpenProject(string path)
    {
        _ = OpenProjectAsync(path);
    }

    // 已经在读工程的时候，返回同一个任务，调用方 await 到的是真正读完的时刻
    public Task OpenProjectAsync(string path)
    {
        if (_loadTask is { IsCompleted: false })
            return _loadTask;

        _loadTask = OpenProjectCoreAsync(path);
        return _loadTask;
    }

    private async Task OpenProjectCoreAsync(string path)
    {

        _loading = true;
        ShowLoading("加载中…", "正在读取工程文件…");
        await Task.Delay(16);

        var (project, error) = await Task.Run(() =>
        {
            UvprojReader.Load(path, out var p, out var err);
            return (p, err);
        });

        if (project is null || !project.IsValid)
        {
            HideLoading();
            _loading = false;
            StatusMessage.Text = error;
            AppendOutput(error);
            return;
        }

        _project = project;
        _activeTarget = 0;
        _engine.SetProject(_project, _activeTarget);

        var s = Settings.Instance;
        s.AddRecentProject(_project.FilePath);
        s.LastProject = _project.FilePath;
        s.Save();
        BuildRecentMenu();

        FillTargetCombo();
        FillTree();
        HideLoading();
        _loading = false;

        Title = $"{_project.BaseName} - uVision5-OSE";
        StatusMessage.Text = $"已加载工程 '{Path.GetFileName(_project.FilePath)}'。";
    }

    private void FillTargetCombo()
    {
        TargetCombo.Items.Clear();
        foreach (var t in _project.Targets)
            TargetCombo.Items.Add(t.Name);
        if (_project.Targets.Count > 0)
            TargetCombo.SelectedIndex = 0;
    }

    private void FillTree()
    {
        ProjectTree.Items.Clear();
        var target = _project.Target(_activeTarget);
        if (target is null)
            return;

        var root = new TreeViewItem
        {
            Header = $"{_project.BaseName} ({target.Name})",
            IsExpanded = true
        };

        foreach (var group in target.Groups)
        {
            var gItem = new TreeViewItem
            {
                Header = $"{group.Name} ({group.Files.Count})",
                IsExpanded = true
            };
            foreach (var file in group.Files)
                gItem.Items.Add(new TreeViewItem { Header = file.Name, Tag = file });
            root.Items.Add(gItem);
        }

        ProjectTree.Items.Add(root);
    }

    private void OnTreeSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (ProjectTree.SelectedItem is TreeViewItem { Tag: ProjFile file })
            OpenFile(file.Path);
    }

    private void OnTargetComboChanged(object? sender, SelectionChangedEventArgs e)
    {
        var index = TargetCombo.SelectedIndex;
        if (index < 0 || index == _activeTarget || _project.Targets.Count == 0)
            return;

        _activeTarget = index;
        _engine.SetProject(_project, _activeTarget);
        FillTree();
        Title = $"{_project.BaseName} ({_project.Target(_activeTarget)?.Name}) - uVision5-OSE";
    }

    // ---------------------------------------------------------------- 编辑

    private TextEditor? OpenFile(string path)
    {
        var abs = _project.ToAbsolute(path);
        if (!File.Exists(abs))
        {
            StatusMessage.Text = $"文件不存在: {abs}";
            return null;
        }

        if (_openFiles.TryGetValue(abs, out var existing))
        {
            foreach (var item in EditorTabs.Items.OfType<TabItem>())
            {
                if (ReferenceEquals(item.Content, existing))
                {
                    EditorTabs.SelectedItem = item;
                    return existing;
                }
            }
        }

        var editor = new TextEditor
        {
            ShowLineNumbers = true,
            FontFamily = new FontFamily("Consolas, DejaVu Sans Mono, Cascadia Mono, monospace"),
            FontSize = 13,
            Padding = new Thickness(4, 2),
            SyntaxHighlighting = LoadCHighlighting(),
            Text = File.ReadAllText(abs)
        };
        editor.TextArea.Caret.PositionChanged += (_, _) => OnCaretMoved(editor);

        _openFiles[abs] = editor;

        var tab = new TabItem
        {
            Header = Path.GetFileName(abs),
            Content = editor,
            Tag = abs
        };
        EditorTabs.Items.Add(tab);
        EditorTabs.SelectedItem = tab;

        StatusMessage.Text = abs;
        return editor;
    }

    private void OnCaretMoved(TextEditor editor)
    {
        var caret = editor.TextArea.Caret;
        StatusPosition.Text = $"Ln {caret.Line}, Col {caret.Column}";
    }

    private void OnNewFileClick(object? sender, RoutedEventArgs e)
    {
        var editor = new TextEditor
        {
            ShowLineNumbers = true,
            FontFamily = new FontFamily("Consolas, DejaVu Sans Mono, Cascadia Mono, monospace"),
            FontSize = 13,
            Padding = new Thickness(4, 2),
            SyntaxHighlighting = LoadCHighlighting()
        };
        var tab = new TabItem { Header = "untitled.c", Content = editor };
        EditorTabs.Items.Add(tab);
        EditorTabs.SelectedItem = tab;
        editor.Focus();
    }

    private void OnSaveClick(object? sender, RoutedEventArgs e)
    {
        if (EditorTabs.SelectedItem is not TabItem { Tag: string path, Content: TextEditor editor })
        {
            StatusMessage.Text = "没有可保存的文件";
            return;
        }

        try
        {
            File.WriteAllText(path, editor.Text);
            StatusMessage.Text = $"已保存 {path}";
        }
        catch (Exception ex)
        {
            StatusMessage.Text = $"保存失败: {ex.Message}";
        }
    }

    private static IHighlightingDefinition? _cHighlighting;

    private static IHighlightingDefinition? LoadCHighlighting()
    {
        if (_cHighlighting is not null)
            return _cHighlighting;

        try
        {
            using var stream = AssetLoader.Open(new Uri("avares://uvision5-ose/Assets/C.xshd"));
            using var reader = XmlReader.Create(stream);
            _cHighlighting = HighlightingLoader.Load(reader, HighlightingManager.Instance);
            HighlightingManager.Instance.RegisterHighlighting("C", new[] { ".c", ".h" }, _cHighlighting);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"highlight load failed: {ex.Message}");
        }

        return _cHighlighting;
    }

    // ---------------------------------------------------------------- 构建

    private async void OnBuildClick(object? sender, RoutedEventArgs e) => await DoBuildAsync(rebuild: false);
    private async void OnRebuildClick(object? sender, RoutedEventArgs e) => await DoBuildAsync(rebuild: true);
    private void OnCleanClick(object? sender, RoutedEventArgs e) => DoClean();
    private void OnStopClick(object? sender, RoutedEventArgs e) => _engine.Cancel();

    private async Task DoBuildAsync(bool rebuild)
    {
        if (_building || _loading)
            return;

        if (!_project.IsValid)
        {
            StatusMessage.Text = "请先打开一个 .uvprojx 工程。";
            return;
        }

        if (!_toolchain.IsValid)
        {
            StatusMessage.Text = "还没配置 ARM 编译器：工具 → 配置编译器…";
            return;
        }

        _errorsList.Clear();
        BuildOutputText.Text = "";
        SetBuilding(true);
        _engine.SetProject(_project, _activeTarget);

        try
        {
            await _engine.BuildAsync(rebuild);
        }
        catch (Exception ex)
        {
            AppendOutput($"*** 构建异常: {ex.Message}");
        }
        finally
        {
            SetBuilding(false);
        }
    }

    private void DoClean()
    {
        if (!_project.IsValid || _building)
            return;

        _errorsList.Clear();
        BuildOutputText.Text = "";
        _engine.SetProject(_project, _activeTarget);

        _engine.Output += line => { };   // Clean 是同步的，输出走下面
        _engine.Clean();
        StatusMessage.Text = "清理完成";
    }

    private void OnBuildFinished(bool success, int errors, int warnings)
    {
        SetBuilding(false);
        StatusMessage.Text = success
            ? $"编译成功（{warnings} 个警告）"
            : $"编译失败：{errors} 个错误，{warnings} 个警告";

        if (errors > 0)
            OutputTabs.SelectedIndex = 1;
    }

    private void SetBuilding(bool busy)
    {
        _building = busy;
        BuildButton.IsEnabled = !busy;
        RebuildButton.IsEnabled = !busy;
        CleanButton.IsEnabled = !busy;
        StopButton.IsEnabled = busy;
    }

    private void AppendOutput(string line)
    {
        BuildOutputText.Text = string.IsNullOrEmpty(BuildOutputText.Text)
            ? line
            : BuildOutputText.Text + Environment.NewLine + line;

        // 一直跟到最新一行
        BuildOutputText.CaretIndex = BuildOutputText.Text?.Length ?? 0;
    }

    private void OnErrorListDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (ErrorList.SelectedItem is not Diagnostic d || !d.HasLocation)
            return;

        var editor = OpenFile(d.File);
        if (editor is null)
            return;

        var line = Math.Max(1, Math.Min(d.Line, editor.LineCount));
        editor.ScrollToLine(line);
        editor.TextArea.Caret.Line = line;
        if (d.Column > 0)
            editor.TextArea.Caret.Column = d.Column;
        editor.Focus();
    }

    // ---------------------------------------------------------------- 菜单杂项

    private void OnToggleProjectDock(object? sender, RoutedEventArgs e) { }
    private void OnToggleOutputDock(object? sender, RoutedEventArgs e) { }

    private void BuildRecentMenu()
    {
        RecentMenu.Items.Clear();
        foreach (var p in Settings.Instance.RecentProjects)
        {
            var item = new MenuItem { Header = Path.GetFileName(p) };
            ToolTip.SetTip(item, p);
            item.Click += (_, _) => OpenProject(p);
            RecentMenu.Items.Add(item);
        }
        if (RecentMenu.Items.Count == 0)
            RecentMenu.Items.Add(new MenuItem { Header = "（空）", IsEnabled = false });
    }

    private async void OnOpenProjectClick(object? sender, RoutedEventArgs e)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "打开 uVision 工程",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("uVision 工程") { Patterns = new[] { "*.uvprojx", "*.uvproj" } }
            }
        });

        if (files.Count > 0)
            OpenProject(files[0].Path.LocalPath);
    }

    private void OnCloseProjectClick(object? sender, RoutedEventArgs e)
    {
        _project = new UvProject();
        _openFiles.Clear();
        EditorTabs.Items.Clear();
        ProjectTree.Items.Clear();
        TargetCombo.Items.Clear();
        _errorsList.Clear();
        Title = "uVision5-OSE";
        StatusMessage.Text = "就绪";
    }

    private void OnExitClick(object? sender, RoutedEventArgs e) => Close();

    private void OnAboutClick(object? sender, RoutedEventArgs e)
    {
        StatusMessage.Text = "uVision5-OSE —— 开源的 uVision5 风格嵌入式 IDE 前端。" +
                             "本程序不分发 ARM Compiler，请自行安装 Keil MDK 并配置编译器路径。";
    }

    // ---------------------------------------------------------------- 加载遮罩

    private void ShowLoading(string title, string detail)
    {
        LoadingTitle.Text = title;
        LoadingDetail.Text = detail;
        LoadingBarFill.Width = 140;

        // 底下的界面整体糊掉，遮罩才像“浮在上面”
        ContentRoot.Effect = new BlurEffect { Radius = 16 };
        LoadingLayer.IsVisible = true;
        _barPhase = 0.28;
        _barTimer.Start();
    }

    private void HideLoading()
    {
        _barTimer.Stop();
        LoadingLayer.IsVisible = false;
        ContentRoot.Effect = null;
    }

    private void AdvanceBar()
    {
        var track = LoadingBarTrack.Bounds.Width;
        var fill = LoadingBarFill.Width;
        if (track <= fill)
            return;

        _barPhase += 0.008;
        if (_barPhase >= 1.0)
            _barPhase -= 1.0;

        _barTransform.X = (track - fill) * _barPhase;
    }

    // ---------------------------------------------------------------- 验证用

    public void OpenFirstSource()
    {
        var file = _project.Target(_activeTarget)?.AllFiles.FirstOrDefault(f => f.Kind == FileKind.CSource)
                   ?? _project.Target(_activeTarget)?.AllFiles.FirstOrDefault();
        if (file is not null)
            OpenFile(file.Path);
    }

    public void DumpState()
    {
        Console.WriteLine($"window {Bounds.Width}x{Bounds.Height}");
        Console.WriteLine($"tree bounds={ProjectTree.Bounds} items={ProjectTree.Items.Count}");
        Console.WriteLine($"tabs={EditorTabs.Items.Count} status='{StatusMessage.Text}'");
        Console.WriteLine($"targetCombo items={TargetCombo.Items.Count} selected={TargetCombo.SelectedIndex}");
        Console.WriteLine($"toolchain='{_toolchain.DisplayName}' valid={_toolchain.IsValid}");
        Console.WriteLine($"errors={_errorsList.Count} output='{(BuildOutputText.Text ?? "").Replace(Environment.NewLine, " | ")}'");
        void Dump(TreeViewItem item, string indent)
        {
            Console.WriteLine($"{indent}tree[{item.Header}] children={item.Items.Count} expanded={item.IsExpanded}");
            foreach (var child in item.Items.OfType<TreeViewItem>())
                Dump(child, indent + "  ");
        }

        foreach (var item in ProjectTree.Items.OfType<TreeViewItem>())
            Dump(item, "  ");
    }
}
