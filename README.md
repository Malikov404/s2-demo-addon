# CS.CHRONO POV plugin

Optional Windows x64 DLL for first-person Counter-Strike 2 demo playback, based
on [WangChuDi/advancedfx](https://github.com/WangChuDi/advancedfx) and
[AdvancedFX](https://github.com/advancedfx/advancedfx), with CS.CHRONO adaptations.

Includes POV HUD/radar, player voices, radio, damage/death feedback, rewards,
MVP music, pickup prompts and sound circles. Scoreboard and buy-menu replay are
optional. For offline demos only; do not join VAC-protected servers with HLAE.

## Build

Requires Visual Studio 2022 with Desktop development with C++, a Windows SDK,
and CMake 3.24+. Required dependency source files are included in this tree.
Run from the repository root:

```powershell
cmake -S pov_plugin -B build/pov-plugin -G "Visual Studio 17 2022" -A x64
cmake --build build/pov-plugin --config Release
cmake --install build/pov-plugin --config Release --component pov-plugin --prefix build/package
```

DLL output: `build/pov-plugin/Release/pov_plugin.dll`. Source hashes, dependency
revisions and build tool versions are recorded in `SOURCE-MANIFEST.json`.

## Use

Requires a compatible HLAE host implementing [POV Plugin API v5](pov_plugin/PovPluginApi.h).
The host supplies native-context/entity/cvar queries and frame/level/event
callbacks. The plugin does not work with an unmodified HLAE host. Inject the
host DLL and `pov_plugin.dll` through HLAE's repeated `-hookDllPath` arguments;
close CS2 before replacing either DLL.

```text
mirv_pov_plugin 1
mirv_pov_plugin status
mirv_pov_plugin 0
```

The main switch defaults to off. Optional replay: `mirv_pov_plugin setting
scoreboard 1` or `mirv_pov_plugin setting buymenu 1`. Independent effects use
`mirv_pov_debug_feature <name> 0|1`; names/defaults are in
[MirvPovFeatures.h](AfxHookSource2/MirvPovFeatures.h). Game updates may affect
individual native mechanisms; check the selected effects in a local demo.

## License

The plugin is distributed under **AGPL-3.0-only**. Original CS.CHRONO contributions
have the additional [MIT permission](LICENSES/CS.CHRONO-MIT.txt); the public API
header and generic event converter are MIT. This does not relicense third-party
code or the combined plugin under MIT. Preserve [LICENSE](LICENSE),
[NOTICE.md](NOTICE.md), the license texts and original source copyright notices.
Attribution is also retained in [CREDITS.md](CREDITS.md).
