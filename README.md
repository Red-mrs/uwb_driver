# uwb_driver

ROS 2 driver for UWB/UVDAR ranging modules attached over USB CDC-ACM. It reads the module's
[LLCP](include/uwb_driver/llcp/LLCP_README.md) stream and republishes every range report it decodes as
`uwb_driver/msg/UwbRangeStamped`.

Written for the [ULTRALOC](https://github.com/fly4future/ultraloc) UWB + UVDAR stack. `UwbRangeStamped` and
`UwbRange` are vendored under [msg/](msg/) with their original field layout, so the package builds on its own — no
`ultraloc_msgs`, and none of the `mrs_msgs` / `mrs_modules_msgs` / `sensor_msgs` / `geometry_msgs` that package pulls
in. The cost is that the published type is named after this package, so a consumer has to name `uwb_driver` too: an
`ultraloc_msgs` publisher or subscriber will not match on the topic.

## How it works

```
UWB module --USB CDC-ACM--> serial thread --LLCP decode--> UwbRangeStamped publisher
                                ^
                  wall timers: |- serial management (open / health check)
                               `- per-link message statistics
```

- **Looked up by USB serial number, not by `/dev/ttyACM*` name**, so the right module is found after replugging and
  port renumbering. The driver walks `/sys/class/tty`, reads each device's serial from sysfs, and opens the
  lowest-numbered ACM interface of the match — that is the one the firmware writes LLCP to.
- **Bytes are read on a dedicated thread.** The module streams continuously at 2 Mbaud and would overflow the tty
  buffer if polled at a timer rate. Reads are non-blocking with a 1 ms sleep between them.
- **Everything else runs on two wall timers** — serial management (open, reconnect, health check) and statistics —
  so the driver does not depend on how often the node gets spun.
- **Hot-plug tolerant.** A disconnected module is caught by the health check, reported once, and re-opened when it
  returns. Nothing needs restarting.
- **Receive-only.** The driver never writes to the module.

The range topic is published with `QoS(1)` (keep last): a stale distance is no use to a consumer.

## Requirements

- Linux — POSIX termios and `/sys/class/tty`.
- ROS 2, developed on Jazzy. The only message dependencies are `std_msgs` and `builtin_interfaces`, both of which
  ship with a desktop install.
- No udev rules needed beyond the usual group membership:
  ```bash
  sudo usermod -aG dialout $USER   # re-login afterwards
  ```

## Build

```bash
cd ~/ros2_ws
git clone <this repo> src/uwb_driver
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install --packages-up-to uwb_driver
source install/setup.bash
```

Source the resulting `install/setup.bash` in **every** shell that touches the topic, including the one you inspect
from. `ros2 topic echo` imports the message type through Python, so a shell without the overlay sourced fails with:

```
The message type 'uwb_driver/msg/UwbRangeStamped' is invalid
```

The package also exports `uwb_driver::UwbDriverComponent`, so another package can `find_package(uwb_driver)` and
load the driver into its own component container.

## First run: find the serial number

With `usb_serial` empty the driver logs every UWB module it can see, then keeps looking:

```bash
ros2 launch uwb_driver uwb_driver.launch.py
```

```
[ERROR] usb_serial is not set, connected devices: 205F33524E31 [ttyACM0] 1B0025006E31 [ttyACM1]. Set usb_serial to select one.
```

The same value can be read straight off the device:

```bash
udevadm info -q property -n /dev/ttyACM0 | grep ID_SERIAL_SHORT
```

Pass it on the command line to check it works, then put it in the config file:

```bash
ros2 launch uwb_driver uwb_driver.launch.py serial_number:=205F33524E31
```

> [!NOTE]
> The module also enumerates as a USB flash drive exposing `CONFIG.TXT`. That file needs `usbvcp 2` and `uart 1`;
> otherwise the module does not put LLCP on the port and the driver connects but decodes nothing.

## Launch

```bash
ros2 launch uwb_driver uwb_driver.launch.py [arg:=value]
```

| Argument | Default | Description |
|---|---|---|
| `node_name` | `uwb_driver` | Node name. Must match the top-level key in the parameter file. |
| `config` | `<pkg>/config/config.yaml` | Path to a node parameter file. |
| `serial_number` | *(empty)* | USB serial number. Overrides `usb_serial` from the config file; empty leaves the file in charge. |
| `container` | *(empty)* | Fully qualified name of an existing component container to load into. Empty starts a dedicated multi-threaded container. |
| `standalone` | `false` | Run as a plain node instead of a component. Useful with a debugger. |

The node lands in the `/$UAV_NAME` namespace (default `uav`), following the MRS bringup conventions — which is also
what the topic name is relative to. In component mode the namespace comes from the container, so `container` also
decides the final topic name.

To load into a container that already exists, and share its process with the rest of the stack:

```bash
ros2 launch uwb_driver uwb_driver.launch.py container:=/uav/uvdar_container
```

## Parameters

`config/config.yaml`, under a top-level key naming the node (or `**`):

| Parameter | Default | Description |
|---|---|---|
| `usb_serial` | `""` | USB serial number of the module. Empty makes the driver list what it can see and keep looking. |
| `frame_id` | `uwb` | `frame_id` stamped on every published message. |
| `topic` | `uwb/distance` | Range topic, relative to the node's namespace. Empty advertises no topic at all. |
| `debug` | `false` | Log every byte that could not be turned into an LLCP message, at DEBUG level. |
| `retry_rate_hz` | `1.0` | How often to look for the device while disconnected. Each scan only walks `/sys/class/tty`. |
| `stats_rate_hz` | `1.0` | How often to print per-link message counts. Suppressed while nothing is being received. |

Non-positive rates fall back to 1.0 Hz with a warning.

> [!WARNING]
> Quote the serial number wherever you set it. A value that looks like scientific notation — all digits, then `E`,
> then digits, as in `206133834E31` — is resolved to a `double` rather than a string, and the node aborts on startup
> with `parameter 'usb_serial' has invalid type`. This catches both `serial_number:=206133834E31` on the command
> line and an unquoted `usb_serial: 206133834E31` in a config file. Serials containing a letter other than `E`
> (`205F33524E31`) happen to survive by accident. Quote it everywhere:
>
> ```bash
> ros2 launch uwb_driver uwb_driver.launch.py "serial_number:='206133834E31'"
> ```
>
> ```yaml
> usb_serial: "206133834E31"   # in config.yaml
> ```

## Output

`uwb_driver/msg/UwbRangeStamped` on `<namespace>/uwb/distance`:

```
header.stamp     # ROS time the LLCP frame was decoded — the module sends no timestamp
header.frame_id  # the frame_id parameter
range.initiator_address
range.responder_address
range.own_address
range.distance   # metres; the module reports millimetres
```

Every address is the module's 16-bit address zero-extended into the message's `uint32`.

```bash
ros2 topic echo /uav/uwb/distance
```

The statistics timer reports what arrived per link, the quickest way to tell a silent module from a slow one:

```
[INFO] ranges received over the last 1.0001 s: 0x6->0x1 59 msgs (58.9926 Hz)
```

## Debugging

Turn on byte-level logging:

```bash
ros2 param set /uav/uwb_driver debug true
ros2 service call /uav/uwb_driver/set_logger_level rcl_interfaces/srv/SetLoggerLevel \
  "{logger: /uav/uwb_driver, level: 0}"   # 0 = DEBUG
```

Resolved parameters, useful when a config file is not doing what you expect:

```bash
ros2 param dump /uav/uwb_driver
```

To attach a debugger, skip the container:

```bash
ros2 launch uwb_driver uwb_driver.launch.py standalone:=true
gdb -p $(pgrep -f uwb_driver_node)
```

## Layout

```
include/uwb_driver/
  uwb_ros_driver.h      UwbDriverComponent — parameters, timers, publishing
  serial_port.h         POSIX tty wrapper; SerialPortThreadsafe adds a mutex
  device_finder.h       serial number -> /dev path, via sysfs
  uwb_types.h           16-byte range payload layout, spelled out explicitly
  llcp/llcp.h           vendored LLCP (+ LLCP_README.md)
  utils/                ILogger abstraction, keeping serial_port.cpp ROS-free
src/
  uwb_ros_driver.cpp    component implementation, registers the plugin
  uwb_driver_node_main.cpp  standalone entry point
  llcp/llcp.c           vendored LLCP, kept byte-identical to upstream
msg/                    vendored UwbRange / UwbRangeStamped (+ package.xml group membership)
launch/uwb_driver.launch.py
config/config.yaml
```

The vendored LLCP sources are not modified. Binary framing — the module sends raw bytes, not the hex ASCII that
`llcp.h` defaults to — is selected from [CMakeLists.txt](CMakeLists.txt) with `LLCP_COMM_HEXADECIMAL=0` and
`LLCP_LITTLE_ENDIAN`.

## Known limitations

- **Checksums are computed but not enforced.** A bug inherited from upstream `llcp.c` tests the `checksum_matched`
  *pointer* rather than the value it points at, so any frame with a plausible length is accepted. The driver also
  ignores the flag LLCP reports. In practice a corrupted frame yields one wrong range instead of being dropped.
- **No LLCP message-ID filtering.** Whatever payload arrives is reinterpreted as a range report. Fine for the
  current firmware, which sends nothing else; it would mis-decode a module that multiplexes message types.
- **The stamp is host receive time**, so it carries USB and scheduling jitter — there is no hardware timestamp.
- The node name is hard-coded to `uwb_driver` in the C++ constructor and overridden by the launch file. A config
  file whose top-level key does not match the running node name is silently ignored by ROS 2.

## License and acknowledgement

BSD 3-Clause. Includes [LLCP](include/uwb_driver/llcp/LLCP_README.md) from the
[CTU MRS group](http://mrs.felk.cvut.cz/), vendored unmodified.

Maintainers: Jan Vojnar (Fly4Future), Radomír Nový (CTU FEE).
