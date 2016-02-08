# - Finds FFMPEG libraries and headers
#
# Once done this will define
#  
#  FFMPEG_FOUND			- system has FFMPEG
#  FFMPEG_INCLUDE_DIR	- the include directories
#  FFMPEG_LIBRARY_DIR	- the directory containing the libraries
#  FFMPEG_LIBRARIES		- link these to use FFMPEG
#

SET(FFMPEG_HEADERS libavformat/avformat.h libavcodec/avcodec.h libavutil/avutil.h libavfilter/avfilter.h libavdevice/avdevice.h libswscale/swscale.h)

IF(WIN32)
	SET(FFMPEG_LIBRARIES libavformat.dll.a libavcodec.dll.a libavutil.dll.a libavfilter.dll.a libavdevice.dll.a libswscale.dll.a)
	SET(FFMPEG_LIBRARY_DIR $ENV{FFMPEGDIR}\\lib CACHE PATH "Location of the AV libraries")
	SET(FFMPEG_INCLUDE_PATHS $ENV{FFMPEGDIR}\\include CACHE PATH "Location of the AV library headers")
ELSE()
	INCLUDE(FindPkgConfig)
	IF(PKG_CONFIG_FOUND)
		PKG_CHECK_MODULES(AVFORMAT libavformat)
		PKG_CHECK_MODULES(AVCODEC libavcodec)
		PKG_CHECK_MODULES(AVUTIL libavutil)
		PKG_CHECK_MODULES(AVFILTER libavfilter)
		PKG_CHECK_MODULES(AVDEVICE libavdevice)
		PKG_CHECK_MODULES(SWSCALE libswscale)
	ENDIF()

	SET(FFMPEG_LIBRARY_DIR ${AVFORMAT_LIBRARY_DIRS}
			     ${AVCODEC_LIBRARY_DIRS}
			     ${AVUTIL_LIBRARY_DIRS}
			     ${AVFILTER_LIBRARY_DIRS}
			     ${AVDEVICE_LIBRARY_DIRS}
			     ${SWSCALE_LIBRARY_DIRS})
	SET(FFMPEG_INCLUDE_PATHS ${AVFORMAT_INCLUDE_DIRS}
			     ${AVCODEC_INCLUDE_DIRS}
			     ${AVUTIL_INCLUDE_DIRS}
			     ${AVFILTER_INCLUDE_DIRS}
			     ${AVDEVICE_INCLUDE_DIRS}
			     ${SWSCALE_INCLUDE_DIRS})

	IF(NOT APPLE)
		SET(FFMPEG_LIBRARIES avformat avcodec avutil avfilter avdevice swscale)
	ELSE()
		SET(FFMPEG_LIBRARIES libavformat.a libavcodec.a libavutil.a libavfilter.a libavdevice.a libswscale.a bz2)
	ENDIF()
ENDIF()

# Find headers
SET(FFMPEG_FOUND TRUE)
SET(FFMPEG_INCLUDE_DIR ${FFMPEG_INCLUDE_PATHS})
FOREACH(HEADER ${FFMPEG_HEADERS})
	SET(HEADER_PATH NOTFOUND)
	FIND_PATH(HEADER_PATH ${HEADER} PATHS ${FFMPEG_INCLUDE_PATHS} $ENV{HOME}/progs/libavstatic/include)
	IF(HEADER_PATH)
		SET(FFMPEG_INCLUDE_DIR ${FFMPEG_INCLUDE_DIR} ${HEADER_PATH})
	ELSE()
		MESSAGE("Could not locate ${HEADER}") 
		SET(FFMPEG_FOUND FALSE)
	ENDIF()
ENDFOREACH()

# Clear out duplicates
IF(NOT "${FFMPEG_INCLUDE_DIR}" MATCHES "")
	LIST(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIR)
ENDIF()
IF(NOT "${FFMPEG_LIBRARY_DIR}" MATCHES "")
	LIST(REMOVE_DUPLICATES FFMPEG_LIBRARY_DIR)
ENDIF()

# Find the full paths of the libraries
FOREACH(LIB ${FFMPEG_LIBRARIES})
	SET(LIB_PATH NOTFOUND)
	FIND_LIBRARY(LIB_PATH NAMES ${LIB} PATHS ${FFMPEG_LIBRARY_DIR} $ENV{HOME}/progs/libavstatic/lib)
	IF(LIB_PATH)
		SET(FFMPEG_LIBRARIES_FULL ${FFMPEG_LIBRARIES_FULL} ${LIB_PATH})
	ELSE()
		MESSAGE("Could not locate library ${LIB}") 
		SET(FFMPEG_FOUND FALSE)
	ENDIF()
ENDFOREACH()
SET(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES_FULL})
IF(APPLE)
	FIND_LIBRARY(COREFOUNDATION_LIBRARY CoreFoundation)
	FIND_LIBRARY(COREVIDEO_LIBRARY CoreVideo)
	FIND_LIBRARY(VIDEODECODEACCELERATION_LIBRARY VideoDecodeAcceleration)
	LIST(APPEND FFMPEG_LIBRARIES ${COREFOUNDATION_LIBRARY} ${COREVIDEO_LIBRARY} ${VIDEODECODEACCELERATION_LIBRARY})
ENDIF()

UNSET(LIB_PATH CACHE)
UNSET(HEADER_PATH CACHE)