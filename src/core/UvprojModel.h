#pragma once

#include <QList>
#include <QString>
#include <QStringList>

enum class FileKind {
    CSource,
    CppSource,
    Assembly,
    Object,
    Library,
    Other
};

FileKind fileKindFromPath(const QString& path);
FileKind fileKindFromType(int fileType);
QString  fileKindName(FileKind kind);

struct ProjFile
{
    QString name;
    QString path;        // 相对 .uvprojx 所在目录（通常形如 .\Source\main.c）
    QString groupName;
    int     fileType = 0;
    bool    excluded = false;

    // 文件级覆盖选项（FileOption/FileArmAds/Cads/VariousControls）
    bool    hasOwnOptions = false;
    QString defines;
    QString includePaths;
    QString misc;

    FileKind kind() const { return fileKindFromPath(path); }
};

struct ProjGroup
{
    QString name;
    QList<ProjFile> files;
};

// 一个 Target（uVision 的 Configuration）
struct Target
{
    QString name;
    QString toolsetName;        // ARM-ADS
    QString compilerVersion;    // pCCUsed
    bool    ac6 = false;        // uAC6

    // ---- Target 页 ----
    QString device;
    QString vendor;
    QString packId;
    QString cpu;                // IRAM(...) IROM(...) CPUTYPE("Cortex-M3")

    // ---- Output 页 ----
    QString outputDirectory;    // 例如 .\Objects  (uvprojx 里以反斜杠结尾)
    QString outputName;         // Project
    QString listingPath;        // 例如 .\Listings
    bool    createExecutable = true;
    bool    createLib = false;
    bool    createHexFile = false;
    bool    debugInformation = true;
    bool    browseInformation = true;

    // ---- C/C++ 页 ----
    int     optimization = 1;       // 0..3 -> -O0..-O3
    bool    optimizeForTime = false;
    bool    c99 = true;
    bool    cpp11 = false;
    bool    oneElfSectionPerFunction = false;
    bool    splitLoadStore = false;
    bool    interwork = true;
    bool    strict = false;
    bool    enumIsInt = false;
    bool    plainChar = false;
    bool    noWarnings = false;
    int     warningLevel = 2;
    QString defines;
    QString undefines;
    QString includePaths;
    QString miscControls;

    // ---- Asm 页 ----
    QString asmDefines;
    QString asmIncludePaths;
    QString asmMisc;

    // ---- Linker 页 ----
    bool    useMemoryLayout = true;     // umfTarg
    QString scatterFile;
    QString linkerMisc;
    QString textAddressRange;           // 0x08000000
    QString dataAddressRange;           // 0x20000000
    bool    noStartLib = false;
    bool    useMicrolib = false;

    QList<ProjGroup> groups;

    QStringList allSourceFiles() const;
    QString     displayCpu() const;
};

struct UvProject
{
    QString filePath;
    QString schemaVersion;
    QList<Target> targets;
    int activeTarget = 0;

    QString directory() const;
    QString baseName() const;
    bool    isValid() const { return !targets.isEmpty(); }
    Target* target(int index);
    const Target* target(int index) const;
};

// 把 uvprojx 里用逗号/分号/空格混写的列表拆开
QStringList splitOptionList(const QString& raw);
