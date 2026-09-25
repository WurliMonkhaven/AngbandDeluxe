enable_language(CXX)
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG 7f3ae3d57459e59943a4ecfefc8f6277ec6bf540)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 6d910d5487d11ca567b61c7824b0c78c569d62f0)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b)
FetchContent_MakeAvailable(SDL3 json imgui)
include("${CMAKE_CURRENT_SOURCE_DIR}/imgui_gpu_patch.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/imgui_platform_patch.cmake")
add_library(deluxe_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${platform_backend_file}"
    "${gpu_backend_file}")
target_include_directories(deluxe_imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(deluxe_imgui PUBLIC SDL3::SDL3)
add_library(deluxe_crt STATIC crt_renderer.cpp tileset.cpp)
target_include_directories(deluxe_crt PRIVATE "${sdl3_SOURCE_DIR}/src/video")
target_compile_features(deluxe_crt PUBLIC cxx_std_17)
target_link_libraries(deluxe_crt PUBLIC deluxe_imgui nlohmann_json::nlohmann_json)
add_executable(deluxe-client-tests EXCLUDE_FROM_ALL test_client.cpp)
target_compile_features(deluxe-client-tests PRIVATE cxx_std_17)
target_compile_definitions(deluxe-client-tests PRIVATE SDL_MAIN_HANDLED DELUXE_CLIENT_TEST
    DELUXE_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")
target_link_libraries(deluxe-client-tests PRIVATE deluxe_crt)
add_executable(deluxe-ui-preview EXCLUDE_FROM_ALL preview_ui.cpp)
target_compile_features(deluxe-ui-preview PRIVATE cxx_std_17)
target_compile_definitions(deluxe-ui-preview PRIVATE SDL_MAIN_HANDLED DELUXE_CLIENT_TEST
    DELUXE_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf")
target_link_libraries(deluxe-ui-preview PRIVATE deluxe_crt)
add_executable(deluxe-gpu-tests EXCLUDE_FROM_ALL test_crt_gpu.cpp)
add_executable(deluxe-transport-tests EXCLUDE_FROM_ALL test_transport.cpp)
target_compile_features(deluxe-transport-tests PRIVATE cxx_std_17)
target_compile_definitions(deluxe-transport-tests PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(deluxe-transport-tests PRIVATE deluxe_crt)
target_compile_features(deluxe-gpu-tests PRIVATE cxx_std_17)
target_compile_definitions(deluxe-gpu-tests PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(deluxe-gpu-tests PRIVATE deluxe_crt)
add_executable(AnybandUI client.cpp)
target_compile_features(AnybandUI PRIVATE cxx_std_17)
target_compile_definitions(AnybandUI PRIVATE DELUXE_DATA_DIR="${CMAKE_SOURCE_DIR}/lib"
    DELUXE_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf")
target_link_libraries(AnybandUI PRIVATE deluxe_crt)
# Stage original audio beside the executable; no source-tree path at runtime.
add_custom_target(deluxe-audio ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnybandUI>/audio"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/audio/pack.json" "$<TARGET_FILE_DIR:AnybandUI>/audio/pack.json"
)
file(GLOB DELUXE_AUDIO_SAMPLES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/audio/audition-soft-circuit/*_a.wav" "${CMAKE_CURRENT_SOURCE_DIR}/audio/audition-soft-circuit/*_b.wav")
foreach(sample IN LISTS DELUXE_AUDIO_SAMPLES)
    add_custom_command(TARGET deluxe-audio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sample}" "$<TARGET_FILE_DIR:AnybandUI>/audio/")
endforeach()
add_custom_command(TARGET deluxe-audio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/fonts" "$<TARGET_FILE_DIR:AnybandUI>/fonts")
add_dependencies(AnybandUI deluxe-audio)
add_executable(deluxe-audio-tests EXCLUDE_FROM_ALL test_audio.cpp)
target_compile_features(deluxe-audio-tests PRIVATE cxx_std_17)
target_link_libraries(deluxe-audio-tests PRIVATE SDL3::SDL3 nlohmann_json::nlohmann_json)
add_dependencies(deluxe-audio-tests deluxe-audio)
if(WIN32)
    set_target_properties(AnybandUI PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

add_executable(deluxe-window-tests EXCLUDE_FROM_ALL test_detached_windows.cpp)
target_compile_features(deluxe-window-tests PRIVATE cxx_std_17)
target_compile_definitions(deluxe-window-tests PRIVATE SDL_MAIN_HANDLED DELUXE_CLIENT_TEST
    DELUXE_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")
target_link_libraries(deluxe-window-tests PRIVATE deluxe_crt)
