# Опыт 005: что даёт контекст и чего он не может сообщить

SYS-237, 2026-11-11. [Предрегистрация](EXPERIMENT-005-PREREG.md), [протокол](../data/experiment-005-protocol.json), [команды/границы](EXPERIMENT-005-METHOD.md), [агрегаты](results/experiment-005.json), [проверки](results/experiment-005-validation.json).

**Простой языковой контекст даёт исправления, но не позволяет отличить ошибку раскладки от намеренного заимствования при одинаковом видимом вводе.** На синтетическом holdout contextAuto восстановил240 из348 заданных ambiguous Correct и ошибочно изменил240 парных borrow Keep. Veto неоднозначности убрал ошибки и все эти исправления. Suggest сохранил текст, но получил нежелательные предложения.

Это **механистический контроль, не естественный языковой benchmark**. Конфликтующие labels при одинаковых observables построены намеренно. Невозможность идеального решения следует из конструкции ещё до запуска; это не экспериментальное доказательство бесполезности contextual AI. Script vote не обучается и не понимает смысл предложений. Его engineered recall не переносится на переписку.

## Данные и неизменность

Из прежних approved stem dictionaries выбрано95 общих canonical keys длиной2..32, из них только5 длиной>=4. Hash keygroup split:37development/58holdout. На каждой стороне ещё12 full-unambiguous контролов на язык, то есть24control keys. Ancillary full membership не используется как фильтр успеха модели. Модель/пороги/старые token methods неизменны. Нового внешнего корпуса нет.

Всего3952 строки:1228development,2724holdout. В holdout348 ambiguous Correct,348borrow Keep с точно теми же features,72control Correct и остальные Keep-сценарии (native/mixed/empty/protected/unknown). Observables не содержат gold/scenario/split/key IDs. Label file отдельный и CLI его не открывает. Development/holdout keygroups и anchor vocabulary disjoint, но словарь/модель могли знать эти targets до опыта; это не unseen-vocabulary benchmark.

Ambiguity pool означает **нормализованные stem collisions**, не гарантирует два полноценных естественных слова. Raw full-both принял14development и23holdout keygroups; остальные могут быть full-single/neither из-за case/flags и состава словаря. На holdout3long keygroups и55short. Контролы full-unambiguous (native accepted, opposite rejected); общий stem/control/full ambiguity flag в них отсутствовал. Редкие формы и аббревиатуры возможны, естественность синтетических префиксов/сочетаний не проверяется.

Полный protocol hash `265757d6d538c400c95243996d853a9e71f77f96892c34510d26beb0c3e23e8b` и исходные feature/label hashes в паспорте не менялись после генерации. До context outcomes отдельно сверены286старых token baseline probes с независимым Python scorer/XKB/direct Hunspell. Corpus/source words и per-case data только ignored build/ с полными notices/MODIFICATIONS, release_approved=false.

## Политики и результаты holdout

ContextAuto использует script двух непосредственно предыдущих слов4..32буквы; одинаковый script даёт голос, mixed/empty — отказ. Словарные obs/alt признаки поступают из проверенной offline-подготовки. Новые context targets2..32, старые token targets>=4; длины показаны отдельно. Protected→Keep, unknown→Abstain — предоставленные oracle flags, не распознавание реального поля.

GuardedContextAuto дополнительно запрещает изменение при общей dictionary ambiguity. ContextSuggest заменяет Correct на Suggest, не меняет текст. Предложения не считаются automatic recovery.

| Метод | Auto recovered ambiguous /348 | Auto recovered control /72 | False auto на всех Keep | Правильных / нежелательных предложений | Strict budget |
| --- | ---: | ---: | ---: | ---: | --- |
| alwaysKeep | 0 | 0 | 0 | 0 / 0 | fail |
| tokenTwoScore | 0 | 72 | 144 | 0 / 0 | fail |
| tokenHunspell | 6 | 72 | 180 | 0 / 0 | fail |
| contextAuto | 240 | 72 | 240 | 0 / 0 | fail |
| guardedContextAuto | 0 | 72 | 0 | 0 / 0 | fail |
| contextSuggest | 0 | 0 | 0 | 312 / 240 | fail |

Типы exact feature+gold category опубликованы рядом с occurrences. Пустой контекст, повторённый для нескольких prefix templates, дедуплицируется по types, поэтому не все numbers равны. Например full имеет180false occurrences/176types; contextAuto240/240. AmbiguousKeep — пересекающийся subset остальных Keep-категорий, его нельзя добавлять к общему false total.

144false у старых token baselines — контролы, где wrong-looking строка намеренно помещена в protected/unknown контекст. Baselines не получают эти field gates и меняют её по своим обычным правилам. Это сконструированный negative control, а не свидетельство неожиданного ухудшения словаря на естественной речи. Дополнительные36false full baseline лежат на stem-collision Keep cases (24prose,6protected,6unknown); они не противоречат результату004 на другом наборе.

## Заранее заявленные проверки

