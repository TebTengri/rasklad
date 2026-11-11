#pragma once

// Замороженный протокол эксперимента SYS-233 (data/experiment-002-protocol.json).
//
// Реализация стенда соответствует ровно одной версии протокола. Файл
// разбирается (не больше kMaxProtocolBytes) и сравнивается с ожидаемым
// JSON-документом целиком: все поля, их типы и значения, порядок массивов.
// Любое изменение файла — отказ, а не молча проигнорированное поле.
// Числа сравниваются как double (-13 и -13.0 равны).
//
// Модуль чистый: разбирает переданные байты, не открывает файлы.

#include "ngramdetector.h"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace rasklad::benchmark {

inline constexpr qint64 kMaxProtocolBytes = 64LL * 1024;

// Значения протокола, которые использует код. Совпадение с файлом
// проверяет validateProtocol.
inline constexpr char kHoldoutSeed[] = "rasklad-SYS-233-holdout-v1";
inline constexpr char kNoiseSeed[] = "rasklad-SYS-233-noise-v1";
inline constexpr int kSampleLimitPerLanguage = 5000;
inline constexpr int kShortTokenMinimum = 4;
inline constexpr int kNoisePerLayout = 256;
inline constexpr int kNoiseMinLength = 4;
inline constexpr int kNoiseMaxLength = 16;
inline constexpr int kChallengeVersion = 1;
inline constexpr double kSingleDGap = 2.5;
inline constexpr double kTwoScoreGap = 2.5;
inline constexpr double kTwoScoreMinAlternative = -13.0;
inline constexpr double kTwoScoreMaxObserved = -9.0;

static_assert(kShortTokenMinimum == kMinDecisionKeys,
              "protocol short-token minimum must equal the core decision gate");

enum class ProtocolError { None, TooLarge, InvalidJson, Mismatch };

[[nodiscard]] QString protocolErrorText(ProtocolError error);

struct ProtocolResult {
    ProtocolError error = ProtocolError::None;
    // Имя первого несовпавшего поля верхнего уровня из ожидаемого документа
    // или "unexpected field" для лишнего поля. Значения файла не копируются.
    QString mismatchedField;
    QString sha256;
};

// Ожидаемый документ протокола, к которому привязана реализация.
[[nodiscard]] QJsonObject expectedProtocol();

[[nodiscard]] ProtocolResult validateProtocol(const QByteArray &bytes);

// Глубокое сравнение JSON: объекты по множеству ключей, массивы по порядку,
// числа по значению double.
[[nodiscard]] bool jsonEqual(const QJsonValue &a, const QJsonValue &b);

// Пороги из протокола, включённые. Калибровки и подбора нет.
[[nodiscard]] SingleDThresholds frozenSingleD();
[[nodiscard]] TwoScoreThresholds frozenTwoScore();

} // namespace rasklad::benchmark
