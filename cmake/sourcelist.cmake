collect_project_files(sources "${CMAKE_CURRENT_SOURCE_DIR}/src" "*.cpp")

list(APPEND sources
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/Detours/detours.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/Detours/disasm.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/Detours/disolx64.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/Detours/image.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/extern/Detours/modules.cpp"
)