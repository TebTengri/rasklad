#include "spellengine.h"

#include "profileverify.h"

#include <hunspell/hunspell.h>
#include <hunspell/hunversion.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <dlfcn.h>

namespace rasklad::profile {

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
    if (dladdr(reinterpret_cast<const void *>(&Hunspell_create), &info) == 0 || !info.dli_fname
        || info.dli_fname[0] != '/')
        return {};
    return QFileInfo(QFile::decodeName(info.dli_fname)).canonicalFilePath();
}

bool verifyLoadedHunspell(const QString &expectedPath, const QString &expectedSha256, qint64 expectedBytes,
                          const QString &expectedVersion)
{
    if (expectedVersion.isEmpty() || compiledHunspellVersion() != expectedVersion
        || runtimeHunspellVersion() != expectedVersion)
        return false;
    if (!expectedPath.startsWith(QLatin1String("/usr/lib/")))
        return false;
    const QString loaded = loadedHunspellLibraryPath();
    if (loaded.isEmpty() || loaded != expectedPath)
        return false;
    QByteArray bytes;
    if (!readBounded(loaded, kMaxLibraryBytes, bytes))
        return false;
    return qint64(bytes.size()) == expectedBytes && sha256Hex(bytes) == expectedSha256;
}

namespace {
// "1.7.5" -> "1.7"; "" unless it is <digits>.<digits>.<digits>.
QString minorSeries(const QString &version)
{
    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.size() != 3) return {};
    for (const QString &part : parts) {
        if (part.isEmpty() || part.size() > 4) return {};
        for (const QChar c : part)
            if (c < QLatin1Char('0') || c > QLatin1Char('9')) return {};
    }
    return parts.at(0) + QLatin1Char('.') + parts.at(1);
}
} // namespace

bool verifyCompatibleHunspell(const QString &expectedVersion)
{
    const QString series = minorSeries(expectedVersion);
    if (series.isEmpty() || minorSeries(compiledHunspellVersion()) != series
        || minorSeries(runtimeHunspellVersion()) != series)
        return false;
    const QString loaded = loadedHunspellLibraryPath();
    if (!loaded.startsWith(QLatin1String("/usr/lib/libhunspell-") + series + QLatin1String(".so.")))
        return false;
    // Not changeable by this user: not writable by us, our group or others. The
    // owner is not compared with root on purpose: inside a user namespace (bwrap,
    // the closed lab) root's files show up as the overflow user.
    const QFileInfo info(loaded);
    if (!info.isFile() || info.isSymLink() || info.isWritable())
        return false;
    return !(info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther));
}

std::unique_ptr<SpellEngine> SpellEngine::create(const QString &affPath, const QString &dicPath)
{
    const QFileInfo aff(affPath);
    const QFileInfo dic(dicPath);
    if (!aff.isAbsolute() || !dic.isAbsolute() || !aff.isFile() || !dic.isFile())
        return nullptr;
    const QByteArray affName = QFile::encodeName(affPath);
    const QByteArray dicName = QFile::encodeName(dicPath);
    if (affName.contains('\0') || dicName.contains('\0'))
        return nullptr;
    Hunhandle *handle = Hunspell_create(affName.constData(), dicName.constData());
    if (!handle)
        return nullptr;
    std::unique_ptr<SpellEngine> engine(new SpellEngine(handle));
    const char *encoding = Hunspell_get_dic_encoding(handle);
    if (!encoding || qstrcmp(encoding, "UTF-8") != 0)
        return nullptr;
    return engine;
}

SpellEngine::SpellEngine(Hunhandle *handle)
    : m_handle(handle)
{
}

SpellEngine::~SpellEngine()
{
    if (m_handle)
        Hunspell_destroy(m_handle);
}

bool SpellEngine::accepts(QStringView token) const
{
    if (!m_handle || !isQueryableToken(token))
        return false;
    const QByteArray utf8 = token.toUtf8();
    if (utf8.isEmpty() || utf8.contains('\0'))
        return false;
    return Hunspell_spell(m_handle, utf8.constData()) != 0;
}

} // namespace rasklad::profile
