cmake_minimum_required(VERSION 3.24)
project(NamToClo VERSION 2.6.7 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "NAM to CLO targets Windows x64 because HTUSBTools.dll is a Windows x64 DLL.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "A 64-bit build is required.")
endif()

add_executable(NamToClo WIN32
    src/gui.cpp
    src/conversion.cpp
    src/conversion.hpp
    src/stimulus.cpp
    src/stimulus.hpp
    src/corrective_ir.cpp
    src/corrective_ir.hpp
    src/clo_refiner.cpp
    src/clo_refiner.hpp
    src/common.cpp
    src/common.hpp
    src/resources.rc
    src/resource.h
)

target_compile_features(NamToClo PRIVATE cxx_std_20)
target_compile_definitions(NamToClo PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(NamToClo PRIVATE comdlg32 shell32 ole32)

if(MSVC)
    target_compile_options(NamToClo PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_property(TARGET NamToClo PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

install(TARGETS NamToClo RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(DIRECTORY runtime DESTINATION . FILES_MATCHING PATTERN "README.md")
