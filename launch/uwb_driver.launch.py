import os

import launch
import launch_ros.actions
from ament_index_python.packages import get_package_share_directory
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import ComposableNodeContainer, LoadComposableNodes
from launch_ros.descriptions import ComposableNode

PKG_NAME = "uwb_driver"

# The standalone executable has a fixed name, unlike the node it produces.
STANDALONE_EXECUTABLE = "uwb_driver_node"


def default_serial():
    # The serial belongs to the drone, not to the package, so it comes from the
    # environment (UWB_SERIAL in .bashrc) rather than from the shipped config.
    # An unset variable leaves the argument empty, which keeps the config file in
    # charge.
    return os.getenv('UWB_SERIAL', '')


def generate_launch_description():
    ld = launch.LaunchDescription()

    ld.add_action(DeclareLaunchArgument(
        'node_name', default_value='uwb_driver',
        description='Name of the driver node. Must match the top-level key in the config file.'
    ))
    ld.add_action(DeclareLaunchArgument(
        'config',
        default_value=os.path.join(get_package_share_directory(PKG_NAME), 'config', 'config.yaml'),
        description='Path to a node parameter file.'
    ))
    ld.add_action(DeclareLaunchArgument(
        'serial_number', default_value=default_serial(),
        description='USB serial number of the module. Defaults to $UWB_SERIAL, and overrides '
                    'usb_serial from the config file when set to a non-empty value.'
    ))
    ld.add_action(DeclareLaunchArgument(
        'container', default_value='',
        description='Fully qualified name of an existing component container. '
                    'If empty, a container is started for this driver.'
    ))
    ld.add_action(DeclareLaunchArgument(
        'standalone', default_value='false',
        description='Run as a plain node instead of a component. Useful with a debugger.'
    ))

    ld.add_action(OpaqueFunction(function=launch_setup))
    return ld


def launch_setup(context):
    # UAV_NAME keeps the driver compatible with the MRS bringup conventions; the
    # namespace it lands in is what the published topic is relative to.
    uav_name = os.getenv('UAV_NAME', 'uav')

    node_name = LaunchConfiguration('node_name').perform(context)
    config_path = LaunchConfiguration('config').perform(context)
    serial_number = LaunchConfiguration('serial_number').perform(context)
    container = LaunchConfiguration('container').perform(context)
    standalone = LaunchConfiguration('standalone').perform(context).lower() in ('true', '1', 'yes')

    # A serial given on the command line should win over the config file, while an
    # empty one must not override the file with a blank value. The driver declares
    # usb_serial with the config file's value as its default, so an override here
    # takes precedence without the file needing to be re-read.
    parameters = [config_path]
    if serial_number:
        parameters.append({'usb_serial': serial_number})

    if standalone:
        return [launch_ros.actions.Node(
            package=PKG_NAME,
            executable=STANDALONE_EXECUTABLE,
            name=node_name,
            namespace=f'/{uav_name}',
            parameters=parameters,
            output='screen',
        )]

    description = ComposableNode(
        package=PKG_NAME,
        plugin='uwb_driver::UwbDriverComponent',
        name=node_name,
        parameters=parameters,
    )

    if container:
        return [LoadComposableNodes(
            target_container=container,
            composable_node_descriptions=[description],
        )]

    return [ComposableNodeContainer(
        name='uwb_container',
        namespace=f'/{uav_name}',
        package='rclcpp_components',
        # The serial reading happens on its own thread regardless, but a
        # multi-threaded container keeps the timers from being delayed by other
        # components sharing the container.
        executable='component_container_mt',
        composable_node_descriptions=[description],
        output='screen',
    )]
