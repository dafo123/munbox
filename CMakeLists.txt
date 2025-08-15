# ====================================================================
# munbox: Root CMakeLists.txt
# This is the main entry point for the project build.
# ====================================================================

cmake_minimum_required(VERSION 3.16) # 3.16+ for modern features like target_precompile_headers if needed

project(munbox
	VERSION 1.0.0
	DESCRIPTION "Portable library and CLI for unpacking classic Macintosh archive formats"
	HOMEPAGE_URL "https://github.com/dafo123/munbox"
	LANGUAGES C
)

# ------------------------------------------------------------------
# Options
# ------------------------------------------------------------------
option(MUNBOX_BUILD_CLI "Build the munbox command-line tool" ON)
option(MUNBOX_BUILD_SHARED "Build shared library instead of (or in addition to) static" OFF)
option(MUNBOX_BUILD_STATIC "Build static library" ON)
option(MUNBOX_ENABLE_WARNINGS "Enable a common set of strict warnings" ON)
option(MUNBOX_ENABLE_ASAN "Enable AddressSanitizer for debug builds" OFF)
option(MUNBOX_BUILD_TESTS "Enable building tests (CTest integration)" ON)
option(MUNBOX_INSTALL "Enable install rules" ON)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(MUNBOX_ENABLE_ASAN AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address)
endif()

# ------------------------------------------------------------------
# Library & CLI subdirectories
# ------------------------------------------------------------------
add_subdirectory(lib)
if(MUNBOX_BUILD_CLI)
	add_subdirectory(cmd)
endif()

# ------------------------------------------------------------------
# Tests (CTest) – integrate shell test harness as individual cases
# ------------------------------------------------------------------
if(MUNBOX_BUILD_TESTS)
	include(CTest)
	if(BUILD_TESTING)
		# Enumerate test case directories under test/testfiles
		file(GLOB _MUNBOX_TEST_DIRS RELATIVE "${PROJECT_SOURCE_DIR}/test/testfiles" "${PROJECT_SOURCE_DIR}/test/testfiles/*")
		foreach(_case IN LISTS _MUNBOX_TEST_DIRS)
			if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/test/testfiles/${_case}")
				# Detect presence of a testfile.* and md5sums.txt to qualify as a test case
				file(GLOB _inputs "${PROJECT_SOURCE_DIR}/test/testfiles/${_case}/testfile.*")
				if(_inputs AND EXISTS "${PROJECT_SOURCE_DIR}/test/testfiles/${_case}/md5sums.txt")
					add_test(NAME munbox_${_case}
						COMMAND bash "${PROJECT_SOURCE_DIR}/test/run_tests.sh" --munbox $<TARGET_FILE:munbox_cli> --test-dir "${PROJECT_SOURCE_DIR}/test/testfiles" ${_case}
					)
					set_tests_properties(munbox_${_case} PROPERTIES TIMEOUT 60)
				endif()
			endif()
		endforeach()
		# Convenience aggregate target
		add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure DEPENDS munbox_cli)
	endif()
endif()

# ------------------------------------------------------------------
# Installation & Packaging (Config package)
# ------------------------------------------------------------------
if(MUNBOX_INSTALL)
	include(CMakePackageConfigHelpers)

	set(MUNBOX_EXPORT_NAME munboxTargets)
	set(MUNBOX_NAMESPACE munbox::)

	# Generate a version file for find_package compatibility
	write_basic_package_version_file(
		"${CMAKE_CURRENT_BINARY_DIR}/munboxConfigVersion.cmake"
		VERSION ${PROJECT_VERSION}
		COMPATIBILITY SameMajorVersion
	)

	# Configure the config file
	configure_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/munboxConfig.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/munboxConfig.cmake"
		@ONLY
	)

	install(FILES
		"${CMAKE_CURRENT_BINARY_DIR}/munboxConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/munboxConfigVersion.cmake"
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/munbox
	)
	install(EXPORT ${MUNBOX_EXPORT_NAME}
		NAMESPACE ${MUNBOX_NAMESPACE}
		DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/munbox
	)

	include(CPack) # Basic packaging; can be extended later
endif()
