project(foscam_hd)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -Wall -std=c++14")

# Global requirements
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Boost REQUIRED system)
include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
set(LIBS ${LIBS} ${Boost_LIBRARIES})

find_package(FFmpeg REQUIRED)
include_directories(${FFMPEG_INCLUDE_DIR})
set(LIBS ${LIBS} ${FFMPEG_LIBRARIES})

find_package(MHD REQUIRED)
include_directories(${MHD_INCLUDE_DIRS})
set(LIBS ${LIBS} ${MHD_LIBRARIES})

file(GLOB FOSCAM_HD_SOURCE *.h *.cpp)
set(LIBS ${LIBS} pthread)

add_executable(foscam_hd ${FOSCAM_HD_SOURCE})
target_link_libraries(foscam_hd ${LIBS})

add_custom_command(TARGET foscam_hd POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/favicon.ico $<TARGET_FILE_DIR:foscam_hd>)
add_custom_command(TARGET foscam_hd POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/video_player.html $<TARGET_FILE_DIR:foscam_hd>)
