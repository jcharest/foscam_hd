project(foscam_hd)
cmake_minimum_required(VERSION 2.8.11)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb -Wall -std=c++14")

# Global requirements
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(QT_USE_PHONON TRUE)

find_package(Qt5Widgets)
find_package(Qt5Multimedia)
find_package(Qt5MultimediaWidgets)
set(LIBS ${LIBS} pthread Qt5::Widgets Qt5::Multimedia Qt5::MultimediaWidgets)

find_package(FFmpeg REQUIRED)
include_directories(${FFMPEG_INCLUDE_DIR})
set(LIBS ${LIBS} ${FFMPEG_LIBRARIES})

file(GLOB FOSCAM_HD_SOURCE *.h *.cpp)
file(GLOB FOSCAM_HD_UI *.ui)

qt5_wrap_ui(FOSCAM_HD_UI_HDRS ${FOSCAM_HD_UI})

add_library(foscam_hd_generated_ui STATIC ${FOSCAM_HD_UI_HDRS})
add_executable(foscam_hd ${FOSCAM_HD_SOURCE})

target_link_libraries(foscam_hd foscam_hd_generated_ui ${LIBS})
