cmake_minimum_required(VERSION 3.15)
project(BHD_QoL_WinMM LANGUAGES CXX)

add_library(winmm SHARED src/winmm_proxy.cpp src/bhd_patches.cpp src/game_window.cpp
    src/cursor_clip.cpp src/cursor_visibility.cpp
    src/borderless_fullscreen.cpp src/borderless_gamma.cpp
    src/raw_input.cpp src/raw_input_math.cpp src/mouse_scaling_fix.cpp src/mousescale_fix.cpp src/camera_fov.cpp src/camera_fov_math.cpp
    src/high_frequency_camera.cpp src/high_frequency_camera_math.cpp
    src/camera_position.cpp src/camera_position_math.cpp
    src/high_frequency_viewmodel.cpp src/high_frequency_viewmodel_math.cpp
    src/remote_player_interpolation.cpp
    src/animation_blend_fix.cpp
    src/allow_high_resolution_fonts.cpp src/fnt_layout.cpp
    src/hud_layout.cpp src/hud_layout_math.cpp src/hud_overlays.cpp
    src/pixel_perfect_crosshair.cpp src/pixel_perfect_crosshair_math.cpp
    src/winmm.def)
target_compile_features(winmm PRIVATE cxx_std_17)

if(NOT WIN32)
    message(FATAL_ERROR "winmm.dll must be cross-compiled for Windows, for example with a MinGW toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "winmm.dll must be built for x86 (32-bit), matching dfbhd.exe.")
endif()

set_target_properties(winmm PROPERTIES
    OUTPUT_NAME "winmm"
    ARCHIVE_OUTPUT_NAME "BHD_QoL_winmm"
    PREFIX ""
)

target_compile_definitions(winmm PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(winmm PRIVATE shlwapi)

add_executable(bhd_player_config WIN32
    src/player_config.cpp
    src/crosshair_editor.cpp
    src/hud_editor.cpp
    src/crosshair_presets.cpp
    src/crosshair_tga.cpp
    src/pff_archive.cpp
    src/player_config.rc)
target_compile_features(bhd_player_config PRIVATE cxx_std_17)
target_compile_definitions(bhd_player_config PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(bhd_player_config PRIVATE comctl32 shlwapi gdiplus msimg32)
set_target_properties(bhd_player_config PROPERTIES OUTPUT_NAME "BHD Player Config")

# Multi-configuration generators place binaries in Release/Debug subfolders.
# Keep the default configuration beside the executable in every generator.
file(GENERATE
    OUTPUT "$<TARGET_FILE_DIR:bhd_player_config>/bhd_player_config.ini"
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/bhd_player_config.ini")

include(CTest)
if(BUILD_TESTING)
    add_executable(animation_blend_fix_test tests/animation_blend_fix_test.cpp)
    target_compile_features(animation_blend_fix_test PRIVATE cxx_std_17)
    target_compile_definitions(animation_blend_fix_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME animation_blend_fix COMMAND animation_blend_fix_test)

    add_executable(cursor_clip_recovery_test tests/cursor_clip_recovery_test.cpp)
    target_compile_features(cursor_clip_recovery_test PRIVATE cxx_std_17)
    target_compile_definitions(cursor_clip_recovery_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME cursor_clip_recovery COMMAND cursor_clip_recovery_test)

    add_executable(hud_overlays_test tests/hud_overlays_test.cpp src/hud_layout_math.cpp)
    target_include_directories(hud_overlays_test PRIVATE src)
    target_compile_features(hud_overlays_test PRIVATE cxx_std_17)
    target_compile_definitions(hud_overlays_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME hud_overlays COMMAND hud_overlays_test)
    foreach(mode signature rollback)
        add_test(NAME hud_overlays_${mode} COMMAND hud_overlays_test ${mode})
    endforeach()
    add_executable(mousescale_fix_test tests/mousescale_fix_test.cpp)
    target_compile_features(mousescale_fix_test PRIVATE cxx_std_17)
    target_compile_definitions(mousescale_fix_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME mousescale_fix COMMAND mousescale_fix_test)
    add_executable(fnt_layout_test tests/fnt_layout_test.cpp src/fnt_layout.cpp)
    target_include_directories(fnt_layout_test PRIVATE src)
    target_compile_features(fnt_layout_test PRIVATE cxx_std_17)
    add_test(NAME fnt_layout COMMAND fnt_layout_test)

    add_executable(allow_high_resolution_fonts_test tests/allow_high_resolution_fonts_test.cpp src/fnt_layout.cpp)
    target_include_directories(allow_high_resolution_fonts_test PRIVATE src)
    target_compile_features(allow_high_resolution_fonts_test PRIVATE cxx_std_17)
    target_compile_definitions(allow_high_resolution_fonts_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME allow_high_resolution_fonts COMMAND allow_high_resolution_fonts_test)

    add_executable(camera_position_math_test tests/camera_position_math_test.cpp src/camera_position_math.cpp)
    target_include_directories(camera_position_math_test PRIVATE src)
    target_compile_features(camera_position_math_test PRIVATE cxx_std_17)
    add_test(NAME camera_position_math COMMAND camera_position_math_test)

    add_executable(camera_position_hook_test tests/camera_position_hook_test.cpp)
    target_include_directories(camera_position_hook_test PRIVATE src)
    target_compile_features(camera_position_hook_test PRIVATE cxx_std_17)
    add_test(NAME camera_position_hook COMMAND camera_position_hook_test)

    add_executable(camera_position_backend_test tests/camera_position_backend_test.cpp src/camera_position_math.cpp)
    target_include_directories(camera_position_backend_test PRIVATE src)
    target_compile_features(camera_position_backend_test PRIVATE cxx_std_17)
    target_compile_definitions(camera_position_backend_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    # The backend test uses a synthetic module image and controlled OS state.
    add_test(NAME camera_position_backend COMMAND camera_position_backend_test)
    add_test(NAME camera_position_extrapolation COMMAND camera_position_backend_test extrapolate)
    add_test(NAME camera_position_signature COMMAND camera_position_backend_test signature)
    add_test(NAME camera_position_rollback COMMAND camera_position_backend_test rollback)
    set_tests_properties(camera_position_backend camera_position_extrapolation
        camera_position_signature camera_position_rollback PROPERTIES TIMEOUT 20)
    add_executable(winmm_proxy_test tests/winmm_proxy_test.cpp)
    target_include_directories(winmm_proxy_test PRIVATE src)
    target_compile_features(winmm_proxy_test PRIVATE cxx_std_17)
    # Use the SDK import library, rather than linking against the proxy target.
    # This exercises automatic loading via the executable's normal WinMM imports.
    if(MSVC)
        target_link_libraries(winmm_proxy_test PRIVATE winmm.lib)
    else()
        target_link_libraries(winmm_proxy_test PRIVATE -lwinmm)
    endif()
    add_dependencies(winmm_proxy_test winmm)
    add_test(NAME winmm_proxy COMMAND winmm_proxy_test "$<TARGET_FILE:winmm>")
    set_tests_properties(winmm_proxy PROPERTIES TIMEOUT 30)

    add_executable(camera_fov_math_test tests/camera_fov_math_test.cpp src/camera_fov_math.cpp)
    target_include_directories(camera_fov_math_test PRIVATE src)
    target_compile_features(camera_fov_math_test PRIVATE cxx_std_17)
    add_test(NAME camera_fov_math COMMAND camera_fov_math_test)

    add_executable(cursor_clip_math_test tests/cursor_clip_math_test.cpp)
    target_include_directories(cursor_clip_math_test PRIVATE src)
    target_compile_features(cursor_clip_math_test PRIVATE cxx_std_17)
    add_test(NAME cursor_clip_math COMMAND cursor_clip_math_test)

    add_executable(high_frequency_camera_math_test tests/high_frequency_camera_math_test.cpp
        src/high_frequency_camera_math.cpp)
    target_include_directories(high_frequency_camera_math_test PRIVATE src)
    target_compile_features(high_frequency_camera_math_test PRIVATE cxx_std_17)
    add_test(NAME high_frequency_camera_math COMMAND high_frequency_camera_math_test)

    add_executable(raw_input_math_test tests/raw_input_math_test.cpp src/raw_input_math.cpp)
    target_include_directories(raw_input_math_test PRIVATE src)
    target_compile_features(raw_input_math_test PRIVATE cxx_std_17)
    add_test(NAME raw_input_math COMMAND raw_input_math_test)

    add_executable(raw_input_packets_test tests/raw_input_packets_test.cpp)
    target_include_directories(raw_input_packets_test PRIVATE src)
    target_compile_features(raw_input_packets_test PRIVATE cxx_std_17)
    add_test(NAME raw_input_packets COMMAND raw_input_packets_test)

    add_executable(high_frequency_viewmodel_math_test
        tests/high_frequency_viewmodel_math_test.cpp src/high_frequency_viewmodel_math.cpp)
    target_include_directories(high_frequency_viewmodel_math_test PRIVATE src)
    target_compile_features(high_frequency_viewmodel_math_test PRIVATE cxx_std_17)
    add_test(NAME high_frequency_viewmodel_math COMMAND high_frequency_viewmodel_math_test)

    add_executable(high_frequency_viewmodel_test
        tests/high_frequency_viewmodel_test.cpp src/high_frequency_viewmodel_math.cpp)
    target_compile_features(high_frequency_viewmodel_test PRIVATE cxx_std_17)
    target_compile_definitions(high_frequency_viewmodel_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME high_frequency_viewmodel COMMAND high_frequency_viewmodel_test)

    add_executable(remote_player_interpolation_test tests/remote_player_interpolation_test.cpp
        src/camera_position_math.cpp src/high_frequency_viewmodel_math.cpp)
    target_compile_features(remote_player_interpolation_test PRIVATE cxx_std_17)
    target_compile_definitions(remote_player_interpolation_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME remote_player_interpolation COMMAND remote_player_interpolation_test)

    add_executable(pff_archive_test tests/pff_archive_test.cpp
        src/pff_archive.cpp src/crosshair_tga.cpp)
    target_include_directories(pff_archive_test PRIVATE src)
    target_compile_features(pff_archive_test PRIVATE cxx_std_17)
    target_compile_definitions(pff_archive_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME pff_archive COMMAND pff_archive_test)

    add_executable(crosshair_presets_test tests/crosshair_presets_test.cpp
        src/crosshair_presets.cpp src/crosshair_tga.cpp)
    target_include_directories(crosshair_presets_test PRIVATE src)
    target_compile_features(crosshair_presets_test PRIVATE cxx_std_17)
    add_test(NAME crosshair_presets COMMAND crosshair_presets_test)

    add_executable(pixel_perfect_crosshair_math_test
        tests/pixel_perfect_crosshair_math_test.cpp
        src/pixel_perfect_crosshair_math.cpp)
    target_include_directories(pixel_perfect_crosshair_math_test PRIVATE src)
    target_compile_features(pixel_perfect_crosshair_math_test PRIVATE cxx_std_17)
    add_test(NAME pixel_perfect_crosshair_math COMMAND pixel_perfect_crosshair_math_test)

    add_executable(hud_layout_math_test tests/hud_layout_math_test.cpp src/hud_layout_math.cpp)
    target_include_directories(hud_layout_math_test PRIVATE src)
    target_compile_features(hud_layout_math_test PRIVATE cxx_std_17)
    add_test(NAME hud_layout_math COMMAND hud_layout_math_test)
endif()
