project(foscam_hd)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -Wall -std=c++14")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -pg -Wall -std=c++14")

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

add_subdirectory(contrib/ffmpeg_wrapper)
include_directories(contrib/ffmpeg_wrapper)
set(LIBS ${LIBS} ffmpeg_wrapper)

file(GLOB FOSCAM_HD_SOURCE *.h *.cpp)
list(REMOVE_ITEM FOSCAM_HD_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/test_sdk.cpp)
set(LIBS ${LIBS} pthread)

add_executable(foscam_hd ${FOSCAM_HD_SOURCE})
target_link_libraries(foscam_hd ${LIBS})

include_directories(${CMAKE_SOURCE_DIR}/sdk/include)
link_directories(${CMAKE_SOURCE_DIR}/sdk/libs/linux)
add_executable(test_sdk test_sdk.cpp)
target_link_libraries(test_sdk FosSdk)
set_target_properties(test_sdk PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")


add_custom_command(TARGET foscam_hd POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/favicon.ico $<TARGET_FILE_DIR:foscam_hd>)
add_custom_command(TARGET foscam_hd POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/video_player.html $<TARGET_FILE_DIR:foscam_hd>)
