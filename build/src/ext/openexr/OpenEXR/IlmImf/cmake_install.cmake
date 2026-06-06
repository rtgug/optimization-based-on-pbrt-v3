# Install script for directory: D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/PBRT-V3")
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

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "D:/mingw64/bin/objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Ray Tracing Optimization/Optimization-based-on-pbrt-v3/build/src/ext/openexr/IlmBase/Imath/libImath.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("D:/Ray Tracing Optimization/Optimization-based-on-pbrt-v3/build/src/ext/openexr/IlmBase/Imath/CMakeFiles/Imath.dir/install-cxx-module-bmi-Release.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/OpenEXR" TYPE FILE FILES
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathBoxAlgo.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathBox.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathColorAlgo.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathColor.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathEuler.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathExc.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathExport.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathForward.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathFrame.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathFrustum.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathFrustumTest.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathFun.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathGL.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathGLU.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathHalfLimits.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathInt64.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathInterval.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathLimits.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathLineAlgo.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathLine.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathMath.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathMatrixAlgo.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathMatrix.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathNamespace.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathPlane.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathPlatform.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathQuat.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathRandom.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathRoots.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathShear.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathSphere.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathVecAlgo.h"
    "D:/Ray Tracing Optimization/optimization-based-on-pbrt-v3/src/ext/openexr/IlmBase/Imath/ImathVec.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/Ray Tracing Optimization/Optimization-based-on-pbrt-v3/build/src/ext/openexr/IlmBase/Imath/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
