#pragma once

// Методы и признаки эксперимента 003 (SYS-235). Девять методов протокола
// поверх признаков опыта 002 (benchmarkeval) и членства пары кандидатов в
// двух наборах Hunspell: full (исходные .aff+.dic) и control noPfxSfx (тот же
// .dic, .aff без строк PFX/SFX).
//
// Правила не дублируются: морфологические методы — это decideMethod опыта 002
// с подставленным членством Hunspell вместо членства stem-словаря:
//   hunspellNoPfxSfx/hunspellFull = ExactFull,
//   morphThenModel               = ExactFullThenTwoScore,
//   modelConfirmedMorph          = TwoScoreConfirmedByExactFull.
//
// Функции не выполняют I/O и не выводят текст.

#include "benchmarkeval.h"
#include "hunspellengine.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <array>

namespace rasklad::morphology {

enum class MorphMethod {
    AlwaysKeep,
    StemExact,
    TwoScore,
    StemThenModel,
    ModelConfirmedStem,
    HunspellNoPfxSfx,
    HunspellFull,
    MorphThenModel,
    ModelConfirmedMorph,
};
inline constexpr int kMorphMethodCount = 9;
inline constexpr std::array<MorphMethod, kMorphMethodCount> kMorphMethods{{
    MorphMethod::AlwaysKeep,
    MorphMethod::StemExact,
    MorphMethod::TwoScore,
    MorphMethod::StemThenModel,
    MorphMethod::ModelConfirmedStem,
    MorphMethod::HunspellNoPfxSfx,
    MorphMethod::HunspellFull,
    MorphMethod::MorphThenModel,
    MorphMethod::ModelConfirmedMorph,
}};

[[nodiscard]] const char *morphMethodName(MorphMethod method);

// Членство пары: observed — фактический текст в словаре языка набранной
// раскладки; alternative — convertLayout(text, correctionDirection(layout)) в
// словаре другого языка. Регистр не меняется.
struct PairMembership {
    bool observed = false;
    bool alternative = false;
};

struct MorphFeatures {
    benchmark::CaseFeatures base;
    PairMembership control;
    PairMembership full;
};

[[nodiscard]] DecisionAction decideMorph(MorphMethod method, const MorphFeatures &features);

// Общая метка неоднозначности для всех девяти методов: stem-both ИЛИ
// control-both ИЛИ full-both на фактической паре случая.
[[nodiscard]] bool stemAmbiguous(const MorphFeatures &features);
[[nodiscard]] bool controlAmbiguous(const MorphFeatures &features);
[[nodiscard]] bool fullAmbiguous(const MorphFeatures &features);
[[nodiscard]] bool sharedAmbiguous(const MorphFeatures &features);

[[nodiscard]] benchmark::Membership pairMembership(bool supported, const PairMembership &pair);

// Набор проверяющих по языкам (индекс — DetectorLanguage).
struct SpellPair {
    std::array<const SpellChecker *, 2> engines{{nullptr, nullptr}};
    [[nodiscard]] bool isValid() const { return engines[0] && engines[1]; }
};

// Прямой запрос пары без кэша. Для неподходящих случаев (!isDecisionEligible)
// словарь не запрашивается и возвращается {false, false}.
[[nodiscard]] PairMembership queryPair(const SpellPair &spell, QStringView text, SourceLayout layout,
                                       const benchmark::CaseFeatures &base);

// Тот же результат, что decideMorph на признаках из computeFeatures и
// queryPair, но живой путь без кэша: canonicalizeToken, модель, front-coded
// поиск и прямой Hunspell_spell — только то, что нужно методу.
[[nodiscard]] DecisionAction decideMorphLive(MorphMethod method, QStringView text, SourceLayout layout,
                                             const benchmark::FeatureContext &context, const SpellPair &control,
                                             const SpellPair &full);

// Ключ типа опыта 003: (язык, роль, точный текст с регистром). Отличается от
// канонического типа 002: Hunspell зависит от регистра, поэтому регистровые
// варианты не сливаются.
[[nodiscard]] QByteArray exactCaseTypeKey(DetectorLanguage language, bool wrongRole, const QString &text);

} // namespace rasklad::morphology
