using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Xml;
using Avalonia;
using Avalonia.Controls;
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
    private readonly DispatcherTimer _barTimer;
    private readonly TranslateTransform _barTransform = new();
    private double _barPhase;

    public MainWindow()
    {
        InitializeComponent();

        LoadingBarFill.RenderTransform = _barTransform;

        _barTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(16) };
        _barTimer.Tick += (_, _) => AdvanceBar();

        StatusToolchain.Text = "未配置编译器";
        StatusPosition.Text = "Ln 1, Col 1";
    }

    // ---------------------------------------------------------------- 工程

    public void OpenProject(string path)
    {
        _ = OpenProjectAsync(path);
    }

    // 读工程放后台线程，主线程只管画遮罩和最后铺界面 —— 工程再大也不会假死
    public async Task OpenProjectAsync(string path)
    {
        if (_loading)
            return;

        _loading = true;
        ShowLoading("加载中…", "正在读取工程文件…");

        // 先让遮罩画出来，再开始干活
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
            var gItem = new TreeViewItem { Header = group.Name, IsExpanded = true };
            foreach (var file in group.Files)
            {
                gItem.Items.Add(new TreeViewItem
                {
                    Header = file.Name,
                    Tag = file
                });
            }
            root.Items.Add(gItem);
        }

        ProjectTree.Items.Add(root);
    }

    private void OnTreeSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (ProjectTree.SelectedItem is TreeViewItem { Tag: ProjFile file })
            OpenFile(file);
    }

    private void OpenFile(ProjFile file)
    {
        var abs = _project.ToAbsolute(file.Path);
        if (!File.Exists(abs))
        {
            StatusMessage.Text = $"文件不存在: {abs}";
            return;
        }

        if (_openFiles.TryGetValue(abs, out var existing))
        {
            foreach (var item in EditorTabs.Items.OfType<TabItem>())
            {
                if (ReferenceEquals(item.Content, existing))
                {
                    EditorTabs.SelectedItem = item;
                    return;
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

        _openFiles[abs] = editor;

        var tab = new TabItem
        {
            Header = file.Name,
            Content = editor
        };
        EditorTabs.Items.Add(tab);
        EditorTabs.SelectedItem = tab;

        StatusMessage.Text = abs;
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

        // 滑块从左跑到右，循环
        _barTransform.X = (track - fill) * _barPhase;
    }

    // ---------------------------------------------------------------- 事件

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
        Title = "uVision5-OSE";
        StatusMessage.Text = "就绪";
    }

    private void OnBuildClick(object? sender, RoutedEventArgs e)
    {
        AppendOutput("== 构建引擎还没移植过来 ==");
        StatusMessage.Text = "构建引擎待移植";
    }

    private void AppendOutput(string line)
    {
        BuildOutputText.Text = string.IsNullOrEmpty(BuildOutputText.Text)
            ? line
            : BuildOutputText.Text + Environment.NewLine + line;
    }

    private void OnExitClick(object? sender, RoutedEventArgs e) => Close();

    // 截图验证用：把工程里第一个 .c 打开
    public void OpenFirstSource()
    {
        var target = _project.Target(_activeTarget);
        if (target is null)
            return;
        var file = target.AllFiles.FirstOrDefault(f => f.Kind == FileKind.CSource)
                   ?? target.AllFiles.FirstOrDefault();
        if (file is not null)
            OpenFile(file);
    }

    // headless 截图模式下把界面状态打出来，方便在没显示器的地方验证
    public void DumpState()
    {
        Console.WriteLine($"window {Bounds.Width}x{Bounds.Height}");
        Console.WriteLine($"tree bounds={ProjectTree.Bounds} items={ProjectTree.Items.Count}");
        Console.WriteLine($"tabs={EditorTabs.Items.Count} status='{StatusMessage.Text}'");
        Console.WriteLine($"targetCombo items={TargetCombo.Items.Count} selected={TargetCombo.SelectedIndex}");
        foreach (var item in ProjectTree.Items)
        {
            if (item is TreeViewItem ti)
                Console.WriteLine($"  tree[{ti.Header}] children={ti.Items.Count}");
        }
    }
}
