# External dependencies

All dependencies are vendored as plain source trees (no `.git`).

## openxr-sdk

- Source: https://github.com/KhronosGroup/OpenXR-SDK
- Tag: release-1.1.63
- Commit: f2448a8797c85814aa892efc1ab8707900fbcc78
- Vendored as a plain source tree (no .git). Built as a static loader
  (DYNAMIC_LOADER OFF, BUILD_TESTS OFF, BUILD_API_LAYERS OFF), see `engine/vr/vr.cmake`.
- Removed from the upstream tree: top-level `.gitattributes`, `.github/`,
  `.git-blame-ignore-revs` (repository metadata, not needed to build).

## glm

- Source: https://github.com/g-truc/glm
- Tag: 1.0.1
- Commit: 0af55ccecd98d4e5a8d1fad7de25ba429d60e863
- Vendored: the header tree `glm/glm/` plus `copying.txt` (license) and `readme.md`.
  Header-only; tests, docs, cmake and util folders omitted.

## doctest

- Source: https://github.com/doctest/doctest
- Tag: v2.4.11
- Commit: ae7a13539fb71f270b87eb2e874fbac80bc8dda2
- Vendored: single header `doctest/doctest.h` plus `LICENSE.txt`. Used only by `vr_tests`.
