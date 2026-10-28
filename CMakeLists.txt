cmake_minimum_required(VERSION 3.20)

project(voicemeeter_reset LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "voicemeeter_reset can only be built on Windows")
endif()

add_executable(voicemeeter_reset src/main.c)
target_compile_features(voicemeeter_reset PRIVATE c_std_11)
target_include_directories(voicemeeter_reset PRIVATE src/include)
target_link_libraries(voicemeeter_reset PRIVATE Advapi32)