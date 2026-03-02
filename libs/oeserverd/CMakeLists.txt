#
# Install the pre-compiled oexserverd DRM module + helper libraries.
#
macro (install_libs libs)
  if (APPLE)
    #set(_oe_libdir "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}")
    set(_oe_libdir "OpenCPN.app/Contents/PlugIns/")
  elseif (WIN32)
    set(_oe_libdir "plugins")
  elseif (UNIX)
    set(_oe_libdir "lib/opencpn/")
  endif ()
  foreach(lib ${ARGV})
    install(
      FILES ${lib}
      PERMISSIONS OWNER_READ OWNER_EXECUTE OWNER_WRITE GROUP_READ GROUP_EXECUTE
                  WORLD_READ WORLD_EXECUTE
      DESTINATION ${CMAKE_INSTALL_PREFIX}/${_oe_libdir}
    )
  endforeach ()
endmacro () 

if (QT_ANDROID)
  configure_file(
   ${CMAKE_CURRENT_LIST_DIR}/oeserverd_android/arm32/oexserverd
   ${CMAKE_INSTALL_PREFIX}/lib/opencpn/oexserverd COPYONLY
  )
elseif (APPLE)
  install_libs("osx/oexserverd" "osx/libsglmac-2.38.0.0.dylib.x86_64")
elseif (UNIX AND ARCH  STREQUAL "x86_64")
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linux64/oexserverd
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linux64/libsgllnx64-2.29.02.so
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
elseif (UNIX AND ARCH STREQUAL "armhf")
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linuxarm/oexserverd
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linuxarm/libsglarmhf32-2.30.0.0.so
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
elseif (UNIX AND ARCH STREQUAL "arm64")
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linuxarm64/oexserverd
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linuxarm64/libsglarm64-2.31.0.0.so
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
elseif (UNIX)
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linux/oeserverd
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  file(COPY ${CMAKE_CURRENT_LIST_DIR}/linux/libsgllnx-2.29.0.1.so
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
elseif (WIN32)
  file(GLOB_RECURSE vcdll_files "win/*.dll")
  message(STATUS "GLOB vcdll_files: ${vcdll_files}")
  install_libs(${vcdll_files} ${CMAKE_CURRENT_LIST_DIR}/win/oexserverd.exe)
endif ()
