cmake_minimum_required(VERSION 3.21)

project(IntegratedMagic VERSION 1.2 LANGUAGES CXX)

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

set(BUILD_TESTS OFF CACHE BOOL "Build unit tests for CommonLibVR." FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Disable Skyrim VR in CommonLibVR-ng" FORCE)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR-ng
    ${CMAKE_BINARY_DIR}/_deps/CommonLibVR-ng-build
)

get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)

include(${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake)

add_commonlibsse_plugin(${PROJECT_NAME}
  SOURCES
    src/plugin.cpp
    src/Hooks.cpp
    src/UI/Strings.cpp
    src/UI/MENU.cpp
    src/UI/HUD.cpp
    src/UI/TextureManager.cpp
    src/UI/StyleConfig.cpp
    src/Config/Config.cpp
    src/Config/Slots.cpp
    src/Persistence/SpellSettingsDB.cpp
    src/Persistence/SaveSpellDB.cpp
    src/Input/Input.cpp
    src/Input/Assign.cpp
    src/State/State.cpp
    src/State/Action.cpp
    src/State/AnimListener.cpp
    src/Detours/detours.cpp
    src/Detours/disasm.cpp
    src/Detours/disolx64.cpp
    src/Detours/image.cpp
    src/Detours/modules.cpp
) 

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
    src/Hooks.h
    src/UI/Strings.h
    src/UI/MENU.h
    src/UI/HUD.h
    src/UI/TextureManager.h
    src/UI/StyleConfig.h
    src/UI/SlotAnimator.h
    src/Config/ConfigPath.h
    src/Config/Config.h
    src/Config/Slots.h
    src/Config/EquipSlots.h
    src/Persistence/SpellSettingsDB.h
    src/Persistence/SaveSpellDB.h
    src/Input/Assign.h
    src/Input/Input.h
    src/State/State.h
    src/State/Action.h
    src/State/AnimListener.h
    src/State/CastGuardEvents.h
) 

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