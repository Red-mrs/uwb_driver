#pragma once

#include <map>
#include <string>
#include <vector>

namespace uwb_driver::device_finder
{

/* UWB modules enumerate as ttyACM devices; the map keys are the USB serial
 * numbers read from sysfs, the values the /dev paths belonging to each. */
using SerialMap = std::map<std::string, std::vector<std::string>>;

SerialMap listAllDeviceSerialPorts();

/* First /dev path registered under `id`, or "" when no such device is present. */
std::string getDeviceSerialPort(const std::string& id);

}  // namespace uwb_driver::device_finder
