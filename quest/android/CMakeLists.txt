cmake_minimum_required(VERSION 3.16)
project(TFEQuest LANGUAGES C CXX ASM)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The engine sources: the root of this fork (override with -DTFE_SRC=... for another checkout).
if(NOT DEFINED TFE_SRC)
  set(TFE_SRC ${ROOT}/..)
endif()

# SDL2 (shared, loaded by SDLActivity)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${ROOT}/SDL2 sdl2)

# SDL2_image (static, PNG/BMP/TGA/JPG via stb, PNG save via miniz)
file(GLOB IMG_SRC ${ROOT}/SDL2_image/src/IMG*.c)
add_library(sdl2_image_static STATIC ${IMG_SRC})
target_include_directories(sdl2_image_static PUBLIC ${ROOT}/SDL2_image/include PRIVATE ${ROOT}/SDL2_image/src)
target_compile_definitions(sdl2_image_static PRIVATE USE_STBIMAGE LOAD_PNG LOAD_BMP LOAD_TGA LOAD_JPG LOAD_GIF SDL_IMAGE_SAVE_PNG=1 SDL_IMAGE_USE_COMMON_BACKEND)
target_link_libraries(sdl2_image_static PUBLIC SDL2::SDL2)

# Khronos OpenXR loader (shared)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
add_subdirectory(${ROOT}/OpenXR-SDK openxr)

# TFE as libmain.so
add_library(tfe SHARED)
set_target_properties(tfe PROPERTIES OUTPUT_NAME "main")
set(LINUX ON)
set(ENABLE_SYSMIDI OFF)
set(ENABLE_EDITOR OFF)
target_compile_definitions(tfe PRIVATE BUILD_FORCE_SCRIPT TFE_ANDROID IMGUI_IMPL_OPENGL_ES3)
target_compile_options(tfe PRIVATE -Wno-switch -Wno-format-security -fno-strict-aliasing -ftrivial-auto-var-init=zero)
target_include_directories(tfe PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${TFE_SRC}/TheForceEngine
  ${TFE_SRC}/TheForceEngine/TFE_ForceScript/Angelscript/angelscript/include
  ${TFE_SRC}/TheForceEngine/TFE_ForceScript/Angelscript/add_on
  ${ROOT}/SDL2/include
  ${ROOT}/SDL2_image/include)
add_subdirectory(${TFE_SRC}/TheForceEngine tfe_src)
target_sources(tfe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_vrmenu.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_vox.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_mesh.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_hiweapons.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_handskin.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_room.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_mask.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/android_glue.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_hands.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/tfe_xr_weapons.cpp)
target_include_directories(tfe PRIVATE ${ROOT}/OpenXR-SDK/include ${CMAKE_BINARY_DIR}/openxr/include)
target_link_libraries(tfe PRIVATE openxr_loader SDL2::SDL2 sdl2_image_static GLESv3 EGL android log)
