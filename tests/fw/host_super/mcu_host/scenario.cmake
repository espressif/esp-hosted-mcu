# eh_scenario(<name> SRCS .. [INCLUDE_DIRS ..] [REQUIRES ..] [PRIV_REQUIRES ..] [RENAME sym ..])
# Registers an example's own sources as a component; app_main and the listed globals get a
# per-scenario name so every example links into one image unchanged. WHOLE_ARCHIVE: the
# dispatcher only holds weak references, which never pull an object out of an archive.
macro(eh_scenario name)
    cmake_parse_arguments(_sc "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES;RENAME" ${ARGN})
    list(TRANSFORM _sc_SRCS PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
    set(_sc_inc "ex")
    list(APPEND _sc_inc ${_sc_INCLUDE_DIRS})
    idf_component_register(SRCS ${_sc_SRCS}
                           PRIV_INCLUDE_DIRS ${_sc_inc}
                           REQUIRES esp_hosted ${_sc_REQUIRES}
                           PRIV_REQUIRES ${_sc_PRIV_REQUIRES}
                           WHOLE_ARCHIVE)
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        set(_sc_defs "app_main=eh_sc_${name}_main")
        foreach(_s ${_sc_RENAME})
            list(APPEND _sc_defs "${_s}=eh_sc_${name}_${_s}")
        endforeach()
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${_sc_defs})
    endif()
endmacro()
