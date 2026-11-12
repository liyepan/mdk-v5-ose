#include "core/BuildEngine.h"
#include "core/Settings.h"
#include "core/Toolchain.h"
#include "core/UvprojReader.h"
#include "ui/KeilStyle.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>

#include <cstdio>

namespace {

struct CliOptions
{
    bool    build = false;
    bool    rebuild = false;
    QString project;
    QString toolchainDir;
    QString screenshot;
    int     target = 0;
};

CliOptions parseArgs(const QStringList& args)
{
    CliOptions opt;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--build")) {
            opt.build = true;
            if (i + 1 < args.size())
                opt.project = args.at(++i);
        } else if (a == QLatin1String("--rebuild")) {
            opt.rebuild = true;
        } else if (a == QLatin1String("--toolchain")) {
            if (i + 1 < args.size())
                opt.toolchainDir = args.at(++i);
        } else if (a == QLatin1String("--screenshot")) {
            if (i + 1 < args.size())
                opt.screenshot = args.at(++i);
        } else if (a == QLatin1String("--target")) {
            if (i + 1 < args.size())
                opt.target = args.at(++i).toInt();
        } else if (!a.startsWith(QLatin1String("--")) && opt.project.isEmpty()) {
            opt.project = a;
        }
    }
    return opt;
}

int runHeadlessBuild(const CliOptions& opt, int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("linuxacme"));
    app.setApplicationName(QStringLiteral("mdk-v5-ose"));

    UvProject project;
    QString error;
    if (!Uvproj::load(opt.project, project, &error)) {
        std::fprintf(stderr, "%s\n", error.toLocal8Bit().constData());
        return 2;
    }

    QString dir = opt.toolchainDir;
    if (dir.isEmpty())
        dir = Settings::instance().toolchainDir();

    const Toolchain tc = Toolchain::detect(dir, Settings::instance().toolchainFamily());
    if (!tc.isValid()) {
        std::fprintf(stderr, "No ARM compiler configured. Use --toolchain <dir>.\n");
        return 3;
    }

    BuildEngine engine;
    engine.setToolchain(tc);
    engine.setProject(project, qBound(0, opt.target, project.targets.size() - 1));

    int exitCode = 1;

    QObject::connect(&engine, &BuildEngine::output, &app, [](const QString& line) {
        std::printf("%s\n", line.toLocal8Bit().constData());
        std::fflush(stdout);
    });
    QObject::connect(&engine, &BuildEngine::finished, &app,
                     [&exitCode](bool ok, int, int) {
                         exitCode = ok ? 0 : 1;
                         QCoreApplication::quit();
                     });

    QTimer::singleShot(0, &app, [&engine, &opt]() { engine.build(opt.rebuild); });

    app.exec();
    return exitCode;
}

} // namespace

int main(int argc, char* argv[])
{
    QStringList rawArgs;
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);

    const CliOptions opt = parseArgs(rawArgs);

    if (opt.build)
        return runHeadlessBuild(opt, argc, argv);

    // 高 DPI：必须在 QApplication 构造之前设置。
    // PassThrough 让 125% / 150% 这类非整数缩放按真实比例渲染，
    // 而不是像默认 Round 那样被凑成 1x 或 2x（这正是 uVision 在 4K 屏上糊掉的原因）。
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("linuxacme"));
    app.setApplicationName(QStringLiteral("mdk-v5-ose"));
    app.setApplicationDisplayName(QStringLiteral("uVision5-OSE"));

    KeilStyle::apply(app);

    MainWindow window;
    window.show();

    QString projectPath = opt.project;
    if (projectPath.isEmpty())
        projectPath = Settings::instance().lastProject();

    if (!projectPath.isEmpty() && QFileInfo::exists(projectPath))
        window.openProject(projectPath);

    if (!opt.screenshot.isEmpty()) {
        const QString path = opt.screenshot;
        QTimer::singleShot(400, &app, [&window, &app, path]() {
            window.grab().save(path);
            app.quit();
        });
    }

    return app.exec();
}