**C1 — структурные инварианты выполнены.** Token baseline actions одинаковы при всех контекстах одного target/layout. В148development и348holdout collision pairs совпадают все observable bytes и действия каждого из6методов. Во всех парах auto-recovery Correct + preservation Keep <=1; jointlySatisfied0. Детектор видит одну и ту же информацию, а намерение изменено только в отдельном label. Никакая детерминированная модель не может удовлетворить оба запроса одновременно без дополнительного сигнала или действия пользователя.

**C2 — поддержана только как engineered coverage.** ContextAuto240ambiguous recoveries против6token-full, одновременно240borrow false changes. На long>=4 группе12/18 против6/18; на short2..3 —228/330 против0/330. Short gain смешивает context feature и ослабление target guard, поэтому не приписывается только контексту. На genuine raw-full-both срезе138Correct и138borrow Keep: contextAuto меняет обе стороны138раз; среди long full-both6/6 и6false. Этот дополнительный membership-срез описательный, не новая независимая проверка гипотезы.

**C3 — veto устраняет неоднозначные изменения ценой покрытия.** GuardedContextAuto:0false и0/348ambiguous recoveries, но72/72control recoveries. Context methods не изменили ни одного protected/unknown Keep. Контрольные72 исправления доступны уже token-full; они не доказывают добавочную пользу context vote. Гейты используют заранее предоставленную разметку, не решают проблему безопасного desktop context.

**C4 — предложения отделены от действий.** ContextSuggest сделал0auto changes, предложил240ambiguous+72control правильных альтернатив и240нежелательных. Это312correct/240false proposals в специально сбалансированном наборе, а не оценка suggestion precision в реальном потоке. UX, утомление от предложений и способность пользователя распознать намерение не проверялись. Текст остаётся неизменным по определению режима, что не является доказательством правильности предложений.

**C5 — строгий budget провален всеми методами, как и должно быть при этой конструкции.** Теперь отдельно требуются zero false на prose/protected/unknown/ambiguous Keep и>=50%auto recovery ambiguous/control Correct по typed layout и pooled. Каждый ambiguous Correct имеет неразличимую borrow Keep пару. Поэтому zero false на всех borrow Keep логически вынуждает0automatic recovery всех парных Correct, и совместный screen невыполним для любого детерминированного автоисправителя. Это диагностирует несовместимые требования, а не ранжирует реальное качество моделей. Suggest не может «пройти» этот auto budget за счёт предложений.

Все development/holdout, US/RU/pooled и short/long scopes, occurrences/types, components включая insufficient опубликованы; независимый recount подтвердил120budget verdicts. Short scope без controlCorrect имеет insufficient. Никаких новых исключений/порогов по holdout не выбрано.

## Реализация и проверка

Claude написал чистый QtCore C++ helper/CLI и23 синтетических Qt cases; автор политики не читал новые features/labels/results. Codex подготовил данные, отдельно сверил baselines, выполнил сборку, независимый policy reference и list/filter recount, написал reviewer tests/отчёт. Результат не выдаётся за независимую языковую разметку людьми.

Сборка без warnings,486Qt и5новых Python reviewer tests прошли; прежние12Python tests также проходят.18negative CLI случаев проверили arguments/status/hash/rows/path/notices, invalid UTF-8/control/CRLF, membership/baseline consistency, short model guard, prefix bounds и raw row limit. На ошибках stdout пуст. Подмена labels ref на несуществующий путь с неверным hash дала побитово идентичный output: CLI действительно не использует gold file. Все3952votes и6actions совпали с независимым Python reference;5040primary counter fields и120budget verdicts сверены отдельным recount.

Изолированный main run14,1мс на этой машине, включая проверки файлов, без сети/home/соседей, project+/usr read-only. Это offline wall time, не runtime keystroke latency/RSS. Новый context CLI непосредственно линкует QtCore, не Hunspell и не загружает словари/модель; признаки для него предварительно вычислены отдельно. Нельзя выдавать его время/память за весь end-to-end детектор. Старое src/, модели/пороги/алгоритмы/отчёты не менялись; input/history/services/install/autostart не включались.

## Решение и следующий опыт

Архитектура должна уметь различать автоматическое действие, отказ и предложение. Script окружения полезен как признак, но не как разрешение всегда перевести неоднозначный токен. Неразличимое намерение требует отказа или явного выбора пользователя; усложнение модели не создаёт отсутствующую информацию. На разных, реально различимых контекстах обучаемая модель потенциально может улучшить решение —005 этого не измеряет.

SYS-238: новый natural-prefix holdout с заранее реалистичным auto budget (отказ на недоопределённости допустим), отдельными coverage/abstention и correct/false-proposal метриками. Реальные заимствования в русском окружении должны иметь исходную gold разметку, а не label из правила того же script voter. До получения новых источников — rights review и prereg; до любых заявлений о новом context model — независимая естественная оценка. Текущий синтетический набор теперь диагностический.
