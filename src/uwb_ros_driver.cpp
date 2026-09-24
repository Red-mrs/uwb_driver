#include <uwb_driver/uwb_ros_driver.h>

#include <chrono>
#include <cstdio>
#include <sstream>

namespace uwb_driver
{

/* UwbDriverComponent() //{ */

UwbDriverComponent::UwbDriverComponent(const rclcpp::NodeOptions& options) : rclcpp::Node("uwb_driver", options)
{
  logger_ = std::make_shared<RosLogger>(get_logger());

  llcp_initialize(&llcp_receiver_);
  logger_->info("LLCP receiver initialized");

  /* Takes an ILogger rather than the node, so it stays independent of ROS. */
  serial_port_ = std::make_unique<SerialPortThreadsafe>(logger_.get());

  loadParams();
  initRosPublishers();
  initTimers();

  if (usb_serial_.empty()) {
    logger_->info("No USB serial number given, listing the UWB modules that are connected and "
                  "then continuing to look for one");
  }
}

//}

/* ~UwbDriverComponent() //{ */

UwbDriverComponent::~UwbDriverComponent()
{
  /* Stop the timers first: they are what starts new serial threads. */
  if (timer_serial_) {
    timer_serial_->cancel();
  }
  if (timer_stats_) {
    timer_stats_->cancel();
  }

  closeSerialPort();
}

//}

/* loadParams() //{ */

void UwbDriverComponent::loadParams()
{
  usb_serial_ = declare_parameter<std::string>("usb_serial", "");
  frame_id_   = declare_parameter<std::string>("frame_id", "uwb");
  topic_name_ = declare_parameter<std::string>("topic", "uwb/distance");
  debug_      = declare_parameter<bool>("debug", false);

  /* The retry timer only scans sysfs, so 1 Hz is plenty and keeps the cost of
   * waiting for a module that is not plugged in negligible. */
  retry_rate_hz_ = declare_parameter<double>("retry_rate_hz", 1.0);

  /* Statistics are only useful if they come around often enough to average a few
   * messages, but printing them is comparatively expensive. */
  stats_rate_hz_ = declare_parameter<double>("stats_rate_hz", 1.0);

  if (retry_rate_hz_ <= 0.0) {
    logger_->warn("retry_rate_hz must be positive, using 1.0 Hz");
    retry_rate_hz_ = 1.0;
  }

  if (stats_rate_hz_ <= 0.0) {
    logger_->warn("stats_rate_hz must be positive, using 1.0 Hz");
    stats_rate_hz_ = 1.0;
  }

  if (debug_) {
    logger_->info("Serial debug output is enabled");
  }
}

//}

/* initRosPublishers() //{ */

void UwbDriverComponent::initRosPublishers()
{
  if (topic_name_.empty()) {
    logger_->warn("Parameter 'topic' is empty, no range topic will be published");
    return;
  }

  /* Keep the last report only: a stale distance is of no use to a consumer. */
  publisher_ = create_publisher<uwb_driver::msg::UwbRangeStamped>(topic_name_, rclcpp::QoS(1));

  logger_->info("Publishing ranges on '" + std::string(publisher_->get_topic_name()) + "'");
}

//}

/* initTimers() //{ */

void UwbDriverComponent::initTimers()
{
  stats_last_time_ = get_clock()->now();

  /* Wall timers, so the driver works as a component in a container that is not
   * spinning this node's default callback group at a useful rate. */
  timer_serial_ = create_wall_timer(std::chrono::duration<double>(1.0 / retry_rate_hz_),
                                    [this]() { serialTimerCallback(); });

  timer_stats_ = create_wall_timer(std::chrono::duration<double>(1.0 / stats_rate_hz_),
                                   [this]() { statsTimerCallback(); });
}

//}

/* serialTimerCallback() //{ */

void UwbDriverComponent::serialTimerCallback()
{
  if (connected_) {
    if (serial_port_->checkConnected()) {
      return;
    }

    logger_->error("Serial device is disconnected");
    closeSerialPort();
  }

  openSerialPort();
}

//}

/* openSerialPort() //{ */

bool UwbDriverComponent::openSerialPort()
{
  if (usb_serial_.empty()) {
    printAvailableDevices(device_finder::listAllDeviceSerialPorts());
    return false;
  }

  const std::string port_name = device_finder::getDeviceSerialPort(usb_serial_);
  if (port_name.empty()) {
    RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
                                "Device " << usb_serial_ << " is not connected, retrying");
    return false;
  }

  /* Only reached while disconnected, so this logs once per connection rather than
   * once per retry. */
  logger_->info("Opening " + port_name + " for device " + usb_serial_ + " at " + std::to_string(BAUDRATE) + " baud");

  if (!serial_port_->connect(port_name, BAUDRATE)) {
    RCLCPP_ERROR_STREAM_THROTTLE(get_logger(), *get_clock(), 1000, "Could not open " << port_name);
    return false;
  }

  logger_->info("Connected to " + port_name);

