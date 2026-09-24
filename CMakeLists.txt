cmake_minimum_required(VERSION 3.16)
project(uwb_driver LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -fno-omit-frame-pointer)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# LLCP is configured for binary framing: the module sends raw bytes, so the
# hexadecimal ASCII encoding that llcp.h defaults to must be switched off.
add_compile_definitions(LLCP_LITTLE_ENDIAN LLCP_COMM_HEXADECIMAL=0)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)

# UwbRangeStamped is vendored from ultraloc_msgs rather than depended on, so the
# driver builds without that package — and without mrs_msgs, mrs_modules_msgs,
# sensor_msgs and geometry_msgs, which it drags in. The field layout is unchanged,
# but the type string is now uwb_driver/msg/UwbRangeStamped, so consumers have to
# name the same package to subscribe.
#
# The generation target must stay ${PROJECT_NAME}: the typesupport libraries it
# produces are lib${PROJECT_NAME}__rosidl_typesupport_*.so and the RMW dlopens them
# by that name when a publisher is created. A LIBRARY_NAME here compiles fine and
# fails at runtime.
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/UwbRange.msg"
  "msg/UwbRangeStamped.msg"
  DEPENDENCIES
    builtin_interfaces
    std_msgs
)

add_library(UwbDriver SHARED
  src/llcp/llcp.c
  src/device_finder.cpp
  src/serial_port.cpp
  src/utils/ros_logger.cpp
  src/uwb_ros_driver.cpp
)

set_target_properties(UwbDriver PROPERTIES
  OUTPUT_NAME ${PROJECT_NAME}
  EXPORT_NAME ${PROJECT_NAME}
)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS UwbDriver)

target_include_directories(UwbDriver
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
  # The vendored llcp.c includes <llcp.h> the way upstream wrote it; putting its
  # own directory on the search path keeps the vendored sources byte-identical to
  # the mrs_llcp repository.
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/uwb_driver/llcp
)

# First argument is the generation target, not the library name.
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} rosidl_typesupport_cpp)

target_link_libraries(UwbDriver
  rclcpp::rclcpp
  rclcpp_components::component
  ${cpp_typesupport_target}
)

# Generates the plugin description that lets the class be loaded into a
# component container.
rclcpp_components_register_nodes(UwbDriver "uwb_driver::UwbDriverComponent")

# Standalone alternative to running inside a container.
add_executable(uwb_driver_node src/uwb_driver_node_main.cpp)

target_link_libraries(uwb_driver_node UwbDriver)

install(
  TARGETS uwb_driver_node
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(
  TARGETS UwbDriver
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(
  DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp rclcpp_components rosidl_default_runtime)

ament_package()
