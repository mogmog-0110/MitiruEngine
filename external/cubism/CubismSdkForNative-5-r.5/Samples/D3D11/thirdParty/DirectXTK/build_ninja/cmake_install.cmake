# Install script for directory: E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/DirectXTK")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/bin/CMake/DirectXTK.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk/DirectXTK-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk/DirectXTK-targets.cmake"
         "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/CMakeFiles/Export/a11a99d19d8d3c8432b0fa94ef825414/DirectXTK-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk/DirectXTK-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/directxtk/DirectXTK-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/directxtk" TYPE FILE FILES "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/CMakeFiles/Export/a11a99d19d8d3c8432b0fa94ef825414/DirectXTK-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/directxtk" TYPE FILE FILES "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/CMakeFiles/Export/a11a99d19d8d3c8432b0fa94ef825414/DirectXTK-targets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/directxtk" TYPE FILE FILES
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/BufferHelpers.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/CommonStates.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/DDSTextureLoader.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/DirectXHelpers.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/Effects.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/GeometricPrimitive.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/GraphicsMemory.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/Model.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/PostProcess.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/PrimitiveBatch.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/ScreenGrab.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/SpriteBatch.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/SpriteFont.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/VertexTypes.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/WICTextureLoader.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/GamePad.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/Keyboard.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/Mouse.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/SimpleMath.h"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/SimpleMath.inl"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/Inc/Audio.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/directxtk" TYPE FILE FILES
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/directxtk-config.cmake"
    "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/directxtk-config-version.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "E:/user/MitiruEngine/external/cubism/CubismSdkForNative-5-r.5/Samples/D3D11/thirdParty/DirectXTK/build_ninja/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
