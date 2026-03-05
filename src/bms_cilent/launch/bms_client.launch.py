import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    # Get package installation path to find the config file
    pkg_share = get_package_share_directory('bms_ros_client')
    config_path = os.path.join(pkg_share, 'config', 'bms_params.yaml')

    return LaunchDescription([
        Node(
            package='bms_ros_client',
            executable='bms_bridge',
            name='bms_bridge',
            output='screen',
            parameters=[config_path],
            # Allow parameter overriding via CLI using --ros-args -p
            arguments=['--ros-args', '--log-level', 'info']
        )
    ])
