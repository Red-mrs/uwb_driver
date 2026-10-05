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
> See [Device CLI](#device-cli) for how to set either.

## Device CLI

The module runs a command line on the same CDC-ACM port the driver reads. It is where the UWB channel, TX power,
node ID and output routing are set; everything it can change is listed under
[Device parameters](#device-parameters), and anything else means reflashing.

### Opening the console

Stop the driver first — it opens the port `O_RDWR`, so a terminal attached at the same time competes with it for
the module's bytes.

```bash
pkill -f uwb_driver_node          # or just Ctrl-C the launch
screen /dev/ttyACM0 115200
```

The baud rate is decoration: this is USB CDC-ACM and the setting is never applied to anything. To get your shell
back without hanging up the port, `Ctrl-A` then `d` detaches the `screen` session. For any terminal, turn **local
echo off**, which is what the device expects — it handles the erase character itself, so with local echo on,
backspace leaves the line visibly scrambled.

Nothing is printed when you connect: the prompt is written once after the port is opened and not repeated until the
module resets. Press Enter for it.

```
uvdar> get usbvcp
usbvcp 1  # debug
uvdar> status
runtime:
  deviceid                   0x0001
  frequency                  30
  ...
  chan                       5
  txpower                    0x30
  # unsaved changes - `save` to write CONFIG.TXT
uvdar>
```

Command names are case-insensitive, `?` is the same as `help`, and Ctrl-C abandons whatever you are mid-way through
typing.

### Commands

| Command | |
|---|---|
| `help` | Command list, then every parameter with its firmware default. `?` does the same. |
| `status` | Runtime values, with a marker if they differ from what is on flash. |
| `default` | The values compiled into the firmware — unaffected by `set`. |
| `get <param>` | One parameter. A bare `get` is the same as `status`. |
| `set <param> <value>` | Change one parameter. One parameter per command. |
| `save` | Write the runtime values to `CONFIG.TXT`, so they survive a reboot. |
| `reboot` | Reset the module. |

`set` takes effect on the running module as far as it can; `save` is only what makes the change survive a
reboot. The reply echoes the parameter in the form `get` prints it, so `set txpower 52` answers
`txpower 0x34`:

```
uvdar> set chan 9
UWB: chan 9, txpower 0x30
chan 9
uvdar> save
saving 398 bytes...
uvdar>
```

> [!NOTE]
> That `UWB: chan 9, ...` line is the firmware's debug output, and it reaches this port only while `usbvcp` is
> `debug` — which is what you need anyway to see the prompt usefully. In `ros` mode the CLI still answers, but
> `printf` goes elsewhere. If the module stops responding to your eyes entirely, `set usbvcp debug` blind and it
> comes back. Debug text and CLI replies travel on separate buffers, so expect the two to interleave rather than
> always arrive in the order above.

Values are decimal or `0x` hex; `usbvcp` and `uart` also take `none` / `debug` / `ros`, booleans `0` / `1` / `off`
/ `on` / `no` / `yes`, and `pattern` a bit string (`set pattern 0110`). Anything out of range is refused with the
allowed span, `chan` takes 5 or 9 and nothing between them, and text following a number is read as a typo rather
than ignored — `set chan 9 now` fails instead of silently setting 9.

### Device parameters

Names are exactly as `status` prints them, in that order. Not to be confused with the node's own
[Parameters](#parameters), which are a separate thing set in `config/config.yaml`.

| Parameter | Default | |
|---|---|---|
| `deviceid` | `0x0001` | The module's own node ID in the ranging network. Has to differ between modules that range together, and has to be set before boot: the ranging task reads it once when it starts. `set` stores it and says so; `reboot` applies it. Only the low byte reaches the ranging protocol, so IDs that differ in the high byte alone collide. |
| `frequency` | `30` | UVDAR blink rate, in pattern bits per second. |
| `pattern` | `0101` | UVDAR LED bit pattern, up to 64 bits. |
| `usbvcp` | `1` `debug` | What the USB port carries: `none`, `debug` (printf text) or `ros` (the LLCP binary stream this driver decodes). |
| `uart` | `2` `ros` | The same choice for the 2 Mbaud UART on the header. |
| `rssi` | `0` | Log the DW3000's channel impulse response diagnostics — a per-path peak readout, useful when a distance looks wrong. This firmware does not put an RSSI value in the range reports. |
| `round_robin` | `1` † | Rotate the initiator role between nodes instead of holding it. |
| `tx_ant_dly` | `16385` | Transmit antenna delay, in UUS (≈ 15.65 ns). A wrong value is a constant range offset. |
| `rx_ant_dly` | `16385` | Receive antenna delay, same units and same effect. |
| `poll_tx_to_resp_rx_dly_uus` | `240` † | When the initiator opens its receiver after sending a poll. |
| `resp_rx_timeout_uus` | `2300` † | How long the initiator waits for the response before giving up on the round. |
| `poll_rx_to_resp_tx_dly_uus` | `1000` † | How long the responder takes to prepare its response. |
| `delay_between_nodes` | `1000` † | Per-node slot offset within one ranging round. |
| `rng_delay_ms` | `10` † | Delay between ranging rounds — the main lever on ranging rate. |
| `loneliness_dly_ms` | `200` † | How long a module goes without hearing a poll before it takes the initiator role. |
| `poll_limit` | `5` † | Polls attempted before giving up the initiator role. |
| `chan` | `5` | DW3xxx RF channel: 5 or 9, nothing else. Must be identical on every module that ranges with itself, so a module left on the other channel sees no peers at all. |
| `txpower` | `0x30` | TX power, as the single byte the driver register is built from. |

> [!WARNING]
> **Eight of these — the ones marked † — are stored, echoed back, and written to `CONFIG.TXT`, but the firmware
> does not act on them:** the ranging code reads their compiled-in values instead. `set` on them changes what you
> see, not what the module does, so treat them as read-only until a firmware build wires them up. Everything else
> applies live, `deviceid` excepted.
>
> Two of the live ones disturb ranging while it runs. `set chan` reconfigures the radio and the round in flight is
> lost. After `set tx_ant_dly` / `rx_ant_dly`, the report for the round already in the air can read one wrong
> distance, off by roughly half the change you just made. Both settle on the next round; neither is a fault.

### Editing `CONFIG.TXT` instead

`CONFIG.TXT` on the mass-storage interface holds what the module loads at boot, `STATUS.TXT` what is running now,
and both use the same `key value` lines that `get` prints. Editing the file from the PC does work — the module picks
up the values once the OS has flushed the write, without a reboot, and mirrors the file to flash when the drive is
ejected — but the CLI is the better tool for a single change: it validates each value before accepting it and
reports a mistake on the spot. A hand-edited file is parsed as a whole and rejected as a whole, so one typo leaves
the module running the values it had before, and the only trace is a line on the debug port.

> [!WARNING]
> The drive is a FAT12 image in RAM that the firmware mirrors into one flash sector, not a real disk, and two
> things follow. Until that mirror runs the edit lives in RAM, so pulling power without ejecting loses it. And a
> filesystem driver on the host rewrites the allocation tables as it saves a file, which relocates `CONFIG.TXT` to
> another cluster and pushes that churn into flash. That last one has bitten this stack before: after the file was
> edited on a PC, the firmware wrote its `save` to one cluster and read boot values from another, so settings
> appeared not to persist at all. Read `CONFIG.TXT` from the device freely; edit it from the device.

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
> Quote the serial number in the YAML config file. An unquoted value that looks like scientific notation — all digits,
> then `E`, then digits, as in `206133834E31` — is parsed as a `double`, and the node aborts on startup with
> `parameter 'usb_serial' has invalid type`. The launch file forces `serial_number` to a string, so the command-line
> launch argument does not need extra quoting:
>
> ```bash
> ros2 launch uwb_driver uwb_driver.launch.py serial_number:=206133834E31
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

Maintainers: Radomír Nový (CTU FEE), Jan Vojnar (Fly4Future)
