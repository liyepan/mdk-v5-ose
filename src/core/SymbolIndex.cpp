#include "core/SymbolIndex.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QVector>

namespace {

// ---- 字符判定 ---------------------------------------------------------------
// 全部按 ASCII 判定，和原来那条 QRegularExpression 的字符类一一对应。

inline bool isIdentStart(ushort u)
{
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_';
}

inline bool isIdentChar(ushort u)
{
    return isIdentStart(u) || (u >= '0' && u <= '9');
}

inline bool isSpaceTab(ushort u)
{
    return u == ' ' || u == '\t';
}

// 函数名前面允许出现的字符：标识符字符、空格、Tab、星号
inline bool isDeclChar(ushort u)
{
    return isIdentChar(u) || u == ' ' || u == '\t' || u == '*';
}

// 后面直接跟 '(' 时不是函数的名字（if / for / sizeof ...）
bool isKeyword(const QChar* p, int len)
{
    static const char* const kws[] = {
        "if", "else", "for", "while", "switch", "case", "do", "return",
        "sizeof", "goto", "break", "continue", "defined", "asm", "__asm",
        "__attribute__", "typedef", nullptr
    };

    if (len < 2 || len > 15)
        return false;

    for (const char* const* k = kws; *k; ++k) {
        const char* w = *k;
        int i = 0;
        while (i < len && w[i] && w[i] == p[i].unicode())
            ++i;
        if (i == len && w[i] == '\0')
            return true;
    }
    return false;
}

bool startsWithWord(const QChar* d, int avail, const char* word)
{
    int i = 0;
    while (word[i]) {
        if (i >= avail || d[i].unicode() != static_cast<ushort>(word[i]))
            return false;
        ++i;
    }
    return true;
}

// ---- 解析结果 ---------------------------------------------------------------

struct ParsedSymbol
{
    QString name;
    int     line = 0;
    bool    definition = false;
};

struct FileParse
{
    qint64               mtime = 0;
    qint64               size = 0;
    QVector<ParsedSymbol> symbols;
    QStringList           includes;
};

// 进程级缓存：同一份文件不重复解析（切 target、重新打开同一个工程都用得上）
QHash<QString, FileParse>& parseCache()
{
    static QHash<QString, FileParse> cache;
    return cache;
}

QMutex& parseCacheMutex()
{
    static QMutex m;
    return m;
}

bool readFileText(const QString& path, QString& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray raw = f.readAll();
    f.close();                       // Windows 上早点放开句柄，后面 stat 才不会挡

    if (raw.isEmpty())
        return false;

    out = QString::fromUtf8(raw);
    if (out.contains(QChar(0xFFFD)))
        out = QString::fromLocal8Bit(raw);
    return true;
}

// 一行一行地扫，但不 split —— 原来 split('\n') 会为每一行单独分配一个 QString
void parseText(const QString& text, FileParse& out)
{
    const QChar* d = text.constData();
    const int    n = text.size();

    int lineNo = 1;
    int i = 0;

    while (i < n) {
        int eol = i;
        while (eol < n && d[eol] != QLatin1Char('\n'))
            ++eol;

        // ---- 行首 ----
        int q = i;
        while (q < eol && isSpaceTab(d[q].unicode()))
            ++q;

        if (q < eol) {
            const ushort c0 = d[q].unicode();

            if (c0 == '#') {
                int p = q + 1;
                while (p < eol && isSpaceTab(d[p].unicode()))
                    ++p;

                if (startsWithWord(d + p, eol - p, "define")) {
                    int s = p + 6;
                    if (s == eol || isSpaceTab(d[s].unicode()) || d[s] == QLatin1Char('(')) {
                        while (s < eol && isSpaceTab(d[s].unicode()))
                            ++s;
                        int e = s;
                        while (e < eol && isIdentChar(d[e].unicode()))
                            ++e;
                        if (e > s) {
                            ParsedSymbol sym;
                            sym.name = QString(d + s, e - s);
                            sym.line = lineNo;
                            sym.definition = true;
                            out.symbols.append(sym);
                        }
                    }
                } else if (startsWithWord(d + p, eol - p, "include")) {
                    int s = p + 7;
                    while (s < eol && isSpaceTab(d[s].unicode()))
                        ++s;
                    if (s < eol) {
                        const ushort b = d[s].unicode();
                        if (b == '"' || b == '<') {
                            const QChar close = (b == '"') ? QLatin1Char('"') : QLatin1Char('>');
                            int e = s + 1;
                            while (e < eol && d[e] != close)
                                ++e;
                            if (e < eol && e > s + 1)
                                out.includes.append(QString(d + s + 1, e - s - 1));
                        }
                    }
                }
            } else if (isIdentStart(c0) && (q - i) <= 12) {
                // 行首缩进超过 12 列基本就是语句块里的东西，不可能是声明
                int s = q;
                while (s < eol && isDeclChar(d[s].unicode()))
                    ++s;

                if (s < eol && d[s] == QLatin1Char('(')) {
                    int e = s;                                   // 名字结尾
                    while (e > q && isSpaceTab(d[e - 1].unicode()))
                        --e;
                    int b = e;                                   // 名字开头
                    while (b > q && isIdentChar(d[b - 1].unicode()))
                        --b;

                    // b == q 说明整行就一个名字 + '('，那是语句或者续行，
                    // 函数声明前面一定有返回类型/修饰符（原来那条正则也是这么要求的）
                    if (b > q && b != e && isIdentStart(d[b].unicode())
                        && !isKeyword(d + b, e - b)) {
                        bool definition = false;
                        bool declaration = false;

                        // 本行剩下的部分有 { 就是定义，有 ; 就是声明
                        int t = e;
                        while (t < eol && d[t] != QLatin1Char('{') && d[t] != QLatin1Char(';'))
                            ++t;
                        if (t < eol) {
                            if (d[t] == QLatin1Char('{'))
                                definition = true;
                            else
                                declaration = true;
                        }

                        // 都没有就往下看最多三行（Allman 风格的花括号在下一行）
                        if (!definition && !declaration) {
                            int look = eol + 1;
                            for (int k = 0; k < 3 && look < n; ++k) {
                                int nl = look;
                                while (nl < n && d[nl] != QLatin1Char('\n'))
                                    ++nl;
                                int u = look;
                                while (u < nl && d[u] != QLatin1Char('{') && d[u] != QLatin1Char(';'))
                                    ++u;
                                if (u < nl) {
                                    definition = (d[u] == QLatin1Char('{'));
                                    break;
                                }
                                look = nl + 1;
                            }
                        }

                        ParsedSymbol sym;
                        sym.name = QString(d + b, e - b);
                        sym.line = lineNo;
                        sym.definition = definition;
                        out.symbols.append(sym);
                    }
                }
            }
        }

        i = eol + 1;
        ++lineNo;
    }
}

// 缓存最多留这么多文件，防止扫过大工程以后一直占着内存
const int kMaxCacheEntries = 8000;

// 取（或补建）某个文件的解析结果；文件没变就直接用缓存。
// 返回拷贝（QVector / QStringList 是写时复制的，几乎不花钱），
// 免得外面拿着引用的时候缓存又被别人动了。
FileParse parseFile(const QString& path)
{
    const QFileInfo fi(path);
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
    const qint64 size = fi.size();

    QMutexLocker locker(&parseCacheMutex());
    QHash<QString, FileParse>& cache = parseCache();

    auto it = cache.constFind(path);
    if (it != cache.constEnd() && it->mtime == mtime && it->size == size)
        return it.value();

    FileParse entry;
    entry.mtime = mtime;
    entry.size = size;

    QString text;
    if (readFileText(path, text))
        parseText(text, entry);

    if (cache.size() >= kMaxCacheEntries)
        cache.clear();
    cache.insert(path, entry);
    return entry;
}

void clearParseCache()
{
    QMutexLocker locker(&parseCacheMutex());
    parseCache().clear();
}

// include 路径解析：候选路径的 QFileInfo::exists 结果缓存起来，
// 免得同一个头文件被几十个源文件 include 时反复去问文件系统。
class IncludeResolver
{
public:
    explicit IncludeResolver(const QStringList& includeDirs) : m_dirs(includeDirs) {}

