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

# test_server.c Opus-encodes its synthetic audio tone frame-by-frame,
# matching AudioCapturePulse's real encoder, so the smoke test exercises
# the client's real Opus decode path -- see sendAudioStream() there.
# Parent CMakeLists.txt already found this via pkg_check_modules(OPUS ...).
add_executable(smoke_test_server test_server.c)
target_include_directories(smoke_test_server PRIVATE ${LibVNCServer_INCLUDE_DIRS} ${OPUS_INCLUDE_DIRS})
target_link_libraries(smoke_test_server PRIVATE ${LibVNCServer_LIBRARIES} ${OPUS_LIBRARIES} m)
target_link_directories(smoke_test_server PRIVATE ${LibVNCServer_LIBRARY_DIRS})

add_executable(smoke_test smoke_test.cxx)
target_link_libraries(smoke_test PRIVATE tigervnc_core Threads::Threads)

# Manual verification tool against a REAL RFB server (e.g. the
# x0vncserver from scripts/build-server.sh) -- see the file's own top comment. Not part of the
# automated smoke_test above; doesn't need libvncserver at all, but
# lives here since it needs the same TIGERVNC_CORE_BUILD_TESTS gate and
# tigervnc_core dependency.
add_executable(audio_capture_manual_test audio_capture_manual_test.cxx)
target_link_libraries(audio_capture_manual_test PRIVATE tigervnc_core Threads::Threads)
