# Evidence

Local verified Hunspell full/control, stem indices and frozen quantized n-gram evidence; single processing owner.

Статус: source_contract_or_implementation

## Файлы

- `src/localprofile.cpp`
- `src/localprofile.h`
- `src/profileverify.cpp`
- `src/ngramdetector.cpp`

## Связи

- [gui](gui.md): incoming analysis worker local provider
- [router](router.md): supplies dictionary/model evidence
- [fcitx](fcitx.md): incoming profile bridge reuses local decision core
- [licensing](licensing.md): incoming asset provenance separate from own MIT code

## Проверки

- `bin/test -silent`

Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.
