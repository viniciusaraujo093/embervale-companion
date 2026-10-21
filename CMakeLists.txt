cmake_minimum_required(VERSION 3.21)
project(EmbervaleCompanion VERSION 0.6.0 LANGUAGES CXX)
if(WIN32)
add_executable(EmbervaleCompanionVulkanProbe src/vulkan_probe.cpp)
target_compile_features(EmbervaleCompanionVulkanProbe PRIVATE cxx_std_17)
target_compile_definitions(EmbervaleCompanionVulkanProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_executable(EmbervaleCompanionPreview WIN32 src/preview.cpp src/localization.cpp)
target_compile_features(EmbervaleCompanionPreview PRIVATE cxx_std_17)
target_include_directories(EmbervaleCompanionPreview PRIVATE include)
target_compile_definitions(EmbervaleCompanionPreview PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
add_custom_command(TARGET EmbervaleCompanionPreview POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/locales" "$<TARGET_FILE_DIR:EmbervaleCompanionPreview>/locales")
add_library(EmbervaleCompanion SHARED src/companion.cpp src/localization.cpp)
target_compile_features(EmbervaleCompanion PRIVATE cxx_std_17)
target_include_directories(EmbervaleCompanion PRIVATE include)
target_compile_definitions(EmbervaleCompanion PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set_target_properties(EmbervaleCompanion PROPERTIES OUTPUT_NAME "EmbervaleCompanion")
# Assets live alongside the DLL; no game installation/injection is performed.
add_custom_command(TARGET EmbervaleCompanion POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/locales" "$<TARGET_FILE_DIR:EmbervaleCompanion>/locales")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(EmbervaleCompanionTests tests/localization_tests.cpp src/localization.cpp)
  target_compile_features(EmbervaleCompanionTests PRIVATE cxx_std_17)
  target_include_directories(EmbervaleCompanionTests PRIVATE include)
  target_compile_definitions(EmbervaleCompanionTests PRIVATE UNICODE _UNICODE)
  add_custom_command(TARGET EmbervaleCompanionTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/locales" "$<TARGET_FILE_DIR:EmbervaleCompanionTests>/locales")
  add_test(NAME LocalizationTests COMMAND EmbervaleCompanionTests "$<TARGET_FILE_DIR:EmbervaleCompanionTests>/locales")
endif()

# Shroudtopia integration is optional for builds without the SDK header.
option(BUILD_SHROUDTOPIA_ADAPTER "Build the Shroudtopia mod loader adapter" ON)
set(SHROUDTOPIA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/shroudtopia"
    CACHE PATH "Directory containing shroudtopia.h")
if(WIN32 AND BUILD_SHROUDTOPIA_ADAPTER)
  if(NOT EXISTS "${SHROUDTOPIA_INCLUDE_DIR}/shroudtopia.h")
    message(FATAL_ERROR "Shroudtopia header not found in ${SHROUDTOPIA_INCLUDE_DIR}. Set SHROUDTOPIA_INCLUDE_DIR or -DBUILD_SHROUDTOPIA_ADAPTER=OFF")
  endif()
  add_library(EmbervaleCompanionShroudtopia SHARED src/shroudtopia_adapter.cpp)
  target_compile_features(EmbervaleCompanionShroudtopia PRIVATE cxx_std_17)
  target_include_directories(EmbervaleCompanionShroudtopia PRIVATE "${SHROUDTOPIA_INCLUDE_DIR}")
  # WIN32_LEAN_AND_MEAN is already defined in the upstream header.
  target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE NOMINMAX UNICODE _UNICODE)
  set_target_properties(EmbervaleCompanionShroudtopia PROPERTIES OUTPUT_NAME "EmbervaleCompanionShroudtopia")
endif()
