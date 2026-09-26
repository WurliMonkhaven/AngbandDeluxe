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
add_library(anybandui_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${platform_backend_file}"
    "${gpu_backend_file}")
target_include_directories(anybandui_imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(anybandui_imgui PUBLIC SDL3::SDL3)
add_library(anybandui_crt STATIC crt_renderer.cpp tileset.cpp)
target_include_directories(anybandui_crt PRIVATE "${sdl3_SOURCE_DIR}/src/video")
target_compile_features(anybandui_crt PUBLIC cxx_std_17)
target_include_directories(anybandui_crt PUBLIC "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(anybandui_crt PUBLIC anybandui_imgui nlohmann_json::nlohmann_json)
add_executable(anybandui-client-tests EXCLUDE_FROM_ALL test_client.cpp)
target_compile_features(anybandui-client-tests PRIVATE cxx_std_17)
target_compile_definitions(anybandui-client-tests PRIVATE SDL_MAIN_HANDLED ANYBANDUI_CLIENT_TEST
    ANYBANDUI_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")
target_link_libraries(anybandui-client-tests PRIVATE anybandui_crt)
add_executable(anybandui-ui-preview EXCLUDE_FROM_ALL preview_ui.cpp)
target_compile_features(anybandui-ui-preview PRIVATE cxx_std_17)
target_compile_definitions(anybandui-ui-preview PRIVATE SDL_MAIN_HANDLED ANYBANDUI_CLIENT_TEST
    ANYBANDUI_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf")
target_link_libraries(anybandui-ui-preview PRIVATE anybandui_crt)
add_executable(anybandui-gpu-tests EXCLUDE_FROM_ALL test_crt_gpu.cpp)
add_executable(anybandui-transport-tests EXCLUDE_FROM_ALL test_transport.cpp)
target_compile_features(anybandui-transport-tests PRIVATE cxx_std_17)
target_compile_definitions(anybandui-transport-tests PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(anybandui-transport-tests PRIVATE anybandui_crt)
target_compile_features(anybandui-gpu-tests PRIVATE cxx_std_17)
target_compile_definitions(anybandui-gpu-tests PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(anybandui-gpu-tests PRIVATE anybandui_crt)
add_executable(AnybandUI client.cpp)
target_compile_features(AnybandUI PRIVATE cxx_std_17)
target_compile_definitions(AnybandUI PRIVATE ANYBANDUI_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/fonts/Cousine-Regular.ttf")
target_link_libraries(AnybandUI PRIVATE anybandui_crt)
# Stage original audio beside the executable; no source-tree path at runtime.
add_custom_target(anybandui-audio ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:AnybandUI>/audio"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AnybandUI>/audio"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/audio/pack.json" "$<TARGET_FILE_DIR:AnybandUI>/audio/pack.json"
)
file(GLOB ANYBANDUI_AUDIO_SAMPLES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.wav")
foreach(sample IN LISTS ANYBANDUI_AUDIO_SAMPLES)
    add_custom_command(TARGET anybandui-audio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sample}" "$<TARGET_FILE_DIR:AnybandUI>/audio/")
endforeach()
add_custom_command(TARGET anybandui-audio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/fonts" "$<TARGET_FILE_DIR:AnybandUI>/fonts")
add_dependencies(AnybandUI anybandui-audio)
add_executable(anybandui-audio-tests EXCLUDE_FROM_ALL test_audio.cpp)
target_compile_features(anybandui-audio-tests PRIVATE cxx_std_17)
target_link_libraries(anybandui-audio-tests PRIVATE SDL3::SDL3 nlohmann_json::nlohmann_json)
add_dependencies(anybandui-audio-tests anybandui-audio)
if(WIN32)
    set_target_properties(AnybandUI PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

add_executable(anybandui-window-tests EXCLUDE_FROM_ALL test_detached_windows.cpp)
target_compile_features(anybandui-window-tests PRIVATE cxx_std_17)
target_compile_definitions(anybandui-window-tests PRIVATE SDL_MAIN_HANDLED ANYBANDUI_CLIENT_TEST
    ANYBANDUI_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")
target_link_libraries(anybandui-window-tests PRIVATE anybandui_crt)
