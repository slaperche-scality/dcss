#########################################################
# Google Test
# Adapted from http://www.kaizou.org/2014/11/gtest-cmake/
#########################################################

find_package(Threads REQUIRED)

# Download and install GoogleTest
#
# To know WHY we have to download and build it ourself, see the FAQ:
# - https://github.com/google/googletest/blob/master/googletest/docs/FAQ.md
include(ExternalProject)

ExternalProject_Add(gtest
  PREFIX          ${CMAKE_CURRENT_BINARY_DIR}/gtest
  GIT_REPOSITORY  https://github.com/google/googletest.git
  GIT_TAG         release-1.8.0
  INSTALL_COMMAND ""
)

# Get paths of the installed GoogleTest.
ExternalProject_Get_Property(gtest source_dir binary_dir)

# GoogleTest target (to be used as a dependency by our test driver).
add_library(libgtest IMPORTED STATIC GLOBAL)
add_dependencies(libgtest gtest)
set_target_properties(libgtest PROPERTIES
    "IMPORTED_LOCATION" "${binary_dir}/googlemock/gtest/libgtest.a"
    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)

# GoogleMock target (to be used as a dependency by our test driver).
add_library(libgmock IMPORTED STATIC GLOBAL)
add_dependencies(libgmock gtest)
set_target_properties(libgmock PROPERTIES
    "IMPORTED_LOCATION" "${binary_dir}/googlemock/libgmock.a"
    "IMPORTED_LINK_INTERFACE_LIBRARIES" "${CMAKE_THREAD_LIBS_INIT}"
)

# Use SYSTEM so that our strict compilers settings are not applied on this code.
include_directories(SYSTEM
    "${source_dir}/googletest/include"
    "${source_dir}/googlemock/include"
)

# Export GoogleTest include paths for the linter.
set(GTEST_INCLUDE_PATHS
  -isystem ${source_dir}/googletest/include
  -isystem ${source_dir}/googlemock/include

  PARENT_SCOPE
)

###########################################################################
# GTEST_ADD_TESTS
# From https://github.com/Kitware/CMake/blob/v3.5.0/Modules/FindGTest.cmake
###########################################################################

# With a modern CMake, we could use `GoogleTest` directly.
# By using a pre-built GoogleTest we could use `find_package(GTest)`.
#
# Since we are not in these case, we will defines the function ourself.
function(GTEST_ADD_TESTS executable extra_args)
  if(NOT ARGN)
    message(FATAL_ERROR "Missing ARGN: Read the documentation for GTEST_ADD_TESTS")
  endif()
  if(ARGN STREQUAL "AUTO")
    # obtain sources used for building that executable
    get_property(ARGN TARGET ${executable} PROPERTY SOURCES)
  endif()
  set(gtest_case_name_regex ".*\\( *([A-Za-z_0-9]+) *, *([A-Za-z_0-9]+) *\\).*")
  set(gtest_test_type_regex "(TYPED_TEST|TEST_?[FP]?)")
  foreach(source ${ARGN})
    file(READ "${source}" contents)
    string(REGEX MATCHALL "${gtest_test_type_regex} *\\(([A-Za-z_0-9 ,]+)\\)" found_tests ${contents})
    foreach(hit ${found_tests})
      string(REGEX MATCH "${gtest_test_type_regex}" test_type ${hit})

      # Parameterized tests have a different signature for the filter
      if("x${test_type}" STREQUAL "xTEST_P")
        string(REGEX REPLACE ${gtest_case_name_regex}  "*/\\1.\\2/*" test_name ${hit})
      elseif("x${test_type}" STREQUAL "xTEST_F" OR "x${test_type}" STREQUAL "xTEST")
        string(REGEX REPLACE ${gtest_case_name_regex} "\\1.\\2" test_name ${hit})
      elseif("x${test_type}" STREQUAL "xTYPED_TEST")
        string(REGEX REPLACE ${gtest_case_name_regex} "\\1/*.\\2" test_name ${hit})
      else()
        message(WARNING "Could not parse GTest ${hit} for adding to CTest.")
        continue()
      endif()
      add_test(NAME ${test_name} COMMAND ${executable} --gtest_filter=${test_name} ${extra_args})
    endforeach()
  endforeach()
endfunction()

#############
# Build tests
#############

# Source files.
set(TEST_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/dummy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp

  CACHE
  INTERNAL
  ""
  FORCE
)

set(TEST_DRIVER ${CMAKE_PROJECT_NAME}_test)
add_executable(${TEST_DRIVER}
  ${TEST_SRC}
)

target_link_libraries(${TEST_DRIVER}
  libgtest
  ${STATIC_LIB}
)

GTEST_ADD_TESTS(${TEST_DRIVER} "" ${TEST_SRC})

# Be sure to recompile the test driver if needed before running the test suite.
add_custom_target(check
  COMMAND ${CMAKE_CTEST_COMMAND}
  DEPENDS ${TEST_DRIVER}
)