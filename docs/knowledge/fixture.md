# Fixture

Fixed native Chromium fixture; CDP observes, private VK types.

Статус: implemented_lab

## Файлы

- `tests/chromium_ime/driver.mjs`
- `tests/chromium_ime/adversarial-focus.mjs`
- `tests/wayland_ime/keys.cpp`

## Связи

- [fcitx](fcitx.md): native IME exercise
- [reference](reference.md): incoming launches isolated reference fixture
- [exact](exact.md): incoming launches exact nested fixture
- [witness](witness.md): incoming case2 interval sampling

## Проверки

- `node --test tests/wayland_ime/space_telemetry_tests.mjs`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
