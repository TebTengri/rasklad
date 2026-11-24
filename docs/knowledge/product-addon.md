# Product Fcitx addon

The installed addon (rasklad.so): double-Shift gesture and automatic correction in listed apps. Split from src/fcitxlab.cpp in SYS-255 with identical preprocessed code.

Статус: implemented

## Файлы

- `src/raskladproduct.cpp`
- `src/configrules.h`
- `src/wordrules.h`
- `src/manualgesture.h`
- `tools/fcitx_product.pro`
- `addon/rasklad.conf`
- `docs/SYS-246-PRODUCT-ADDON.md`

## Связи


## Проверки

- `tests/product_core/product_core.pro`
- `tests/manual_gesture/manual_gesture.pro`
- `python3 tools/wayland_ime_lab.py --run --product-suite listed --packaged`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
