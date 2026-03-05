import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    # 获取包的安装路径以找到配置文件
    pkg_share = get_package_share_directory('bms_ros_client')
    config_path = os.path.join(pkg_share, 'config', 'bms_params.yaml')

    return LaunchDescription([
        Node(
            package='bms_ros_client',
            executable='bms_bridge',
            name='bms_bridge',
            output='screen',
            parameters=[config_path],
            # 允许在命令行通过 --ros-args -p 修改参数
            arguments=['--ros-args', '--log-level', 'info']
        )
    ])
