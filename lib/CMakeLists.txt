# CMakeLists.txt for the munbox library

set(_munbox_sources
    munbox.c
    layers/hqx.c
    layers/bin.c
    layers/sit.c
    layers/sit13.c
    layers/sit15.c
    layers/cpt.c
)

if(MUNBOX_BUILD_STATIC)
    add_library(munbox_static STATIC ${_munbox_sources})
    add_library(munbox::munbox_static ALIAS munbox_static)
    set_target_properties(munbox_static PROPERTIES OUTPUT_NAME munbox POSITION_INDEPENDENT_CODE ON)
endif()

if(MUNBOX_BUILD_SHARED)
    add_library(munbox_shared SHARED ${_munbox_sources})
    add_library(munbox::munbox_shared ALIAS munbox_shared)
    set_target_properties(munbox_shared PROPERTIES OUTPUT_NAME munbox)
    if(WIN32 AND BUILD_SHARED_LIBS)
        target_compile_definitions(munbox_shared PRIVATE MUNBOX_EXPORTS)
    endif()
endif()

# Backwards compatibility alias (prefer shared if both built)
if(TARGET munbox_shared)
    add_library(munbox ALIAS munbox_shared)
elseif(TARGET munbox_static)
    add_library(munbox ALIAS munbox_static)
endif()

# 2. Specify include directories for the library.
#    This tells the compiler where to find header files.
foreach(tgt IN ITEMS munbox_static munbox_shared)
    if(TARGET ${tgt})
        target_include_directories(${tgt}
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
                $<INSTALL_INTERFACE:include>
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
        )
        target_compile_features(${tgt} PRIVATE c_std_99)
        if(MUNBOX_ENABLE_WARNINGS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes)
        endif()
    endif()
endforeach()

if(MUNBOX_INSTALL)
    include(GNUInstallDirs)
    foreach(tgt IN ITEMS munbox_static munbox_shared)
        if(TARGET ${tgt})
            install(TARGETS ${tgt}
                EXPORT munboxTargets
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            )
        endif()
    endforeach()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()


