/*
 * Minimal logging abstraction shared by the driver and its serial helper.
 *
 * The serial layer deliberately does not know about rclcpp - it receives an
 * ILogger* at construction, which keeps it usable outside ROS.
 */

#pragma once

#include <string>

enum class LogLevel{Debug, Info, Warn, Error};

class ILogger
{
public:
  virtual ~ILogger() = default;

  virtual void log(const LogLevel level, const std::string& msg) = 0;

  void debug(const std::string& msg){ log(LogLevel::Debug, msg); }
  void info(const std::string& msg){ log(LogLevel::Info, msg); }
  void warn(const std::string& msg){ log(LogLevel::Warn, msg); }
  void error(const std::string& msg){ log(LogLevel::Error, msg); }
};
