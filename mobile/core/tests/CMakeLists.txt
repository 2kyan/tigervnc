# Host-only test tools: a minimal RFB server (test_server.c) and a
# smoke test client (smoke_test.cxx) against tigervnc_core's C ABI. Not
# built by default, and not meant to run on-device -- see
# TIGERVNC_CORE_BUILD_TESTS in the parent CMakeLists.txt.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(LibVNCServer QUIET libvncserver)
endif()

if(NOT LibVNCServer_FOUND)
  message(WARNING
    "TIGERVNC_CORE_BUILD_TESTS is on but libvncserver wasn't found "
    "(try: brew install libvncserver) -- skipping core/tests.")
  return()
endif()

add_executable(smoke_test_server test_server.c)
target_include_directories(smoke_test_server PRIVATE ${LibVNCServer_INCLUDE_DIRS})
target_link_libraries(smoke_test_server PRIVATE ${LibVNCServer_LIBRARIES})
target_link_directories(smoke_test_server PRIVATE ${LibVNCServer_LIBRARY_DIRS})

add_executable(smoke_test smoke_test.cxx)
target_link_libraries(smoke_test PRIVATE tigervnc_core Threads::Threads)
