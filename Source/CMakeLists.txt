cmake_minimum_required (VERSION 2.6)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)



#add_subdirectory(/home/mnoon/LIB3MF/Examples/Cpp/Source/rapidxml)
#add_library(rapidxml /home/mnoon/LIB3MF/Examples/Cpp/Source/rapidxml/RapidXMLSTD.cpp /home/mnoon/LIB3MF/Examples/Cpp/Source/rapidxml/RapidXMLSTD.hpp)
#target_include_directories(rapidxml INTERFACE /home/mnoon/LIB3MF/Examples/Cpp/Source/rapidxml/)


if (WIN32)
	set(LSUFFIX "dll")
	set(LSUFFIXOUT ".dll")
elseif(UNIX AND NOT APPLE)
	set(LSUFFIX "so")
	set(LSUFFIXOUT ".so.2")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0")
else()
	set(LSUFFIX "dylib")
	set(LSUFFIXOUT ".2.dylib")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0")
endif()

add_definitions( -DTEXTURESPATH="${CMAKE_CURRENT_SOURCE_DIR}/../Files/Textures/" )

project(3MF_Scripts)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Bindings/Cpp)
include_directories(Inipp)
add_subdirectory(OpenXLSX)

if (WIN32)
	link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../Lib)
	link_libraries(lib3mf)
else()
	# Unix prefixes the name of the library with "lib" anyway
	link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Bin)
	link_libraries(3mf nlohmann_json::nlohmann_json OpenXLSX::OpenXLSX)
	link_directories(${CMAKE_CURRENT_BINARY_DIR})
	#link_directories(/home/mnoon/LIB3MF/Examples/Cpp/Source/rapidxml)
	
endif()



function(CopySharedLibrary MyTarget)
	add_custom_command(TARGET ${MyTarget} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../Bin/lib3mf.${LSUFFIX}" $<TARGET_FILE_DIR:${MyTarget}>/lib3mf${LSUFFIXOUT})
endfunction(CopySharedLibrary)

add_executable(mat-assign Source/MatAssign.cpp)
CopySharedLibrary(mat-assign)

add_executable(prusa-helper Source/PrusaHelper.cpp)
CopySharedLibrary(prusa-helper)
#target_link_libraries(PrusaHelper PUBLIC rapidxml)


if (${MSVC})
	IF(${CMAKE_VERSION} VERSION_LESS 3.6.3)
		MESSAGE ("Note: You need to manually select a StartUp-project in Visual Studio.")
	ELSE()
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Example_Cube)
	ENDIF()
endif()
