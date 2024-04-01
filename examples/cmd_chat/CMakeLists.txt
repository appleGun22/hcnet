cmake_minimum_required (VERSION 3.13)

project ("chat_cmd")
set(CMAKE_CXX_STANDARD 23)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	set(CMAKE_CXX_FLAGS "/O2 /W4")
	set(CMAKE_CXX_FLAGS_DEBUG "/Zi /EHsc /fsanitize=address")
#elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")

endif()

# dependencies
if(NOT DEFINED hcnet_directory)
    message(FATAL_ERROR "You must set hcnet_directory to the location of hcnet's folder")
endif()

if(NOT DEFINED gef_directory)
    message(FATAL_ERROR "You must set gef_directory to the location of hcnet's folder")
endif()

find_package(fmt CONFIG REQUIRED)

find_package(hcnet CONFIG REQUIRED HINTS ${hcnet_directory})
find_library(miniupnpc_library NAMES miniupnpc HINTS ${CMAKE_PREFIX_PATH})
find_package(asio REQUIRED CONFIG)
find_package(tl-optional CONFIG REQUIRED)

set(execs "host;client")

foreach (exec IN ${execs})
	add_executable(${exec} src/${exec}.cpp)

	target_include_directories(${exec}
			PUBLIC "${PROJECT_SOURCE_DIR}/include"
			PUBLIC "${hcnet_directory}/include"
			PUBLIC "${gef_directory}/include")

	target_link_libraries(${exec}
			PUBLIC fmt::fmt
			PUBLIC ${miniupnpc_library}
			PUBLIC asio::asio)
endforeach()