function(collect_project_files out_var base_path pattern)
    file(GLOB_RECURSE collected CONFIGURE_DEPENDS
        "${base_path}/${pattern}"
    )
    set(${out_var} ${collected} PARENT_SCOPE)
endfunction()