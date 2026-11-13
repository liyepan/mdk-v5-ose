using System;
using System.Linq;
using Avalonia;
using Avalonia.Headless;

namespace UvisionOse;

internal static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        // --screenshot <png> [--shot-delay ms]：离屏渲染一张界面图，跑 CI / 验证用
        var shotIndex = Array.IndexOf(args, "--screenshot");
        if (shotIndex >= 0 && shotIndex + 1 < args.Length)
        {
            Screenshot.Run(args, args[shotIndex + 1]);
            return;
        }

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    // 设计器和 StartWithClassicDesktopLifetime 都用这个
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
#if DEBUG
            .WithDeveloperTools()
#endif
            .WithInterFont()
            .LogToTrace();
}
