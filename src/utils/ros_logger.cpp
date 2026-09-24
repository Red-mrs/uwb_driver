#include <uwb_driver/utils/ros_logger.h>

/* RosLogger //{ */

RosLogger::RosLogger(const rclcpp::Logger& logger) : m_logger(logger)
{
}

//}

/* log() //{ */

void RosLogger::log(const LogLevel level, const std::string& msg)
{
  switch (level) {
    case LogLevel::Debug: RCLCPP_DEBUG_STREAM(m_logger, msg); break;
    case LogLevel::Info: RCLCPP_INFO_STREAM(m_logger, msg); break;
    case LogLevel::Warn: RCLCPP_WARN_STREAM(m_logger, msg); break;
    case LogLevel::Error: RCLCPP_ERROR_STREAM(m_logger, msg); break;
  }
}

//}
