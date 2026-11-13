using System;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Headless;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Skia;
using Avalonia.Threading;

namespace UvisionOse;

// 用 Avalonia 的 headless 后端把窗口真渲染一遍再存成 PNG。
// 这样在没有 X 的机器上也能验证界面（和 Qt 那边 --screenshot 是一个思路）。
internal static class Screenshot
{
    public static void Run(string[] args, string outputPath)
    {
        var delayIndex = Array.IndexOf(args, "--shot-delay");
        var delay = delayIndex >= 0 && delayIndex + 1 < args.Length
            ? int.Parse(args[delayIndex + 1])
            : 800;

        var project = args.FirstOrDefault(a => !a.StartsWith("--") && a.EndsWith(".uvprojx", StringComparison.OrdinalIgnoreCase));

        AppBuilder.Configure<App>()
            .UseSkia()
            .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
            .WithInterFont()
            .SetupWithoutStarting();

        var window = new MainWindow();
        window.Show();

        if (!string.IsNullOrEmpty(project))
            PumpUntil(() => window.OpenProjectAsync(project!), 30000);

        if (args.Contains("--shot-open"))
            window.OpenFirstSource();

        // 让计时器、布局、动画跑起来
        var until = DateTime.UtcNow.AddMilliseconds(delay);
        while (DateTime.UtcNow < until)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(10);
        }

        Dispatcher.UIThread.RunJobs();
        var frame = window.CaptureRenderedFrame();
        if (frame is null)
        {
            Console.Error.WriteLine("capture failed");
            return;
        }

        frame.Save(outputPath);
        Console.WriteLine($"saved {outputPath} ({frame.PixelSize.Width}x{frame.PixelSize.Height})");

        if (args.Contains("--shot-map"))
            DumpMap(frame);

        if (args.Contains("--shot-info"))
            window.DumpState();
    }

    // 一边泵消息循环一边等异步任务（headless 下没有真正的 app.Run）
    private static void PumpUntil(Func<Task> start, int timeoutMs)
    {
        var task = start();
        var until = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (!task.IsCompleted && DateTime.UtcNow < until)
        {
            Dispatcher.UIThread.RunJobs();
            Thread.Sleep(5);
        }
    }

    // 没有图形界面看的时候，用字符画把版面“看”一眼
    private static void DumpMap(WriteableBitmap bitmap)
    {
        const string ramp = " .:-=+*#%@";
        using var fb = bitmap.Lock();
        var w = fb.Size.Width;
        var h = fb.Size.Height;
        var mapW = 78;
        var cell = Math.Max(1, w / mapW);
        var cellY = cell * 2;                      // 字符是竖长的，纵向两个像素块拼一格
        var mapH = Math.Max(1, h / cellY);

        var sb = new StringBuilder();
        sb.AppendLine(new string('-', mapW));
        for (var my = 0; my < mapH; my++)
        {
            for (var mx = 0; mx < mapW; mx++)
            {
                // 取格子里的最暗点：文字和边框很细，取平均会被白色淹没
                var v = 255;
                for (var y = my * cellY; y < Math.Min(h, (my + 1) * cellY); y += 1)
                {
                    for (var x = mx * cell; x < Math.Min(w, (mx + 1) * cell); x += 1)
                    {
                        var px = ReadPixel(fb, x, y);
                        var lum = (int)(0.299 * px.R + 0.587 * px.G + 0.114 * px.B);
                        if (lum < v)
                            v = lum;
                    }
                }
                var idx = Math.Clamp((255 - v) * ramp.Length / 256, 0, ramp.Length - 1);
                sb.Append(ramp[idx]);
            }
            sb.AppendLine();
        }
        sb.AppendLine(new string('-', mapW));
        Console.Write(sb.ToString());
    }

    private static (byte R, byte G, byte B) ReadPixel(Avalonia.Platform.ILockedFramebuffer fb, int x, int y)
    {
        unsafe
        {
            var p = (byte*)fb.Address + y * fb.RowBytes + x * 4;
            return fb.Format == Avalonia.Platform.PixelFormat.Bgra8888
                ? (p[2], p[1], p[0])
                : (p[0], p[1], p[2]);
        }
    }
}
