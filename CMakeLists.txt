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
option(ENABLE_VULKAN_HOOK_EXPERIMENT "Enable the experimental Vulkan present hook" OFF)
option(ENABLE_C4_SHROUDTOPIA_PROBE "Read-only Shroudtopia C4 render signature probe" OFF)
option(ENABLE_C4_S7_PASSIVE_PRESENT_HOOK "Experimental Shroudtopia present observer (writes one pointer)" OFF)
option(ENABLE_C4_S10_PRESENT_INFO "Read-only Vulkan present snapshots through working S9 hook" OFF)
option(ENABLE_C4_S11_SWAPCHAIN_OBSERVER "Observe swapchain recreation and image enumeration (2 Vulkan dispatch hooks)" OFF)
option(ENABLE_C4_S12_ACQUIRE_OBSERVER "Observe vkAcquireNextImageKHR at verified Vulkan dispatch slot +0x598" OFF)
option(ENABLE_C4_S13_NATIVE_CONTEXT "Verified Enshrouded render context observer through Shroudtopia (no GPU work)" OFF)
option(ENABLE_C4_S14_ORANGE_SQUARE "C4-S14 opt-in Vulkan orange square: single swapchain generation, menu-only test" OFF)
option(ENABLE_C4_S15_RESILIENT_SQUARE "C4-S15 guarded regeneration with retained old resources (no hot reload)" OFF)
option(ENABLE_C4_S15_NATIVE_BOOTSTRAP "C4-S15 assume 2560x1440 RGBA UNORM for preexisting swapchain on exact matched client" OFF)
option(ENABLE_C4_S16_NATIVE_FIRST "C4-S16: initialize Vulkan hooks from verified native context, without S4 memory scan" OFF)
option(ENABLE_C4_S17_RECLAIMING_SQUARE "C4-S17 opt-in successor reacquisition retirement and precise swapchain matching" OFF)
option(ENABLE_C4_S18_PUBLIC_API "C4-S18 opt-in stable C ABI surface for third-party mods" OFF)
option(ENABLE_C4_S19_STORAGE "C4-S19 opt-in module-scoped persistent storage C ABI v1.1" OFF)
option(ENABLE_C4_S20_LEAN_STARTUP "C4-S20 skip historical Load scanner and reduce diagnostic logs" OFF)
option(ENABLE_C4_S21_CORE_MODE "C4-S21 replace square with a headless Core or opt-in dev banner" OFF)
option(ENABLE_C4_S21_DEV_BANNER "C4-S21 show in-game loaded banner for 4.5 seconds in dev builds" OFF)
option(ENABLE_C4_S8_DIRECT_PRESENT_DETOUR "Experimental Shroudtopia direct present function detour" OFF)
set(SHROUDTOPIA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/shroudtopia"
    CACHE PATH "Directory containing shroudtopia.h")
