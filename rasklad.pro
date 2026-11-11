# Корневой проект. Сборка только теневая, в build/: см. bin/build.
TEMPLATE = subdirs

SUBDIRS = core tests detector_tests detector_experiment benchmark_tests detector_benchmark

core.file = src/rasklad_core.pro
tests.file = tests/rasklad_tests.pro
tests.depends = core
# Тесты экспериментального детектора (SYS-232), собираются в build/tests/detector.
detector_tests.file = tests/detector/detector_tests.pro
detector_tests.depends = core
# Offline CLI эксперимента; не входит в runtime приложения.
detector_experiment.file = tools/detector_experiment.pro
detector_experiment.depends = core
# Тесты helper'ов независимой оценки (SYS-233), собираются в build/tests/benchmark.
benchmark_tests.file = tests/benchmark/benchmark_tests.pro
benchmark_tests.depends = core
# Offline CLI оценки SYS-233 в том же build/tools: отдельный Makefile и
# каталог объектов, чтобы не пересекаться с detector_experiment.
detector_benchmark.file = tools/detector_benchmark.pro
detector_benchmark.makefile = Makefile.detector_benchmark
detector_benchmark.depends = core

# Эксперимент 003 (SYS-235): только если уже установлена Hunspell с
# pkg-config. Ничего не скачивается и не устанавливается; без неё цели
# пропускаются, а bin/test сообщает, что зависимость недоступна.
packagesExist(hunspell) {
    SUBDIRS += morphology_tests morphology_benchmark
    morphology_tests.file = tests/morphology/morphology_tests.pro
    morphology_tests.depends = core
    morphology_benchmark.file = tools/morphology_benchmark.pro
    morphology_benchmark.makefile = Makefile.morphology_benchmark
    morphology_benchmark.depends = core
} else {
    message("rasklad: pkg-config hunspell not found; SYS-235 morphology targets are skipped")
}