    QString resolve(const QString& fromFile, const QString& name) const
    {
        const QString dir = QFileInfo(fromFile).absolutePath();
        const QString local = QDir::cleanPath(dir + QLatin1Char('/') + name);
        if (exists(local))
            return local;

        for (const QString& inc : m_dirs) {
            const QString cand = QDir::cleanPath(inc + QLatin1Char('/') + name);
            if (exists(cand))
                return cand;
        }
        return QString();
    }

private:
    bool exists(const QString& path) const
    {
        auto it = m_exists.find(path);
        if (it != m_exists.end())
            return it.value();
        const bool r = QFileInfo::exists(path);
        m_exists.insert(path, r);
        return r;
    }

    QStringList           m_dirs;
    mutable QHash<QString, bool> m_exists;
};

QString absoluteProjectPath(const QString& projectDir, const QString& raw)
{
    QString p = raw;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!QFileInfo(p).isAbsolute())
        p = QDir::cleanPath(projectDir + QLatin1Char('/') + p);
    return p;
}

// 最多扫这么多文件，防止某个工程的 include 路径把整个 SDK 都拖进来
const int kMaxFiles = 4000;

void buildIndex(QHash<QString, QList<SymbolLocation>>& symbols,
                QSet<QString>& indexed,
                const UvProject& project,
                int targetIndex)
{
    const Target* target = project.target(targetIndex);
    if (!target)
        return;

    const QString projectDir = project.directory();

    QStringList includeDirs;
    for (const QString& inc : splitOptionList(target->includePaths)) {
        QString p = inc;
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!QFileInfo(p).isAbsolute())
            p = QDir::cleanPath(projectDir + QLatin1Char('/') + p);
        if (!includeDirs.contains(p))
            includeDirs << p;
    }

    // 待解析的队列：工程文件打头，然后按 #include 往外扩散
    QStringList queue;
    for (const ProjGroup& group : target->groups) {
        if (group.excluded)
            continue;
        for (const ProjFile& file : group.files) {
            if (file.excluded)
                continue;
            const QString p = absoluteProjectPath(projectDir, file.path);
            if (QFileInfo::exists(p))
                queue << p;
        }
    }

    IncludeResolver resolver(includeDirs);

    for (int cursor = 0; cursor < queue.size() && indexed.size() < kMaxFiles; ++cursor) {
        const QString path = queue.at(cursor);
        if (indexed.contains(path))
            continue;
        indexed.insert(path);

        const FileParse parsed = parseFile(path);

        const int count = parsed.symbols.size();
        for (int i = 0; i < count; ++i) {
            const ParsedSymbol& sym = parsed.symbols.at(i);
            SymbolLocation loc;
            loc.file = path;
            loc.line = sym.line;
            loc.definition = sym.definition;
            symbols[sym.name].append(loc);
        }

        for (const QString& inc : parsed.includes) {
            const QString resolved = resolver.resolve(path, inc);
            if (!resolved.isEmpty() && !indexed.contains(resolved))
                queue << resolved;
        }
    }
}

} // namespace

void SymbolIndex::clear()
{
    m_symbols.clear();
    m_indexed.clear();
    m_sortedNames.clear();
    m_built = false;
}

void SymbolIndex::build(const UvProject& project, int targetIndex)
{
    clear();
    buildIndex(m_symbols, m_indexed, project, targetIndex);
    m_built = true;
}

QStringList SymbolIndex::names() const
{
    if (m_sortedNames.isEmpty() && !m_symbols.isEmpty()) {
        m_sortedNames = m_symbols.keys();
        m_sortedNames.sort(Qt::CaseInsensitive);
    }
    return m_sortedNames;
}

QList<SymbolLocation> SymbolIndex::lookup(const QString& name) const
{
    const QList<SymbolLocation> all = m_symbols.value(name);
    if (all.size() < 2)
        return all;

    // 定义排前面，同一文件同一行去重
    QList<SymbolLocation> sorted;
    sorted.reserve(all.size());
    for (const SymbolLocation& loc : all) {
        if (loc.definition)
            sorted.prepend(loc);
        else
            sorted.append(loc);
    }
    return sorted;
}

void SymbolIndex::clearCache()
{
    clearParseCache();
}
