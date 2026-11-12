# Вендорная спецификация input-method-unstable-v2

Единственный сторонний файл лаборатории SYS-223 — XML-описание протокола. Код реализаций (Hyprland, wlroots и др.) не копировался.

| Поле | Значение |
| --- | --- |
| Файл | `input-method-unstable-v2.xml` |
| Источник | <https://raw.githubusercontent.com/swaywm/wlroots/0855cdacb2eeeff35849e2e9c4db0aa996d78d10/protocol/input-method-unstable-v2.xml> |
| Commit | `0855cdacb2eeeff35849e2e9c4db0aa996d78d10` |
| Размер | 21354 байт |
| SHA-256 | `99414dbad9458e71aa1fa01bc45f94ca6685787bfcb4d98948f72c1b45b60703` |
| Изменения | нет; файл должен совпадать побайтно |
| Использование | только вход `wayland-scanner`; сгенерированные `*-protocol.h/.c` создаются в `build/input_lab/generated` и в Git не попадают |

`bin/test-input-lab` сверяет SHA-256 до запуска `wayland-scanner` и отказывается собирать лабораторию при несовпадении. Проверка вручную: `sha256sum tests/input_lab/protocol/input-method-unstable-v2.xml`.

Статус лицензии: MIT (Expat), разрешение из блока `<copyright>` файла. Codex независимо проверил условия, побайтное совпадение XML с закреплённым источником и полноту приведённых ниже уведомлений. Файл допущен для developer-лаборатории; сторонние реализации и словари не переносились.

## Уведомления (полностью, как в исходном файле)

```
Copyright © 2008-2011 Kristian Høgsberg
Copyright © 2010-2011 Intel Corporation
Copyright © 2012-2013 Collabora, Ltd.
Copyright © 2012, 2013 Intel Corporation
Copyright © 2015, 2016 Jan Arne Petersen
Copyright © 2017, 2018 Red Hat, Inc.
Copyright © 2018       Purism SPC

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

Сгенерированный `wayland-scanner` код является производным от этого XML и наследует те же условия; он не распространяется.