  connected_      = true;
  serial_running_ = true;
  serial_thread_  = std::thread(&UwbDriverComponent::serialThread, this);

  return true;
}

//}

/* closeSerialPort() //{ */

void UwbDriverComponent::closeSerialPort()
{
  connected_ = false;

  /* The thread polls connected_ between reads and returns when it goes low; reads
   * are non-blocking, so no wake-up is needed. */
  if (serial_thread_.joinable()) {
    serial_thread_.join();
  }

  serial_running_ = false;
  serial_port_->disconnect();
}

//}

/* printAvailableDevices() //{ */

void UwbDriverComponent::printAvailableDevices(const device_finder::SerialMap& serial_map)
{
  if (serial_map.empty()) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "No ttyACM devices found");
    return;
  }

  std::ostringstream message;
  message << "usb_serial is not set, connected devices:";
  for (const auto& [serial, ttys] : serial_map) {
    message << " " << serial << " [";
    for (size_t i = 0; i < ttys.size(); ++i) {
      message << ttys[i].substr(ttys[i].find_last_of('/') + 1) << (i + 1 < ttys.size() ? "," : "");
    }
    message << "]";
  }
  message << ". Set usb_serial to select one.";

  RCLCPP_ERROR_STREAM_THROTTLE(get_logger(), *get_clock(), 5000, message.str());
}

//}

/* serialThread() //{ */

void UwbDriverComponent::serialThread()
{
  uint8_t rx_buffer[SERIAL_BUFFER_SIZE];

  logger_->info("Serial thread starting");

  while (serial_running_ && rclcpp::ok()) {
    if (!connected_) {
      logger_->warn("Serial thread stopping, the port was disconnected");
      return;
    }

    const int bytes_read = serial_port_->readSerial(rx_buffer, SERIAL_BUFFER_SIZE);

    if (bytes_read <= 0) {
      /* VMIN=0, VTIME=0 makes read() return immediately, so idle time must be
       * yielded explicitly or this thread would spin a core. */
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    for (int i = 0; i < bytes_read; ++i) {
      LLCP_Message_t* message_in    = nullptr;
      bool            checksum_matched = false;

      if (!llcp_processChar(rx_buffer[i], &llcp_receiver_, &message_in, &checksum_matched)) {
        if (debug_) {
          std::ostringstream byte;
          byte << static_cast<unsigned>(rx_buffer[i]);
          logger_->debug(byte.str());
        }
        continue;
      }

      /* llcp_processChar hands back a pointer into the receiver's own buffer, so
       * the payload must be consumed before the next character is processed. */
      const range* range_msg = reinterpret_cast<const range*>(message_in->payload);

      const uint16_t initiator_address = (range_msg->initiator_address_hi << 8) | range_msg->initiator_address_lo;
      const uint16_t responder_address = (range_msg->responder_address_hi << 8) | range_msg->responder_address_lo;
      const uint16_t own_address       = (range_msg->own_address_hi << 8) | range_msg->own_address_lo;

      if (publisher_) {
        uwb_driver::msg::UwbRangeStamped msg_out;
        msg_out.header.stamp            = get_clock()->now();
        msg_out.header.frame_id         = frame_id_;
        msg_out.range.initiator_address = initiator_address;
        msg_out.range.responder_address = responder_address;
        msg_out.range.own_address       = own_address;
        /* The module reports millimetres, the message carries metres. */
        msg_out.range.distance = static_cast<double>(range_msg->distance_mm) / 1000.0;

        publisher_->publish(msg_out);
      }

      {
        std::scoped_lock lock(received_msgs_mutex_);

        const auto it = std::find_if(received_msgs_.begin(), received_msgs_.end(), [&](const msg_counter& counter) {
          return counter.init == initiator_address && counter.resp == responder_address;
        });

        if (it != received_msgs_.end()) {
          ++it->num;
        } else {
          received_msgs_.push_back({initiator_address, responder_address, 1});
        }
      }
    }
  }

  logger_->info("Serial thread finished");
}

//}

/* statsTimerCallback() //{ */

void UwbDriverComponent::statsTimerCallback()
{
  const rclcpp::Time now = get_clock()->now();
  const double       dt  = (now - stats_last_time_).seconds();
  stats_last_time_       = now;

  std::vector<msg_counter> counts;
  {
    std::scoped_lock lock(received_msgs_mutex_);
    counts.swap(received_msgs_);
  }

  if (counts.empty()) {
    /* Nothing to report. Staying quiet here keeps the log readable while a module
     * is unplugged; connection state is reported by the serial timer. */
    return;
  }

  std::ostringstream message;
  message << "ranges received over the last " << dt << " s:";
  for (const auto& counter : counts) {
    char link[32];
    snprintf(link, sizeof(link), " 0x%X->0x%X", counter.init, counter.resp);

    message << link << " " << counter.num << " msgs";
    if (dt > 0.0) {
      message << " (" << static_cast<double>(counter.num) / dt << " Hz)";
    }
  }

  logger_->info(message.str());
}

//}

}  // namespace uwb_driver

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(uwb_driver::UwbDriverComponent)
