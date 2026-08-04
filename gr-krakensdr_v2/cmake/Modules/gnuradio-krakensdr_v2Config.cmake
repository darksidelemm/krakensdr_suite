find_package(PkgConfig)

PKG_CHECK_MODULES(PC_GR_KRAKENSDR_V2 gnuradio-krakensdr_v2)

FIND_PATH(
    GR_KRAKENSDR_V2_INCLUDE_DIRS
    NAMES gnuradio/krakensdr_v2/api.h
    HINTS $ENV{KRAKENSDR_V2_DIR}/include
        ${PC_KRAKENSDR_V2_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GR_KRAKENSDR_V2_LIBRARIES
    NAMES gnuradio-krakensdr_v2
    HINTS $ENV{KRAKENSDR_V2_DIR}/lib
        ${PC_KRAKENSDR_V2_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-krakensdr_v2Target.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GR_KRAKENSDR_V2 DEFAULT_MSG GR_KRAKENSDR_V2_LIBRARIES GR_KRAKENSDR_V2_INCLUDE_DIRS)
MARK_AS_ADVANCED(GR_KRAKENSDR_V2_LIBRARIES GR_KRAKENSDR_V2_INCLUDE_DIRS)
