cmake_minimum_required(VERSION 3.21)
project(EmbervaleCompanion VERSION 0.2.0 LANGUAGES CXX)
add_library(EmbervaleCompanion SHARED src/companion.cpp src/localization.cpp)
target_compile_features(EmbervaleCompanion PRIVATE cxx_std_17)
target_include_directories(EmbervaleCompanion PRIVATE include)
target_compile_definitions(EmbervaleCompanion PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set_target_properties(EmbervaleCompanion PROPERTIES OUTPUT_NAME "EmbervaleCompanion")
# Assets live alongside the DLL; no game installation/injection is performed.
add_custom_command(TARGET EmbervaleCompanion POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/locales" "$<TARGET_FILE_DIR:EmbervaleCompanion>/locales")
