#include <uwb_driver/device_finder.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace uwb_driver::device_finder
{

/* listAllDeviceSerialPorts() //{ */

SerialMap listAllDeviceSerialPorts()
{
  namespace fs = std::filesystem;

  SerialMap serial_map;

  for (const auto& entry : fs::directory_iterator("/sys/class/tty")) {
    const std::string tty_name = entry.path().filename();
    if (tty_name.find("ttyACM") != 0) {
      continue;
    }

    /* The serial number lives on the parent USB device, not on the tty. */
    const std::string serial_file = entry.path().string() + "/device/../serial";

    std::ifstream serial_ifs(serial_file);
    if (!serial_ifs.is_open()) {
      continue;
    }

    std::string serial;
    std::getline(serial_ifs, serial);

    if (serial.empty()) {
      continue;
    }

    serial_map[serial].push_back("/dev/" + tty_name);
  }

  /* A module can expose several ACM interfaces; the first one is the one the
   * firmware writes LLCP to, and it is always the lowest-numbered. */
  for (auto& [serial, ttys] : serial_map) {
    std::sort(ttys.begin(), ttys.end());
  }

  return serial_map;
}

//}

/* getDeviceSerialPort() //{ */

std::string getDeviceSerialPort(const std::string& id)
{
  const SerialMap serial_map = listAllDeviceSerialPorts();

  const auto it = serial_map.find(id);
  if (it == serial_map.end() || it->second.empty()) {
    return "";
  }

  return it->second.front();
}

//}

}  // namespace uwb_driver::device_finder
