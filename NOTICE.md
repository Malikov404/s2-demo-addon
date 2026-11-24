# Licensing and provenance notice

## Default license and mirv_pov exception

The repository is licensed under the AdvancedFX MIT license by default. The
`mirv_pov` feature implementation and its integration modifications are the
exception: they are licensed under GNU Affero General Public License version 3
only (`AGPL-3.0-only`). The exact source scope is defined in
`AfxHookSource2/MIRV_POV_LICENSE.md`. The root `LICENSE` explains the mixed
license scope; the full AGPL text is in `LICENSE-AGPL-3.0.txt`, with the
provenance copy preserved in `LICENSES/MulNX-AGPL-3.0.txt`.

The distributed `pov_plugin.dll` combines `mirv_pov` with its supporting code
and is conveyed under `AGPL-3.0-only`. Separate AdvancedFX and third-party
source material retains its original license and notices.

## AdvancedFX

This repository is based on AdvancedFX:

- Project: https://github.com/advancedfx/advancedfx
- Copyright: Copyright (c) 2021 advancedfx.org and its contributors
- Original license: MIT
- Preserved license: `LICENSES/AdvancedFX-MIT.txt`

The MIT-licensed AdvancedFX material remains available under its original
license. Its inclusion in this fork does not remove its original copyright or
license notices.

## MulNX_CS2

The `mirv_pov` implementation, including Radar, SoundCircle, and related POV
integration, is distributed on the conservative basis that it may contain or
be based on material from MulNX_CS2. That feature scope is therefore treated
as covered by `AGPL-3.0-only`.

- Project: https://github.com/Co1Swet/MulNX_CS2
- Copyright: Copyright (C) 2026 Co1Swet
- License option selected by this fork: GNU Affero General Public License v3.0
- License source commit: `c4d521f0cff8ec0c62a7904d00495355bf83726c`

This fork is not endorsed, sponsored, or maintained by the MulNX_CS2 author.

## WangChuDi/advancedfx

The POV implementation and the starting point for this modified fork come from
WangChuDi/advancedfx, including WangChuDi's CS2 mirv_pov implementation.

- Project: https://github.com/WangChuDi/advancedfx
- Starting commit: `b8e51e77933dfc11bab1d2a3a4fde16ab36f03b5`
- Upstream releases and source: https://github.com/WangChuDi/advancedfx/releases

That upstream release location does not supply the matching source for a
CS.CHRONO-modified binary. Preserve upstream file notices and authorship.

## Modification notice

This is a modified version of AdvancedFX. The `mirv_pov` implementation,
including its Radar, SoundCircle, HUD, voice, scoreboard, feedback, radio, and
associated integration work, was modified and reorganized by CS.CHRONO.
The standalone extraction and startup/helper-discovery changes are dated
2026-11-18. The upstream starting revision is recorded above.


## CS.CHRONO contributions and source distribution

CS.CHRONO's original contributions are additionally available under
`LICENSES/CS.CHRONO-MIT.txt`. That permission does not cover third-party material
or change the AGPL-3.0-only distribution of the combined POV plugin. The public
C API header and generic event converter retain MIT.

For every CS.CHRONO plugin binary release, provide the matching source archive,
including required dependency files, build materials and notices. The binary package's
`RELEASE.json` identifies the exact public source revision, API version and DLL
hash; `SHA256SUMS.txt` identifies both matching release archives. Build
instructions are in `README.md`; component license texts and notices are
preserved in `LICENSES/`, the SDK headers and the Detours source directory.
Project: https://github.com/Malikov404/cs.chrono-plugin

## Included dependency sources

- `deps/release/prop`: https://github.com/advancedfx/advancedfx-prop.git at `43f0981bd7a970b783b8d94f85feb6adc45fc229`.
- `deps/release/Detours`: https://github.com/advancedfx/Detours at `4b8c659f549b0ab21cf649377c7a84eb708f5e68`.

Only required source/header files are vendored. Preserve their original notices
when updating them; no submodule download is needed for a source release.
