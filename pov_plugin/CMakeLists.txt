cmake_minimum_required(VERSION 3.24)
project(PovPlugin LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "pov_plugin requires Windows x64")
endif()
add_library(pov_plugin MODULE PovPlugin.cpp PovPluginApi.h PovNativeRuntime.cpp
    PovNativeRuntime.h PovNativeBindings.cpp PovDeathPanel.cpp PovDeathFade.cpp
    ../AfxHookSource2/MirvPovCvars.h ../AfxHookSource2/MirvPovFeatures.h
    ../AfxHookSource2/MirvPovContext.cpp ../AfxHookSource2/MirvPovHud.cpp
    ../AfxHookSource2/MirvPovRadar.cpp ../AfxHookSource2/MirvPanorama.cpp
    ../AfxHookSource2/MirvPovTeamHealth.cpp ../AfxHookSource2/MirvPovTeamID.cpp
    ../AfxHookSource2/MirvPovVoice.cpp ../AfxHookSource2/MirvPovVoiceBan.cpp
    ../AfxHookSource2/MirvPovLocalization.cpp ../AfxHookSource2/MirvPovFeedback.cpp
    ../AfxHookSource2/MirvPovPawnHook.cpp
    ../AfxHookSource2/MirvPovEventReader.cpp
    ../AfxHookSource2/MirvPovScoreboard.cpp
    ../AfxHookSource2/MirvPovPickupPrompt.cpp
    ../AfxHookSource2/MirvPovBuyMenu.cpp
    ../AfxHookSource2/MirvPovSoundCircle.cpp
    ../AfxHookSource2/MirvPovRadio.cpp
    ../AfxHookSource2/MirvPovDeathPanel.cpp
    ../AfxHookSource2/MirvPovDeathCam.cpp
    ../AfxHookSource2/MirvPovMvpMusic.cpp
    ../AfxHookSource2/MirvPovKillReward.cpp
    ../AfxHookSource2/SchemaSystem.cpp ../shared/AfxConsole.cpp ../shared/binutils.cpp
    ../deps/release/Detours/src/detours.cpp ../deps/release/Detours/src/disasm.cpp
    ../deps/release/Detours/src/image.cpp ../deps/release/Detours/src/modules.cpp)
target_include_directories(pov_plugin PRIVATE ../AfxHookSource2)
target_compile_features(pov_plugin PRIVATE cxx_std_17)
target_compile_definitions(pov_plugin PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(pov_plugin PROPERTIES PREFIX "")
install(TARGETS pov_plugin LIBRARY DESTINATION bin COMPONENT pov-plugin)
install(FILES ../LICENSE ../LICENSE-AGPL-3.0.txt ../NOTICE.md DESTINATION . COMPONENT pov-plugin)
install(FILES ../AfxHookSource2/MIRV_POV_LICENSE.md DESTINATION AfxHookSource2 COMPONENT pov-plugin)
install(DIRECTORY ../LICENSES/ DESTINATION LICENSES COMPONENT pov-plugin)
install(FILES ../deps/release/Detours/LICENSE.md DESTINATION LICENSES RENAME Detours.txt COMPONENT pov-plugin)
install(FILES ../README.md ../CREDITS.md DESTINATION . COMPONENT pov-plugin)
install(FILES PovPluginApi.h PovGameEvent.h DESTINATION pov_plugin COMPONENT pov-plugin)
