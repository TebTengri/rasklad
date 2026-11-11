#pragma once

// Обёртка над установленной системной Hunspell 1.7.5 для эксперимента 003
// (SYS-235). Собирается только при наличии pkg-config hunspell; в runtime
// приложения не входит и библиотеку не распространяет.
//
// Используется только C API из <hunspell/hunspell.h>: Hunspell_create,
// Hunspell_destroy, Hunspell_spell, Hunspell_get_dic_encoding,
// Hunspell_get_library_version. Подсказки, анализ, генерация форм,
// персональные словари и поиск словарей по умолчанию не используются:
// пути .aff/.dic всегда явные и заранее проверены вызывающим кодом.
// Сообщения об ошибках не содержат путей и слов.

#include <QString>
#include <QStringView>

#include <memory>

// Непрозрачный дескриптор из hunspell.h (typedef struct Hunhandle Hunhandle).
struct Hunhandle;

namespace rasklad::morphology {

inline constexpr int kMaxQueryUtf16 = 32;

// Членство токена в словаре. Реализации не хранят и не выводят токены.
class SpellChecker
{
public:
    virtual ~SpellChecker() = default;
    [[nodiscard]] virtual bool accepts(QStringView token) const = 0;
};

// 1..32 кодовых единиц UTF-16, без NUL и иных символов категории Cc, без
// одиночных суррогатов. Только такие токены передаются в Hunspell.
[[nodiscard]] bool isQueryableToken(QStringView token);

// HUNSPELL_VERSION_STRING заголовка, с которым собран код.
[[nodiscard]] QString compiledHunspellVersion();
// Hunspell_get_library_version() загруженной библиотеки.
[[nodiscard]] QString runtimeHunspellVersion();

// Canonical-путь объекта, содержащего Hunspell_create (dladdr), или пустая
// строка. Не читает /proc и не запускает процессы.
[[nodiscard]] QString loadedHunspellLibraryPath();

struct LoadedLibrary {
    QString sha256;
    qint64 bytes = 0;
};

// Загруженная библиотека: canonical-путь (dladdr) лежит под /usr/lib/ и
// точно равен expectedPath; файл не больше 4 MiB, размер и SHA-256 равны
// ожидаемым; версия заголовка и Hunspell_get_library_version() равны
// expectedVersion.
[[nodiscard]] bool verifyLoadedHunspell(const QString &expectedPath, const QString &expectedSha256,
                                        qint64 expectedBytes, const QString &expectedVersion,
                                        LoadedLibrary &out, QString &error);

class HunspellEngine final : public SpellChecker
{
public:
    enum class Error { None, InvalidPath, CreateFailed, NotUtf8 };

    // Пути — уже проверенные canonical-пути обычных файлов. Hunspell_create
    // не сообщает об ошибках чтения; неудачная загрузка .aff проявляется в
    // кодировке по умолчанию, поэтому требуется ровно "UTF-8".
    [[nodiscard]] static std::unique_ptr<HunspellEngine> create(const QString &affPath, const QString &dicPath,
                                                                Error &error);

    ~HunspellEngine() override;
    HunspellEngine(const HunspellEngine &) = delete;
    HunspellEngine &operator=(const HunspellEngine &) = delete;
    HunspellEngine(HunspellEngine &&) = delete;
    HunspellEngine &operator=(HunspellEngine &&) = delete;

    // Hunspell_spell на UTF-8 токена в исходном регистре. Недопустимый токен
    // (isQueryableToken == false) в библиотеку не передаётся: false.
    [[nodiscard]] bool accepts(QStringView token) const override;

private:
    explicit HunspellEngine(Hunhandle *handle);
    Hunhandle *m_handle = nullptr;
};

[[nodiscard]] QString hunspellEngineErrorText(HunspellEngine::Error error);

} // namespace rasklad::morphology
