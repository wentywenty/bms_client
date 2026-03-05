#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "bms_status.h"

using namespace std::chrono_literals;

class BmsRosBridge : public rclcpp::Node {
public:
    BmsRosBridge() : Node("bms_bridge"), sock_fd_(-1) {
        // --- Declare Parameters ---
        this->declare_parameter<std::string>("socket_path", "/tmp/bms.sock");
        this->declare_parameter<double>("publish_rate", 1.0);
        this->declare_parameter<std::string>("frame_id", "battery_link");
        this->declare_parameter<std::string>("location", "base_link");

        socket_path_ = this->get_parameter("socket_path").as_string();
        double rate = this->get_parameter("publish_rate").as_double();
        frame_id_ = this->get_parameter("frame_id").as_string();
        location_ = this->get_parameter("location").as_string();

        pub_ = this->create_publisher<sensor_msgs::msg::BatteryState>("battery_state", 10);
        
        // Setup timer based on rate
        auto period = std::chrono::duration<double>(1.0 / rate);
        timer_ = this->create_wall_timer(period, std::bind(&BmsRosBridge::on_timer, this));

        RCLCPP_INFO(this->get_logger(), "BMS ROS Bridge Initialized. Connecting to %s", socket_path_.c_str());
    }

    ~BmsRosBridge() {
        if (sock_fd_ >= 0) close(sock_fd_);
    }

private:
    int sock_fd_;
    std::string socket_path_;
    std::string frame_id_;
    std::string location_;
    rclcpp::Publisher<sensor_msgs::msg::BatteryState>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;

    bool try_connect() {
        if (sock_fd_ >= 0) return true;

        sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd_ < 0) return false;

        // Set non-blocking for connection attempt
        int flags = fcntl(sock_fd_, F_GETFL, 0);
        fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            if (errno != EINPROGRESS) {
                close(sock_fd_);
                sock_fd_ = -1;
                return false;
            }
            
            // Wait for connection to complete
            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(sock_fd_, &write_fds);
            struct timeval tv = {0, 100000}; // 100ms timeout
            if (select(sock_fd_ + 1, NULL, &write_fds, NULL, &tv) <= 0) {
                close(sock_fd_);
                sock_fd_ = -1;
                return false;
            }
        }

        // Set back to blocking for stable reads
        fcntl(sock_fd_, F_SETFL, flags);
        RCLCPP_INFO(this->get_logger(), "Connected to BMS Daemon.");
        return true;
    }

    void on_timer() {
        if (!try_connect()) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000, 
                                "BMS Daemon not reachable at %s", socket_path_.c_str());
            return;
        }

        bms::BatteryStatus s;
        ssize_t n = read(sock_fd_, &s, sizeof(s));
        
        if (n == sizeof(s)) {
            auto msg = sensor_msgs::msg::BatteryState();
            msg.header.stamp = this->now();
            msg.header.frame_id = frame_id_;
            msg.location = location_;
            
            msg.voltage = static_cast<float>(s.voltage);
            msg.current = static_cast<float>(s.current);
            msg.temperature = static_cast<float>(s.temperature);
            msg.percentage = static_cast<float>(s.percentage); // Already 0.0-1.0
            msg.charge = static_cast<float>(s.charge);
            msg.capacity = static_cast<float>(s.capacity);
            msg.design_capacity = static_cast<float>(s.design_capacity);
            
            // Map BMS work_state to ROS BatteryState constants
            // Assuming: 0:Idle, 1:Charge, 2:Discharge, 4:Full, 8:Protect
            if (s.work_state & 0x01) {
                msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_CHARGING;
            } else if (s.work_state & 0x02) {
                msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_DISCHARGING;
            } else if (s.work_state & 0x04) {
                msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_FULL;
            } else {
                msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_NOT_CHARGING;
            }

            msg.serial_number = std::string(s.serial_number);
            msg.present = true;
            msg.power_supply_technology = sensor_msgs::msg::BatteryState::POWER_SUPPLY_TECHNOLOGY_LION;
            
            // Map protect_status
            if (s.protect_status == 0) {
                msg.power_supply_health = sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_GOOD;
            } else {
                msg.power_supply_health = sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 10000, 
                                     "BMS Protection Active! Code: 0x%08X", s.protect_status);
            }

            // Optional: Populate cell voltages if data is valid
            if (s.max_cell_voltage > 0) {
                msg.cell_voltage.push_back(static_cast<float>(s.min_cell_voltage));
                msg.cell_voltage.push_back(static_cast<float>(s.max_cell_voltage));
            }

            pub_->publish(msg);
        } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
            RCLCPP_ERROR(this->get_logger(), "Connection to BMS Daemon lost.");
            close(sock_fd_);
            sock_fd_ = -1;
        }
    }
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<BmsRosBridge>());
    rclcpp::shutdown();
    return 0;
}
