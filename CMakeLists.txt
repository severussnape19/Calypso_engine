cmake_minimum_required(VERSION 4.2)
project(app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_program(SLANGC slangc REQUIRED)

add_executable(app src/main.cpp)
target_sources(app
  PUBLIC
    FILE_SET CXX_MODULES FILES
      src/types.cppm
      src/vulkanContext.cppm
      src/swapchain.cppm
      src/pipeline.cppm
      src/frameRenderer.cppm
      src/math.cppm
)

target_include_directories(app PRIVATE
  ${Vulkan_INCLUDE_DIRS}
)

target_compile_definitions(app PRIVATE
  GLM_FORCE_RADIANS                  # Angles in radians
  GLM_FORCE_DEPTH_ZERO_TO_ONE        # Vulkan depth range [0.0, 1.0]
  GLM_FORCE_DEFAULT_ALIGNED_GENTYPES # alignas(16) on vec4/mat4 for std140/std430 layout
)

target_link_libraries(app PRIVATE
  Vulkan::Vulkan
  glfw
  glm::glm
)
