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
