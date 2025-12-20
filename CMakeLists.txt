cmake_minimum_required(VERSION 3.21)

project(IntegratedMagic VERSION 0.0.1 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
  SOURCES
    src/plugin.cpp
    src/MagicStrings.cpp
    src/MagicConfig.cpp
    src/SpellSettingsDB.cpp
    src/UI_IntegratedMagic.cpp
    src/MagicInput.cpp) 

target_include_directories(${PROJECT_NAME}
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23) 

target_precompile_headers(${PROJECT_NAME}
  PRIVATE
    src/PCH.h
    src/MagicStrings.h
    src/MagicConfigPath.h
    src/MagicConfig.h
    src/SpellSettingsDB.h
    src/UI_IntegratedMagic.h
    src/MagicInput.h) 

set_source_files_properties(
  src/Detours/detours.cpp
  src/Detours/disasm.cpp
  src/Detours/disolx64.cpp
  src/Detours/image.cpp
  src/Detours/modules.cpp
  PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON       
    COMPILE_DEFINITIONS "DETOURS_INTERNAL"  
)

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")

    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()
