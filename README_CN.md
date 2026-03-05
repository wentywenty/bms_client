# TWS BMS ROS 2 客户端 (bms_ros_client)

该仓库是 `bms_daemon` 的配套客户端，包含标准的 ROS 2 节点实现以及一个易用的 Python 开发接口。它从本地 Socket 获取数据并转换为 ROS 标准消息。

## 🌟 核心特性

- **ROS 2 集成**：发布 `sensor_msgs/msg/BatteryState` 消息，完全兼容 ROS 机器人电源管理系统。
- **实时诊断系统**：
    - **电芯监控**：实时计算最大/最小电芯压差，超过阈值 (默认 0.1V) 触发警告。
    - **安全告警**：低电量、电池过热 (55°C) 的三级彩色日志报警（绿/黄/红）。
    - **5秒概览**：周期性在终端输出电池核心状态快照。
- **灵活配置**：支持通过 YAML 文件动态调整发布频率、设计容量及各项报警阈值。
- **双接口支持**：
    - **C++ Node**：高性能 ROS 转发。
    - **Python API**：提供 `BmsClient` 类，支持在非 ROS 脚本中快速读取数据。

## ⚙️ 参数配置 (`bms_params.yaml`)

| 参数 | 默认值 | 说明 |
|---|---|---|
| `publish_rate` | `1.0` | 数据发布频率 (Hz) |
| `low_soc_warn_threshold` | `0.15` | 低电量报警阈值 (15%) |
| `cell_gap_warn_threshold` | `0.1` | 电芯压差报警阈值 (V) |
| `over_temp_threshold` | `55.0` | 二级过温保护阈值 (℃) |

## 🚀 运行指南

### 1. 编译
```bash
colcon build --packages-select bms_ros_client
source install/setup.bash
```

### 2. 使用 Launch 启动 (推荐)
```bash
ros2 launch bms_ros_client bms_client.launch.py
```

### 3. Python 接口示例
```python
from bms_client import BmsClient
client = BmsClient()
data = client.read_data()
if data:
    print(f"当前电压: {data['voltage']}V")
```

## 📦 依赖说明
- 需要先安装并运行 `bms-daemon`。
- C++ 编译依赖 `sensor_msgs` 和 `rclcpp`。
- Python 接口仅依赖内置 `socket` 和 `struct` 库。
