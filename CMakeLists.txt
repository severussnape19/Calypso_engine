cmake_minimum_required(VERSION 4.2)
project(app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_program(SLANGC slangc REQUIRED)

add_executable(app src/main.cpp)
target_sources(app
  PUBLIC
    FILE_SET CXX_MODULES FILES
      src/types.cppm
      src/app.cppm
)

target_include_directories(app PRIVATE
  ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(app PRIVATE
  Vulkan::Vulkan
  glfw
)
