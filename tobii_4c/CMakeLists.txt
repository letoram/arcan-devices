PROJECT( drv_eyetrack )
cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)

include(FindPkgConfig)
pkg_search_module(ARCAN_SHMIF arcan-shmif)

add_definitions(
	-Wall
	-D__UNIX
	-DPOSIX_C_SOURCE
	-DGNU_SOURCE
	-Wno-unused-variable
	-std=gnu11 # shmif-api requires this
)

include_directories(${ARCAN_SHMIF_INCLUDE_DIRS} /tobii/include)

SET(LIBRARIES
	tobii_stream_engine
	#	rt
	pthread
	m
	${ARCAN_SHMIF_LIBRARIES}
)

SET(SOURCES
	${PROJECT_NAME}.c
)

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})
