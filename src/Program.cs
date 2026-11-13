using System;
using System.IO;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Headless;

namespace UvisionOse;

internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        // --screenshot <png> [--shot-delay ms] [--shot-map] [--shot-info]：
        // 离屏渲染一张界面图，跑 CI / 没有显示器的时候验证用
        // --build <uvprojx>：命令行构建，输出走 stdout，方便 CI
        if (Array.IndexOf(args, "--build") >= 0)
        {
            AttachParentConsole();
            return Cli.RunBuild(args);
        }

        var shotIndex = Array.IndexOf(args, "--screenshot");
        if (shotIndex >= 0 && shotIndex + 1 < args.Length)
        {
            AttachParentConsole();
            Screenshot.Run(args, args[shotIndex + 1]);
            return 0;
        }

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        return 0;
    }

    // GUI 程序（WinExe）在 Windows 上默认不占用父控制台，
    // 那么在 cmd 里跑 --shot-info 就什么也看不到。这里主动挂到父控制台上。
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AttachConsole(int dwProcessId);

    private const int AttachParentProcess = -1;

    private static void AttachParentConsole()
    {
        if (!OperatingSystem.IsWindows())
            return;

        try
        {
            if (!AttachConsole(AttachParentProcess))
                return;
            var stdout = new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true };
            var stderr = new StreamWriter(Console.OpenStandardError()) { AutoFlush = true };
            Console.SetOut(stdout);
            Console.SetError(stderr);
        }
        catch
        {
            // 挂不上就算了，输出丢掉比崩掉强
        }
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
