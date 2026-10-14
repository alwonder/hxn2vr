# hxn2vr

Форк [uHexen2](https://github.com/sezero/uhexen2) (движок Hexen II) с поддержкой 6DoF VR через OpenXR.

- Upstream: remote `upstream`, ветка `master` (https://github.com/sezero/uhexen2).
- Базовая ветка проекта: `master`. Рабочие ветки фаз, например `phase0-foundation`.
- Лицензия: GPLv2, см. `LICENSE`.

## Правила изменений

- VR-код лежит в `engine/vr` и принадлежит нам целиком.
- Правки в остальном коде движка помечаются комментарием `// HXVR` (однострочный) или `/* HXVR */` (блок), чтобы их было видно при слиянии с upstream.
- Посмотреть все наши правки в движке вне `engine/vr`:

  `git diff upstream/master -- engine ':!engine/vr'`

## Сборка

См. CMake-сборку (будет добавлена).
