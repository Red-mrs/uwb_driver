/*
 * Standalone entry point.
 *
 * The driver normally runs as a component inside a container. Running it as its
 * own process is mostly useful when attaching a debugger or when no container is
 * available, so this file is kept to the minimum: spin one node with enough
 * threads that the wall timers are not blocked.
 */

#include <uwb_driver/uwb_ros_driver.h>

#include <rclcpp/executors/multi_threaded_executor.hpp>

#include <memory>

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<uwb_driver::UwbDriverComponent>(rclcpp::NodeOptions());

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();

  rclcpp::shutdown();

  return 0;
}
