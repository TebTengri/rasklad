#include "hunspellengine.h"

#include "benchmarkio.h"
#include "detectormanifest.h"
#include "morphologydata.h"

#include <hunspell/hunspell.h>
#include <hunspell/hunversion.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <dlfcn.h>

namespace rasklad::morphology {

bool isQueryableToken(QStringView token)
{
    if (token.size() < 1 || token.size() > kMaxQueryUtf16)
        return false;
    for (qsizetype i = 0; i < token.size(); ++i) {
        const QChar ch = token.at(i);
        if (ch.category() == QChar::Other_Control)
            return false;
        if (ch.isHighSurrogate()) {
            if (i + 1 >= token.size() || !token.at(i + 1).isLowSurrogate())
                return false;
            ++i;
        } else if (ch.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

QString compiledHunspellVersion()
{
    return QString::fromLatin1(HUNSPELL_VERSION_STRING);
}

QString runtimeHunspellVersion()
{
    const char *version = Hunspell_get_library_version();
    return version ? QString::fromLatin1(version) : QString();
}

QString loadedHunspellLibraryPath()
{
    Dl_info info{};
    // Адрес функции библиотеки; в PIE это определение в разделяемом объекте.
    // Если загрузчик вернёт исполняемый файл (PLT), проверка ниже откажет.
    if (dladdr(reinterpret_cast<const void *>(&Hunspell_create), &info) == 0 || !info.dli_fname
        || info.dli_fname[0] != '/')
        return {};
    return QFileInfo(QFile::decodeName(info.dli_fname)).canonicalFilePath();
}

bool verifyLoadedHunspell(const QString &expectedPath, const QString &expectedSha256, qint64 expectedBytes,
                          const QString &expectedVersion, LoadedLibrary &out, QString &error)
{
    out = LoadedLibrary();
    if (compiledHunspellVersion() != expectedVersion || runtimeHunspellVersion() != expectedVersion) {
        error = QStringLiteral("Hunspell header or library version differs from the manifest");
        return false;
    }
    if (!isSafeLibraryPath(expectedPath)) {
        error = QStringLiteral("expected Hunspell library path is not under /usr/lib");
        return false;
    }
    const QString loaded = loadedHunspellLibraryPath();
    if (loaded.isEmpty() || !loaded.startsWith(QLatin1String(kLibraryPrefix)) || loaded != expectedPath) {
        error = QStringLiteral("loaded Hunspell library is not the manifest library");
        return false;
    }
    QByteArray bytes;
    if (!benchmark::readBoundedFile(loaded, kMaxLibraryBytes, bytes, error)) {
        error = QStringLiteral("Hunspell library: ") + error;
        return false;
    }
    if (!experiment::bytesMatch(bytes, expectedBytes, expectedSha256)) {
        error = QStringLiteral("Hunspell library size or SHA-256 does not match the manifest");
        return false;
    }
    out.sha256 = experiment::sha256Hex(bytes);
    out.bytes = bytes.size();
    return true;
}

QString hunspellEngineErrorText(HunspellEngine::Error error)
{
    switch (error) {
    case HunspellEngine::Error::None:
        return QStringLiteral("ok");
    case HunspellEngine::Error::InvalidPath:
        return QStringLiteral("Hunspell aff/dic path is not an existing regular file");
    case HunspellEngine::Error::CreateFailed:
        return QStringLiteral("Hunspell_create failed");
    case HunspellEngine::Error::NotUtf8:
        return QStringLiteral("Hunspell dictionary encoding is not UTF-8");
    }
    return QStringLiteral("unknown Hunspell error");
}

std::unique_ptr<HunspellEngine> HunspellEngine::create(const QString &affPath, const QString &dicPath,
                                                       Error &error)
{
    error = Error::None;
    const QFileInfo aff(affPath);
    const QFileInfo dic(dicPath);
    if (!aff.isAbsolute() || !dic.isAbsolute() || !aff.isFile() || !dic.isFile()) {
        error = Error::InvalidPath;
        return nullptr;
    }
    const QByteArray affName = QFile::encodeName(affPath);
    const QByteArray dicName = QFile::encodeName(dicPath);
    if (affName.contains('\0') || dicName.contains('\0')) {
        error = Error::InvalidPath;
        return nullptr;
    }
    Hunhandle *handle = Hunspell_create(affName.constData(), dicName.constData());
    if (!handle) {
        error = Error::CreateFailed;
        return nullptr;
    }
    // Владение передаётся сразу: деструктор освободит дескриптор при отказе.
    std::unique_ptr<HunspellEngine> engine(new HunspellEngine(handle));
    const char *encoding = Hunspell_get_dic_encoding(handle);
    if (!encoding || qstrcmp(encoding, "UTF-8") != 0) {
        error = Error::NotUtf8;
        return nullptr;
    }
    return engine;
}

HunspellEngine::HunspellEngine(Hunhandle *handle)
    : m_handle(handle)
{
}

HunspellEngine::~HunspellEngine()
{
    if (m_handle)
        Hunspell_destroy(m_handle);
}

bool HunspellEngine::accepts(QStringView token) const
{
    if (!m_handle || !isQueryableToken(token))
        return false;
    const QByteArray utf8 = token.toUtf8();
    if (utf8.isEmpty() || utf8.contains('\0'))
        return false;
    return Hunspell_spell(m_handle, utf8.constData()) != 0;
}

} // namespace rasklad::morphology