if(WIN32 AND BUILD_SHROUDTOPIA_ADAPTER)
  if(NOT EXISTS "${SHROUDTOPIA_INCLUDE_DIR}/shroudtopia.h")
    message(FATAL_ERROR
      "Shroudtopia header not found in ${SHROUDTOPIA_INCLUDE_DIR}. "
      "Set SHROUDTOPIA_INCLUDE_DIR or -DBUILD_SHROUDTOPIA_ADAPTER=OFF")
  endif()
  add_library(EmbervaleCompanionShroudtopia SHARED
    src/shroudtopia_adapter.cpp)
  target_compile_features(EmbervaleCompanionShroudtopia PRIVATE cxx_std_17)
  target_include_directories(EmbervaleCompanionShroudtopia PRIVATE
    "${SHROUDTOPIA_INCLUDE_DIR}")
  target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
    NOMINMAX UNICODE _UNICODE)
  if(ENABLE_C4_SHROUDTOPIA_PROBE)
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE EMBERVALE_C4_SHROUDTOPIA_PROBE=1)
  endif()
  if(ENABLE_C4_S7_PASSIVE_PRESENT_HOOK)
    if(NOT ENABLE_C4_SHROUDTOPIA_PROBE)
      message(FATAL_ERROR "C4-S7 requer ENABLE_C4_SHROUDTOPIA_PROBE=ON")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S7_PASSIVE_PRESENT_HOOK=1)
  endif()
  if(ENABLE_C4_S8_DIRECT_PRESENT_DETOUR)
    if(NOT ENABLE_C4_SHROUDTOPIA_PROBE)
      message(FATAL_ERROR "C4-S8 requer ENABLE_C4_SHROUDTOPIA_PROBE=ON")
    endif()
    if(ENABLE_C4_S7_PASSIVE_PRESENT_HOOK OR ENABLE_VULKAN_HOOK_EXPERIMENT)
      message(FATAL_ERROR "C4-S8 nao pode coexistir com hooks S7 ou legacy")
    endif()
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(MINHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(minhook
      GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
      GIT_TAG v1.3.4)
    FetchContent_MakeAvailable(minhook)
    target_link_libraries(EmbervaleCompanionShroudtopia PRIVATE minhook)
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR=1)
  endif()
  if(ENABLE_C4_S10_PRESENT_INFO)
    if(NOT ENABLE_C4_S7_PASSIVE_PRESENT_HOOK OR NOT ENABLE_C4_SHROUDTOPIA_PROBE)
      message(FATAL_ERROR "C4-S10 requires C4-S9 observer and Shroudtopia probe")
    endif()
    find_package(Vulkan REQUIRED)
    target_link_libraries(EmbervaleCompanionShroudtopia PRIVATE Vulkan::Headers)
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S10_PRESENT_INFO=1 VK_NO_PROTOTYPES)
  endif()
  if(ENABLE_C4_S11_SWAPCHAIN_OBSERVER)
    if(NOT ENABLE_C4_S7_PASSIVE_PRESENT_HOOK OR NOT ENABLE_C4_S10_PRESENT_INFO)
      message(FATAL_ERROR "C4-S11 requires S9 and S10")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S11_SWAPCHAIN_OBSERVER=1)
  endif()
  if(ENABLE_C4_S12_ACQUIRE_OBSERVER)
    if(NOT ENABLE_C4_S11_SWAPCHAIN_OBSERVER OR NOT ENABLE_C4_S10_PRESENT_INFO OR
       NOT ENABLE_C4_S7_PASSIVE_PRESENT_HOOK)
      message(FATAL_ERROR "C4-S12 requires S9 + S10 + S11")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S12_ACQUIRE_OBSERVER=1)
  endif()
  if(ENABLE_C4_S13_NATIVE_CONTEXT)
    if(NOT ENABLE_C4_S12_ACQUIRE_OBSERVER OR NOT ENABLE_C4_S11_SWAPCHAIN_OBSERVER OR
       NOT ENABLE_C4_S10_PRESENT_INFO OR NOT ENABLE_C4_S7_PASSIVE_PRESENT_HOOK OR
       NOT ENABLE_C4_SHROUDTOPIA_PROBE)
      message(FATAL_ERROR "C4-S13 requires C4-S9/S10/S11/S12")
    endif()
    if(ENABLE_C4_S8_DIRECT_PRESENT_DETOUR OR ENABLE_VULKAN_HOOK_EXPERIMENT)
      message(FATAL_ERROR "C4-S13 conflicts with legacy or S8 direct detour")
    endif()
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(MINHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(minhook
      GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
      GIT_TAG v1.3.4)
    FetchContent_MakeAvailable(minhook)
    target_link_libraries(EmbervaleCompanionShroudtopia PRIVATE minhook)
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S13_NATIVE_CONTEXT=1)
  endif()
  if(ENABLE_C4_S14_ORANGE_SQUARE)
    if(NOT ENABLE_C4_S13_NATIVE_CONTEXT OR NOT ENABLE_C4_S12_ACQUIRE_OBSERVER OR
       NOT ENABLE_C4_S11_SWAPCHAIN_OBSERVER OR NOT ENABLE_C4_S10_PRESENT_INFO OR
       NOT ENABLE_C4_S7_PASSIVE_PRESENT_HOOK)
      message(FATAL_ERROR "C4-S14 requires S9/S10/S11/S12/S13")
    endif()
    if(ENABLE_C4_S8_DIRECT_PRESENT_DETOUR OR ENABLE_VULKAN_HOOK_EXPERIMENT)
      message(FATAL_ERROR "C4-S14 conflicts with S8 and legacy Vulkan hook")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S14_ORANGE_SQUARE=1)
  endif()
  if(ENABLE_C4_S15_RESILIENT_SQUARE)
    if(NOT ENABLE_C4_S14_ORANGE_SQUARE)
      message(FATAL_ERROR "C4-S15 requires C4-S14 and its complete prerequisites")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE EMBERVALE_C4_S15_RESILIENT_SQUARE=1)
    target_link_libraries(EmbervaleCompanionShroudtopia PRIVATE user32)
  endif()
  if(ENABLE_C4_S15_NATIVE_BOOTSTRAP)
    if(NOT ENABLE_C4_S15_RESILIENT_SQUARE)
      message(FATAL_ERROR "C4-S15 native bootstrap requires C4-S15")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE EMBERVALE_C4_S15_NATIVE_BOOTSTRAP=1)
  endif()
  if(ENABLE_C4_S16_NATIVE_FIRST)
    if(NOT ENABLE_C4_S15_RESILIENT_SQUARE OR NOT ENABLE_C4_S15_NATIVE_BOOTSTRAP OR
       NOT ENABLE_C4_S13_NATIVE_CONTEXT OR NOT ENABLE_C4_S12_ACQUIRE_OBSERVER OR
       NOT ENABLE_C4_S11_SWAPCHAIN_OBSERVER OR NOT ENABLE_C4_S7_PASSIVE_PRESENT_HOOK)
      message(FATAL_ERROR "C4-S16 requires S9/S11/S12/S13/S15 and native bootstrap")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE EMBERVALE_C4_S16_NATIVE_FIRST=1)
  endif()
  if(ENABLE_C4_S17_RECLAIMING_SQUARE)
    if(NOT ENABLE_C4_S16_NATIVE_FIRST OR NOT ENABLE_C4_S15_RESILIENT_SQUARE OR
       NOT ENABLE_C4_S12_ACQUIRE_OBSERVER OR NOT ENABLE_C4_S11_SWAPCHAIN_OBSERVER)
      message(FATAL_ERROR "C4-S17 requires S16, S15, S12 and S11")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE EMBERVALE_C4_S17_RECLAIMING_SQUARE=1)
  endif()
  if(ENABLE_C4_S18_PUBLIC_API)
    target_sources(EmbervaleCompanionShroudtopia PRIVATE src/embervale_core_api.cpp)
    target_include_directories(EmbervaleCompanionShroudtopia PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S18_PUBLIC_API=1)
  endif()
  if(ENABLE_C4_S19_STORAGE)
    if(NOT ENABLE_C4_S18_PUBLIC_API)
      message(FATAL_ERROR "C4-S19 storage requires ENABLE_C4_S18_PUBLIC_API=ON")
    endif()
    target_sources(EmbervaleCompanionShroudtopia PRIVATE
      src/embervale_core_storage.cpp)
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S19_STORAGE=1)
    if(BUILD_TESTING)
      add_executable(EmbervaleCoreApi19Tests
        tests/core_api_contract_test.cpp
        src/embervale_core_api.cpp src/embervale_core_storage.cpp)
      add_executable(EmbervaleCoreStorage19Tests
        tests/core_storage_contract_test.cpp
        src/embervale_core_api.cpp src/embervale_core_storage.cpp)
      foreach(tgt EmbervaleCoreApi19Tests EmbervaleCoreStorage19Tests)
        target_compile_features(${tgt} PRIVATE cxx_std_17)
        target_include_directories(${tgt} PRIVATE include src)
        target_compile_definitions(${tgt} PRIVATE EMBERVALE_C4_S19_STORAGE=1)
      endforeach()
      add_test(NAME EmbervaleCoreApi19 COMMAND EmbervaleCoreApi19Tests)
      add_test(NAME EmbervaleCoreStorage19Write
        COMMAND EmbervaleCoreStorage19Tests write)
      add_test(NAME EmbervaleCoreStorage19Verify
        COMMAND EmbervaleCoreStorage19Tests verify)
      set_tests_properties(EmbervaleCoreStorage19Verify PROPERTIES
        DEPENDS EmbervaleCoreStorage19Write)
    endif()
  endif()
  if(ENABLE_C4_S20_LEAN_STARTUP)
    if(NOT ENABLE_C4_S16_NATIVE_FIRST OR NOT ENABLE_C4_S17_RECLAIMING_SQUARE OR
       NOT ENABLE_C4_S18_PUBLIC_API OR NOT ENABLE_C4_S19_STORAGE)
      message(FATAL_ERROR "C4-S20 requires S16/S17/S18/S19 ON")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S20_LEAN_STARTUP=1)
  endif()
  if(ENABLE_C4_S21_DEV_BANNER AND NOT ENABLE_C4_S21_CORE_MODE)
    message(FATAL_ERROR "C4-S21 banner requires ENABLE_C4_S21_CORE_MODE=ON")
  endif()
  if(ENABLE_C4_S21_CORE_MODE)
    if(NOT ENABLE_C4_S20_LEAN_STARTUP OR NOT ENABLE_C4_S17_RECLAIMING_SQUARE OR
       NOT ENABLE_C4_S18_PUBLIC_API OR NOT ENABLE_C4_S19_STORAGE)
      message(FATAL_ERROR "C4-S21 requires S17/S18/S19/S20")
    endif()
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_C4_S21_CORE_MODE=1)
    if(ENABLE_C4_S21_DEV_BANNER)
      target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
        EMBERVALE_C4_S21_DEV_BANNER=1)
    endif()
  endif()
  if(ENABLE_VULKAN_HOOK_EXPERIMENT)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    set(MINHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      minhook
      GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
      GIT_TAG v1.3.4
    )
    FetchContent_MakeAvailable(minhook)
    target_link_libraries(EmbervaleCompanionShroudtopia PRIVATE minhook)
    target_compile_definitions(EmbervaleCompanionShroudtopia PRIVATE
      EMBERVALE_EXPERIMENTAL_VULKAN_HOOK=1)
  endif()
  set_target_properties(EmbervaleCompanionShroudtopia PROPERTIES
    OUTPUT_NAME "EmbervaleCompanionShroudtopia")
endif()


option(BUILD_VULKAN_LAYER
    "Build the experimental Vulkan diagnostic layer"
    OFF)

if(WIN32 AND BUILD_VULKAN_LAYER)
    find_package(Vulkan REQUIRED)

    add_library(EmbervaleCompanionVulkanLayer SHARED
        src/vulkan_layer.cpp
    )

    target_compile_features(
        EmbervaleCompanionVulkanLayer PRIVATE cxx_std_17
    )

    target_link_libraries(
        EmbervaleCompanionVulkanLayer PRIVATE Vulkan::Headers
    )

    target_compile_definitions(
        EmbervaleCompanionVulkanLayer PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        VK_NO_PROTOTYPES
    )
endif()
