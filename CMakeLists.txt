cmake_minimum_required(VERSION 3.24)

project(NamToCloResearch VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "NamToCloResearch currently targets Windows x64 because HTUSBTools.dll is a Windows x64 DLL.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "A 64-bit build is required.")
endif()

add_executable(NamToClo
    src/main.cpp
    src/common.cpp
    src/common.hpp
)

target_compile_features(NamToClo PRIVATE cxx_std_20)
target_compile_definitions(NamToClo PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if(MSVC)
    target_compile_options(NamToClo PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_property(TARGET NamToClo PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(NamToClo PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS NamToClo RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(DIRECTORY docs DESTINATION .)
install(DIRECTORY scripts DESTINATION .)
install(DIRECTORY runtime DESTINATION . FILES_MATCHING PATTERN "README.md")
install(DIRECTORY tools DESTINATION . FILES_MATCHING PATTERN "*.py")
