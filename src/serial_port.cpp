#include <uwb_driver/serial_port.h>

#include <unistd.h>

namespace uwb_driver
{

namespace
{

/* Maps a numeric baudrate to the termios constant. Returns false for anything the
 * module cannot be configured to, which the caller reports. */
bool toTermiosBaudrate(const int baudrate, speed_t& out)
{
  switch (baudrate) {
    case 9600: out = B9600; return true;
    case 19200: out = B19200; return true;
    case 38400: out = B38400; return true;
    case 57600: out = B57600; return true;
    case 115200: out = B115200; return true;
    case 230400: out = B230400; return true;
    case 460800: out = B460800; return true;
    case 500000: out = B500000; return true;
    case 576000: out = B576000; return true;
    case 921600: out = B921600; return true;
    case 1000000: out = B1000000; return true;
    case 1500000: out = B1500000; return true;
    case 2000000: out = B2000000; return true;
    case 2500000: out = B2500000; return true;
    case 3000000: out = B3000000; return true;
    case 3500000: out = B3500000; return true;
    case 4000000: out = B4000000; return true;
    default: return false;
  }
}

}  // namespace

/* SerialPort() //{ */

SerialPort::SerialPort(ILogger* logger) : logger_(logger)
{
}

//}

/* SerialPortThreadsafe() //{ */

SerialPortThreadsafe::SerialPortThreadsafe(ILogger* logger) : SerialPort(logger)
{
}

//}

/* ~SerialPort() //{ */

SerialPort::~SerialPort()
{
  disconnect();
}

//}

/* checkConnected() //{ */

bool SerialPort::checkConnected()
{
  if (serial_port_fd_ < 0) {
    return false;
  }

  struct termios tmp_newtio
  {
  };

  if (tcgetattr(serial_port_fd_, &tmp_newtio) == -1) {
    logger_->error("Serial port disconnected (" + std::string(strerror(errno)) + ")");
    close(serial_port_fd_);
    serial_port_fd_ = -1;
    return false;
  }

  return true;
}

//}

/* connect() //{ */

bool SerialPort::connect(const std::string& port, int baudrate)
{
  /* O_RDWR | O_NOCTTY: read/write, and do not make this tty the controlling
   * terminal. O_NONBLOCK is only there so that open() cannot stall waiting for
   * the carrier; it is cleared again below. */
  const int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd == -1) {
    logger_->error("Could not open serial port " + port + " (" + strerror(errno) + ")");
    return false;
  }

  fcntl(fd, F_SETFL, 0);

  speed_t termios_baudrate;
  if (!toTermiosBaudrate(baudrate, termios_baudrate)) {
    logger_->error("Unsupported baudrate: " + std::to_string(baudrate));
    close(fd);
    return false;
  }

  struct termios newtio
  {
  };
  bzero(&newtio, sizeof(newtio));

  cfsetispeed(&newtio, termios_baudrate);
  cfsetospeed(&newtio, termios_baudrate);

  newtio.c_cflag |= (CLOCAL | CREAD); /* ignore modem status lines, enable receiver */
  newtio.c_cflag &= ~PARENB;          /* no parity bit */
  newtio.c_cflag &= ~CSTOPB;          /* one stop bit */
  newtio.c_cflag &= ~CSIZE;
  newtio.c_cflag |= CS8;              /* 8-bit words */

  newtio.c_iflag = 0;                 /* raw input */
  newtio.c_oflag = 0;                 /* raw output */
  newtio.c_lflag = 0;                 /* unprocessed, no echo */

  /* Copied from MAVROS to work around an issue with Arduino-based devices. */
  newtio.c_iflag &= ~(IXOFF | IXON);
  newtio.c_cflag &= ~CRTSCTS;

  if (tcflush(fd, TCIFLUSH) != 0 || tcsetattr(fd, TCSANOW, &newtio) != 0) {
    logger_->error("Could not configure serial port " + port + " (" + strerror(errno) + ")");
    close(fd);
    return false;
  }

  /* Disconnect any previous connection only once the new one is usable, so a
   * failed reconnect does not also drop a working port. */
  disconnect();

  serial_port_fd_ = fd;

  setBlocking(serial_port_fd_, 0);

  return true;
}

//}

/* setBlocking() //{ */

void SerialPort::setBlocking(int fd, int should_block)
{
  struct termios tty
  {
  };
  memset(&tty, 0, sizeof tty);
  if (tcgetattr(fd, &tty) != 0) {
    logger_->error("tcgetattr failed (" + std::string(strerror(errno)) + ")");
    return;
  }

  /* VMIN=1, VTIME=0 blocks until at least one byte arrives; VMIN=0, VTIME=0 makes
   * read() return immediately, which is what the serial thread polls. */
  tty.c_cc[VMIN]  = should_block ? 1 : 0;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    logger_->error("tcsetattr failed (" + std::string(strerror(errno)) + ")");
  }
}

//}

/* disconnect() //{ */

void SerialPort::disconnect()
{
  if (serial_port_fd_ >= 0) {
    close(serial_port_fd_);
    serial_port_fd_ = -1;
  }
}

//}

/* sendChar() //{ */

bool SerialPort::sendChar(const char c)
{
  if (serial_port_fd_ < 0) {
    return false;
  }

  if (write(serial_port_fd_, &c, 1) != 1) {
    logger_->warn("Error while writing to serial line (" + std::string(strerror(errno)) + ")");
    return false;
  }

  return true;
}

//}

/* sendCharArray() //{ */

bool SerialPort::sendCharArray(uint8_t* buffer, int len)
{
  if (serial_port_fd_ < 0 || len <= 0) {
    return false;
  }

  /* A short write means the kernel buffer could not take the whole message, which
   * would desynchronise the LLCP stream; treat it as a failure. */
  const ssize_t written = write(serial_port_fd_, buffer, len);
  if (written != len) {
    logger_->warn("Incomplete write to serial line (" + std::to_string(written) + "/" + std::to_string(len) + " bytes)");
    return false;
  }

  tcflush(serial_port_fd_, TCOFLUSH);

  return true;
}

//}

/* readSerial() //{ */

int SerialPort::readSerial(uint8_t* arr, int arr_max_size)
{
  if (serial_port_fd_ < 0) {
    return -1;
  }

  return read(serial_port_fd_, arr, arr_max_size);
}

//}

/* readChar() //{ */

bool SerialPort::readChar(uint8_t* c)
{
  return readSerial(c, 1) == 1;
}

//}

}  // namespace uwb_driver
