cmake_minimum_required(VERSION 3.28)
project(botmod VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)
option(BOTMOD_BUILD_PLUGIN "Build the CS2 Metamod plugin" ON)
set(MMSOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/metamod-source-api17" CACHE PATH "Metamod:Source plugin API 17 checkout")
set(HL2SDK "${CMAKE_CURRENT_SOURCE_DIR}/.deps/hl2sdk-cs2" CACHE PATH "HL2SDK cs2 checkout")
set(SAFETYHOOK "${CMAKE_CURRENT_SOURCE_DIR}/.deps/safetyhook" CACHE PATH "SafetyHook checkout")
set(ZYDIS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zydis" CACHE PATH "Zydis checkout")

add_library(botmod_core STATIC src/display.cpp src/client_display.cpp src/pattern.cpp)
target_include_directories(botmod_core PUBLIC src)
target_compile_features(botmod_core PUBLIC cxx_std_17)
set_target_properties(botmod_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(BOTMOD_BUILD_PLUGIN)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "CS2 requires a 64-bit build")
    endif()
    if(WIN32 AND NOT MSVC)
        message(FATAL_ERROR "Use Visual Studio/MSVC on Windows to match the CS2 ABI")
    endif()
    foreach(header "${MMSOURCE}/core/ISmmPlugin.h" "${SAFETYHOOK}/include/safetyhook.hpp"
        "${ZYDIS}/include/Zydis/Zydis.h" "${HL2SDK}/public/eiface.h")
        if(NOT EXISTS "${header}")
            message(FATAL_ERROR "Missing ${header}. Run scripts/fetch-deps.py first.")
        endif()
    endforeach()
    # Reject cached API 18 headers before producing another incompatible binary.
    file(STRINGS "${MMSOURCE}/core/ISmmPluginExt.h" metamod_api REGEX "^#define METAMOD_PLAPI_VERSION")
    if(NOT metamod_api MATCHES "METAMOD_PLAPI_VERSION[ \t]+17[ \t]")
        message(FATAL_ERROR "Botmod requires plugin API 17 headers. Set MMSOURCE to .deps/metamod-source-api17 or use a fresh build directory.")
    endif()
    set(BUILD_SHARED_LIBS OFF)
    # Supply pinned local sources to SafetyHook's FetchContent declaration.
    set(FETCHCONTENT_SOURCE_DIR_ZYDIS "${ZYDIS}")
    add_subdirectory("${SAFETYHOOK}" "safetyhook" EXCLUDE_FROM_ALL)
    if(NOT MSVC)
        target_compile_definitions(safetyhook PUBLIC _GLIBCXX_USE_CXX11_ABI=0)
        target_compile_options(safetyhook PRIVATE -fvisibility=hidden)
    endif()
    if(WIN32)
        set(PROTOC "${HL2SDK}/devtools/bin/protoc.exe")
    else()
        set(PROTOC "${HL2SDK}/devtools/bin/linux/protoc")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/network_connection.pb.h"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/network_connection.pb.cc"
        COMMAND "${PROTOC}" --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/generated
            --proto_path=${HL2SDK}/common --proto_path=${HL2SDK}/thirdparty/protobuf-3.21.8/src
            "${HL2SDK}/common/network_connection.proto"
        DEPENDS "${HL2SDK}/common/network_connection.proto" VERBATIM)
    add_library(botmod SHARED src/plugin.cpp src/module.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/network_connection.pb.h")
    target_link_libraries(botmod PRIVATE botmod_core safetyhook::safetyhook)
    target_include_directories(botmod SYSTEM PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${MMSOURCE}/core" "${MMSOURCE}/public"
        "${MMSOURCE}/core/sourcehook"
        "${HL2SDK}/public" "${HL2SDK}/public/engine"
        "${HL2SDK}/public/tier0" "${HL2SDK}/public/tier1"
        "${HL2SDK}/public/mathlib" "${HL2SDK}/public/entity2"
        "${HL2SDK}/game/shared" "${HL2SDK}/common"
        "${HL2SDK}/thirdparty/protobuf-3.21.8/src")
    target_compile_definitions(botmod PRIVATE META_IS_SOURCE2=1 CORE_CS2
        SOURCE_ENGINE=25 SE_CS2=25)
    set_target_properties(botmod PROPERTIES PREFIX "" OUTPUT_NAME botmod)
    if(MSVC)
        target_compile_definitions(botmod PRIVATE WIN32 _WIN32 _WIN64 PLATFORM_64BITS
            COMPILER_MSVC COMPILER_MSVC64 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
        target_compile_options(botmod PRIVATE /W3 /permissive- /utf-8)
        target_link_libraries(botmod PRIVATE "${HL2SDK}/lib/public/win64/tier0.lib"
            "${HL2SDK}/lib/public/win64/interfaces.lib")
        set(BOTMOD_PLATFORM win64)
    else()
        target_compile_definitions(botmod PRIVATE POSIX LINUX _LINUX GNUC COMPILER_GCC
            PLATFORM_64BITS _GLIBCXX_USE_CXX11_ABI=0 stricmp=strcasecmp _stricmp=strcasecmp)
        target_compile_definitions(botmod_core PUBLIC _GLIBCXX_USE_CXX11_ABI=0)
        target_compile_options(botmod PRIVATE -fPIC -fno-strict-aliasing -msse -fvisibility=hidden
            -Wno-ignored-attributes)
        target_link_libraries(botmod PRIVATE "${HL2SDK}/lib/linux64/interfaces.a"
            "${HL2SDK}/lib/linux64/libtier0.so" dl)
        target_link_options(botmod PRIVATE -Wl,--no-undefined -static-libstdc++ -static-libgcc)
        set_target_properties(botmod PROPERTIES BUILD_RPATH_USE_ORIGIN ON INSTALL_RPATH "$ORIGIN/../../../../../bin/linuxsteamrt64")
        set(BOTMOD_PLATFORM linuxsteamrt64)
    endif()
    install(TARGETS botmod RUNTIME DESTINATION "addons/botmod/bin/${BOTMOD_PLATFORM}"
        LIBRARY DESTINATION "addons/botmod/bin/${BOTMOD_PLATFORM}")
    configure_file(addons/metamod/botmod.vdf.in "${CMAKE_CURRENT_BINARY_DIR}/botmod.vdf" @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/botmod.vdf" DESTINATION addons/metamod)
    install(FILES addons/botmod/gamedata.ini DESTINATION addons/botmod)
    install(FILES README.md TESTING.md LICENSE DESTINATION addons/botmod)
    install(FILES "${SAFETYHOOK}/LICENSE" DESTINATION addons/botmod/licenses RENAME SafetyHook.txt)
    install(FILES "${ZYDIS}/LICENSE" DESTINATION addons/botmod/licenses RENAME Zydis.txt)
    install(FILES "${ZYDIS}/dependencies/zycore/LICENSE" DESTINATION addons/botmod/licenses RENAME Zycore.txt)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "botmod-${PROJECT_VERSION}-${BOTMOD_PLATFORM}")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    include(CPack)
endif()

if(BUILD_TESTING)
    add_executable(botmod_tests tests/core_tests.cpp)
    target_link_libraries(botmod_tests PRIVATE botmod_core)
    add_test(NAME botmod_core COMMAND botmod_tests)
    if(BOTMOD_BUILD_PLUGIN)
        add_executable(botmod_detour_tests tests/detour_tests.cpp)
        target_link_libraries(botmod_detour_tests PRIVATE safetyhook::safetyhook)
        add_test(NAME botmod_detour COMMAND botmod_detour_tests)
    endif()
endif()
