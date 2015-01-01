project (COMIO C)

cmake_minimum_required(VERSION 2.8.11)

INCLUDE(../../build/SynchronetMacros.cmake)

set(SOURCE
	comio.c
)
if(WIN32)
	list(APPEND SOURCE comio_win32.c)
else()
	list(APPEND SOURCE comio_nix.c)
endif()
add_library(comio STATIC ${SOURCE})
set_target_properties(comio PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

double_require_lib(comio xpdev)

set(INSTALL_HEADERS
	comio.h
)

export(PACKAGE COMIO)

install(TARGETS comio DESTINATION lib EXPORT COMIOConfig)
install(FILES ${INSTALL_HEADERS} DESTINATION include)
install(EXPORT COMIOConfig DESTINATION lib/cmake/COMIO)
