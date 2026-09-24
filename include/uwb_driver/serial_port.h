#pragma once

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>
#include <string>

#include <uwb_driver/utils/i_logger.h>

namespace uwb_driver
{

/*
 * Plain POSIX tty wrapper. Takes an ILogger rather than a node so that it stays
 * usable outside ROS.
 */
class SerialPort
{
public:
  explicit SerialPort(ILogger* logger);

  virtual ~SerialPort();

  bool connect(const std::string& port, int baudrate);

  void disconnect();

  virtual bool sendChar(const char c);

  virtual bool sendCharArray(uint8_t* buffer, int len);

  void setBlocking(int fd, int should_block);

  bool checkConnected();

  virtual bool readChar(uint8_t* c);

  virtual int readSerial(uint8_t* arr, int arr_max_size);

protected:
  int serial_port_fd_ = -1;

  ILogger* logger_;
};

class SerialPortThreadsafe : public SerialPort
{
public:
  explicit SerialPortThreadsafe(ILogger* logger);

  virtual bool sendChar(const char c) override
  {
    std::scoped_lock lck(mtx_);
    return SerialPort::sendChar(c);
  };

  virtual bool sendCharArray(uint8_t* buffer, int len) override
  {
    std::scoped_lock lck(mtx_);
    return SerialPort::sendCharArray(buffer, len);
  };

  virtual bool readChar(uint8_t* c) override
  {
    std::scoped_lock lck(mtx_);
    return SerialPort::readChar(c);
  };

  virtual int readSerial(uint8_t* arr, int arr_max_size) override
  {
    std::scoped_lock lck(mtx_);
    return SerialPort::readSerial(arr, arr_max_size);
  };

private:
  std::mutex mtx_;
};

}  // namespace uwb_driver
