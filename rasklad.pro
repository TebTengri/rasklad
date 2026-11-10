# Корневой проект. Сборка только теневая, в build/: см. bin/build.
TEMPLATE = subdirs

SUBDIRS = core tests detector_tests detector_experiment

core.file = src/rasklad_core.pro
tests.file = tests/rasklad_tests.pro
tests.depends = core
# Тесты экспериментального детектора (SYS-232), собираются в build/tests/detector.
detector_tests.file = tests/detector/detector_tests.pro
detector_tests.depends = core
# Offline CLI эксперимента; не входит в runtime приложения.
detector_experiment.file = tools/detector_experiment.pro
detector_experiment.depends = core
