include(FetchContent)
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG c859b25da02955fef659d658b8f324b5cde87be3)
FetchContent_GetProperties(cjson)
if(NOT cjson_POPULATED)
    FetchContent_Populate(cjson)
endif()
add_library(deluxe_json STATIC "${cjson_SOURCE_DIR}/cJSON.c")
target_include_directories(deluxe_json PUBLIC "${cjson_SOURCE_DIR}")
