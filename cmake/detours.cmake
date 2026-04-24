set_source_files_properties(
    extern/Detours/detours.cpp
    extern/Detours/disasm.cpp
    extern/Detours/disolx64.cpp
    extern/Detours/image.cpp
    extern/Detours/modules.cpp
    PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        COMPILE_DEFINITIONS "DETOURS_INTERNAL"
)