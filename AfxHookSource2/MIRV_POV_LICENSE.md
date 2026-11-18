# mirv_pov license scope

The `mirv_pov` feature implementation is licensed under the GNU Affero General
Public License version 3 only (`AGPL-3.0-only`). The license text is available
at `../LICENSE-AGPL-3.0.txt`; the provenance copy is preserved at
`../LICENSES/MulNX-AGPL-3.0.txt`. See `../LICENSE` for the repository's mixed
license scope.

This scope includes:

- source files in this directory whose names begin with `MirvPov`;
- `MirvVoicePolicy.h`, used by this fork's POV voice-selection integration;
- the separate implementation in `../pov_plugin/PovPlugin.cpp`,
  `PovNativeRuntime.cpp` / `PovNativeBindings.cpp`, its CMake build,
  and the `MirvPovCvars.h` / `MirvPovFeatures.h` definitions shared with it;
- `mirv_pov`-specific code in shared integration files, including command,
  entity, event, frame-stage, Panorama, rendering, and lifecycle integration;
- modifications and generated code needed to build and operate those parts as
  one feature.

Code in this repository that is separate from the `mirv_pov` feature remains
under its existing license, normally the AdvancedFX MIT license or the license
declared by the applicable third-party component.

The independently authored public C ABI header `../pov_plugin/PovPluginApi.h`
and generic scalar converter `../pov_plugin/PovGameEvent.h` retain their explicit
MIT licenses. Original contributions owned by CS.CHRONO contributors are also
additionally available under the limited MIT permission in
`../LICENSES/CS.CHRONO-MIT.txt`; third-party material and the combined plugin
retain the licenses stated above. This notice does not establish the license
boundary of other components connected to that ABI.

Because the distributed `AfxHookSource2.dll` combines `mirv_pov` with the rest
of AfxHookSource2 into one binary, that combined binary is conveyed under
`AGPL-3.0-only`. This does not change the original license of separable
AdvancedFX or third-party source material.
