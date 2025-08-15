add_executable(munbox_cli main.c)
set_target_properties(munbox_cli PROPERTIES OUTPUT_NAME munbox)

# Prefer shared over static if both built
if(TARGET munbox_shared)
	target_link_libraries(munbox_cli PRIVATE munbox_shared)
elseif(TARGET munbox_static)
	target_link_libraries(munbox_cli PRIVATE munbox_static)
endif()

target_compile_features(munbox_cli PRIVATE c_std_99)
if(MUNBOX_ENABLE_WARNINGS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(munbox_cli PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes)
endif()

if(MUNBOX_INSTALL)
	install(TARGETS munbox_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
