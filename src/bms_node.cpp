#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "bms_status.h"

using namespace std::chrono_literals;

class BmsRosBridge : public rclcpp::Node {
public:
    BmsRosBridge() : Node("bms_bridge"), sock_fd_(-1) {
        pub_ = this->create_publisher<sensor_msgs::msg::BatteryState>("battery_state", 10);
        timer_ = this->create_wall_timer(1s, std::bind(&BmsRosBridge::on_timer, this));
        RCLCPP_INFO(this->get_logger(), "BMS ROS Bridge Started.");
    }

private:
    int sock_fd_;
    rclcpp::Publisher<sensor_msgs::msg::BatteryState>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;

    void on_timer() {
        if (sock_fd_ < 0) {
            sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, "/tmp/bms.sock", sizeof(addr.sun_path) - 1);
            if (connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                close(sock_fd_); sock_fd_ = -1;
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000, "Daemon NOT reachable.");
                return;
            }
        }

        bms::BatteryStatus s;
        if (read(sock_fd_, &s, sizeof(s)) == sizeof(s)) {
            auto msg = sensor_msgs::msg::BatteryState();
            msg.header.stamp = this->now();
            msg.header.frame_id = "battery_link";
            
            msg.voltage = s.voltage;
            msg.current = s.current;
            msg.temperature = s.temperature;
            msg.percentage = s.percentage;
            msg.charge = s.charge;
            msg.capacity = s.capacity;
            msg.design_capacity = s.design_capacity;
            
            // 状态映射
            switch (s.work_state) {
                case 1: msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_CHARGING; break;
                case 2: msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_DISCHARGING; break;
                case 4: msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_FULL; break;
                default: msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_NOT_CHARGING; break;
            }

            msg.serial_number = std::string(s.serial_number);
            msg.present = true;
            msg.power_supply_technology = sensor_msgs::msg::BatteryState::POWER_SUPPLY_TECHNOLOGY_LION;
            msg.power_supply_health = (s.protect_status == 0) ? 
                sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_GOOD : 
                sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;

            pub_->publish(msg);
        } else {
            close(sock_fd_); sock_fd_ = -1;
        }
    }
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<BmsRosBridge>());
    rclcpp::shutdown();
    return 0;
}
