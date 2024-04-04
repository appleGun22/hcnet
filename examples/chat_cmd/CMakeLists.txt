cmake_minimum_required (VERSION 3.26)

project ("chat_cmd")
set(CMAKE_CXX_STANDARD 23)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")

	if (CMAKE_BUILD_TYPE STREQUAL "Release")
		set(CMAKE_CXX_FLAGS "/O2")
	elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
		set(CMAKE_CXX_FLAGS_DEBUG "/Zi /EHsc")

		if (DEFINED USE_ASAN)
			string(APPEND CMAKE_CXX_FLAGS_DEBUG "/fsanitize=address")
		endif()
	endif()
#elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

endif()

# dependencies
if(NOT DEFINED hcnet_directory)
    message(FATAL_ERROR "You must set hcnet_directory to the location of hcnet's folder")
endif()

if(NOT DEFINED gef_directory)
    message(FATAL_ERROR "You must set gef_directory to the location of gef's folder")
endif()

find_package(fmt CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(unofficial-concurrentqueue CONFIG REQUIRED)

find_path(READERWRITERQUEUE_INCLUDE_DIRS "readerwriterqueue/atomicops.h")
find_library(miniupnpc_library NAMES miniupnpc HINTS ${CMAKE_PREFIX_PATH})

function(create_executable exec_name src_file)
	add_executable(${exec_name} ${src_file})

	target_include_directories(${exec_name} PUBLIC
		"${PROJECT_SOURCE_DIR}/include"
		"${hcnet_directory}/include"
		"${gef_directory}/include"
		${READERWRITERQUEUE_INCLUDE_DIRS}
	)

	target_link_libraries(${exec_name} PUBLIC
		fmt::fmt
		asio::asio
		unofficial::concurrentqueue::concurrentqueue
		${miniupnpc_library}
	)
endfunction()

create_executable(host src/host.cpp)
create_executable(client src/client.cpp)