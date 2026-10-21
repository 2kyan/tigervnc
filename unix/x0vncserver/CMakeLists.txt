if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "cmake must be invoked with the top level directory")
endif()

add_executable(x0vncserver
  Geometry.cxx
  Image.cxx
  PollingManager.cxx
  PollingScheduler.cxx
  TimeMillis.cxx
  qnum_to_xorgevdev.c
  qnum_to_xorgkbd.c
  x0vncserver.cxx
  XPixelBuffer.cxx
  XDesktop.cxx
  RandrGlue.c
  XSelection.cxx
  ../vncconfig/QueryConnectDialog.cxx
)

target_include_directories(x0vncserver SYSTEM PUBLIC ${X11_INCLUDE_DIR})
target_link_libraries(x0vncserver tx rfbserver rfb network rdr unixcommon core)

# Remote audio (TigerVNC-native extension) capture backend: PulseAudio
# capture + Opus encode
if(HAVE_AUDIO_CAPTURE)
  target_sources(x0vncserver PRIVATE AudioCapturePulse.cxx)
  target_include_directories(x0vncserver SYSTEM PUBLIC ${PULSE_INCLUDE_DIRS} ${OPUS_INCLUDE_DIRS})
  target_link_libraries(x0vncserver ${PULSE_LIBRARIES} ${OPUS_LIBRARIES})
endif()

# systemd support (socket activation)
if (SYSTEMD_FOUND)
  target_include_directories(x0vncserver SYSTEM PRIVATE ${SYSTEMD_INCLUDE_DIRS})
  target_link_libraries(x0vncserver ${SYSTEMD_LIBRARIES})
endif()

if(X11_XTest_LIB)
  target_link_libraries(x0vncserver ${X11_XTest_LIB})
else()
  message(WARNING "No XTest extension.  x0vncserver will be view-only.")
endif()

if(X11_Xdamage_LIB)
  target_link_libraries(x0vncserver ${X11_Xdamage_LIB})
else()
  message(WARNING "No DAMAGE extension.  x0vncserver will have to use the slower polling method.")
endif()

if(X11_Xfixes_LIB)
  target_link_libraries(x0vncserver ${X11_Xfixes_LIB})
else()
  message(WARNING "No XFIXES extension.  x0vncserver will not be able to show cursors.")
endif()

if(X11_Xrandr_LIB)
  target_link_libraries(x0vncserver ${X11_Xrandr_LIB})
else()
  message(WARNING "No Xrandr extension.  x0vncserver will not be able to resize session.")
endif()

target_link_libraries(x0vncserver ${X11_LIBRARIES})

install(TARGETS x0vncserver DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
install(FILES x0vncserver.man DESTINATION ${CMAKE_INSTALL_FULL_MANDIR}/man1 RENAME x0vncserver.1)
