cmake_minimum_required(VERSION 3.24)
project(NamToClo VERSION 2.7.0.1 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "NAM to CLO targets Windows x64.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "A 64-bit build is required.")
endif()

option(NTC_ENABLE_INDEPENDENT_TRAINER "Build the independent NAM -> CLO trainer tab" ON)
set(NAM_CORE_SOURCE_DIR "" CACHE PATH "Optional local NeuralAmpModelerCore source tree (avoids network FetchContent)")

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
target_link_libraries(NamToClo PRIVATE comdlg32 shell32 ole32 dbghelp advapi32 comctl32)

if(NTC_ENABLE_INDEPENDENT_TRAINER)
    if(NAM_CORE_SOURCE_DIR)
        set(_ntc_namcore_dir "${NAM_CORE_SOURCE_DIR}")
    else()
        include(FetchContent)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Declare(
            ntc_namcore_src
            GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
            GIT_TAG v0.5.4
            GIT_SHALLOW TRUE
            GIT_SUBMODULES_RECURSE TRUE
        )
        FetchContent_GetProperties(ntc_namcore_src)
        if(NOT ntc_namcore_src_POPULATED)
            FetchContent_Populate(ntc_namcore_src)
        endif()
        set(_ntc_namcore_dir "${ntc_namcore_src_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${_ntc_namcore_dir}/NAM/get_dsp.h")
        message(FATAL_ERROR
            "Independent trainer requested but NeuralAmpModelerCore was not found. "
            "Provide -DNAM_CORE_SOURCE_DIR=<path> or allow FetchContent network access.")
    endif()

    file(GLOB_RECURSE NTC_NAMCORE_SOURCES CONFIGURE_DEPENDS
        "${_ntc_namcore_dir}/NAM/*.cpp"
    )
    add_library(ntc_namcore STATIC ${NTC_NAMCORE_SOURCES})
    target_compile_features(ntc_namcore PUBLIC cxx_std_20)
    target_include_directories(ntc_namcore PUBLIC
        "${_ntc_namcore_dir}"
        "${_ntc_namcore_dir}/Dependencies/eigen"
        "${_ntc_namcore_dir}/Dependencies/nlohmann"
        "${_ntc_namcore_dir}/Dependencies/AudioDSPTools"
    )
    target_compile_definitions(ntc_namcore PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX NAM_ENABLE_A2_FAST
    )

    target_sources(NamToClo PRIVATE
        src/independent_trainer.cpp
        src/independent_trainer.hpp
    )
    target_link_libraries(NamToClo PRIVATE ntc_namcore)
    target_compile_definitions(NamToClo PRIVATE NTC_HAS_INDEPENDENT_TRAINER=1)

    # Preserve license notices when distributing a statically linked build.
    if(EXISTS "${_ntc_namcore_dir}/LICENSE")
        install(FILES "${_ntc_namcore_dir}/LICENSE" DESTINATION . RENAME NEURALAMPMODELERCORE_LICENSE.txt)
    endif()
    if(EXISTS "${_ntc_namcore_dir}/Dependencies/nlohmann/LICENSE.MIT")
        install(FILES "${_ntc_namcore_dir}/Dependencies/nlohmann/LICENSE.MIT" DESTINATION . RENAME NLOHMANN_JSON_LICENSE.txt)
    endif()
    if(EXISTS "${_ntc_namcore_dir}/Dependencies/eigen/COPYING.MPL2")
        install(FILES "${_ntc_namcore_dir}/Dependencies/eigen/COPYING.MPL2" DESTINATION . RENAME EIGEN_LICENSE.txt)
    endif()
    if(EXISTS "${_ntc_namcore_dir}/Dependencies/AudioDSPTools/LICENSE")
        install(FILES "${_ntc_namcore_dir}/Dependencies/AudioDSPTools/LICENSE" DESTINATION . RENAME AUDIODSPTOOLS_LICENSE.txt)
    endif()
else()
    target_compile_definitions(NamToClo PRIVATE NTC_HAS_INDEPENDENT_TRAINER=0)
endif()

if(MSVC)
    target_compile_options(NamToClo PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_property(TARGET NamToClo PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(TARGET ntc_namcore)
        target_compile_options(ntc_namcore PRIVATE /W3 /permissive- /EHsc /utf-8)
        set_property(TARGET ntc_namcore PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

install(TARGETS NamToClo RUNTIME DESTINATION .)
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(DIRECTORY runtime DESTINATION . FILES_MATCHING PATTERN "README.md")
