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
FetchContent_MakeAvailable(SDL3 json)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
add_executable(deluxe-client-tests EXCLUDE_FROM_ALL test_client.cpp)
target_compile_features(deluxe-client-tests PRIVATE cxx_std_17)
target_compile_definitions(deluxe-client-tests PRIVATE SDL_MAIN_HANDLED DELUXE_CLIENT_TEST)
target_link_libraries(deluxe-client-tests PRIVATE deluxe_imgui nlohmann_json::nlohmann_json)
add_library(deluxe_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp")
target_include_directories(deluxe_imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(deluxe_imgui PUBLIC SDL3::SDL3)
add_executable(angband-deluxe client.cpp)
target_compile_features(angband-deluxe PRIVATE cxx_std_17)
target_compile_definitions(angband-deluxe PRIVATE DELUXE_DATA_DIR="${CMAKE_SOURCE_DIR}/lib"
    DELUXE_FONT_FILE="${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf")
target_link_libraries(angband-deluxe PRIVATE deluxe_imgui nlohmann_json::nlohmann_json)
if(WIN32)
    set_target_properties(angband-deluxe PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
