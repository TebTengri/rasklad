# Корневой проект. Сборка только теневая, в build/: см. bin/build.
TEMPLATE = subdirs

SUBDIRS = core tests

core.file = src/rasklad_core.pro
tests.file = tests/rasklad_tests.pro
tests.depends = core
