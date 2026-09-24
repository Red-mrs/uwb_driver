#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <uwb_driver/msg/uwb_range_stamped.hpp>

#include <uwb_driver/device_finder.h>
#include <uwb_driver/serial_port.h>
#include <uwb_driver/uwb_types.h>
#include <uwb_driver/utils/ros_logger.h>

extern "C" {
#include <uwb_driver/llcp/llcp.h>
}

namespace uwb_driver
{

/*
 * Reads LLCP range reports from a UWB module on a USB CDC-ACM port and publishes
 * them as uwb_driver::msg::UwbRangeStamped.
 *
 * Byte reading happens on a dedicated thread, because the module streams
 * continuously and would overflow the tty buffer if polled at a timer rate.
 * Everything else (open, health check, statistics) runs on two wall timers, so
 * the driver does not depend on the node being spun.
 */
class UwbDriverComponent : public rclcpp::Node
{
public:
  explicit UwbDriverComponent(const rclcpp::NodeOptions& options);

  ~UwbDriverComponent() override;

  UwbDriverComponent(const UwbDriverComponent&)            = delete;
  UwbDriverComponent& operator=(const UwbDriverComponent&) = delete;

private:
  void loadParams();
  void initRosPublishers();
  void initTimers();

  /* Serial management, run on the serial-management timer. */
  void serialTimerCallback();
  bool openSerialPort();
  void closeSerialPort();
  void printAvailableDevices(const device_finder::SerialMap& serial_map);

  /* Runs on serial_thread_, decodes LLCP frames and publishes ranges. */
  void serialThread();

  void statsTimerCallback();

  static constexpr size_t SERIAL_BUFFER_SIZE = 1024;

  /* The module's USB CDC interface runs at 2 Mbaud. */
  static constexpr int BAUDRATE = 2000000;

  std::shared_ptr<RosLogger> logger_;

  std::string usb_serial_;
  std::string frame_id_;
  std::string topic_name_;
  bool        debug_         = false;
  double      stats_rate_hz_ = 1.0;
  double      retry_rate_hz_ = 1.0;

  /* The topic is only advertised when it is configured, so a downstream-less
   * setup can silence it by passing an empty name. */
  rclcpp::Publisher<uwb_driver::msg::UwbRangeStamped>::SharedPtr publisher_;

  std::unique_ptr<SerialPort> serial_port_;

  LLCP_Receiver_t llcp_receiver_ {};

  std::thread serial_thread_;
  std::atomic<bool> serial_running_ {false};
  std::atomic<bool> connected_ {false};

  /* Guarded by received_msgs_mutex_: written on the serial thread, drained by the
   * statistics timer. */
  std::mutex                       received_msgs_mutex_;
  std::vector<msg_counter>         received_msgs_;
  rclcpp::Time                     stats_last_time_;

  rclcpp::TimerBase::SharedPtr timer_serial_;
  rclcpp::TimerBase::SharedPtr timer_stats_;
};

}  // namespace uwb_driver
