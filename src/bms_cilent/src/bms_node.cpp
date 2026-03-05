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
    BmsRosBridge() : Node("bms_bridge"), sock_fd_(-1), last_print_time_(0) {
        // --- Declare Parameters (Matching bms_config.yaml) ---
        this->declare_parameter<std::string>("socket_path", "/tmp/bms.sock");
        this->declare_parameter<double>("publish_rate", 1.0);
        this->declare_parameter<std::string>("frame_id", "battery_link");
        this->declare_parameter<std::string>("location", "base_link");
        this->declare_parameter<double>("design_capacity", 6.0);
        
        // --- Diagnostic Thresholds ---
        this->declare_parameter<double>("over_temp_threshold", 55.0);
        this->declare_parameter<double>("low_soc_warn_threshold", 0.15);
        this->declare_parameter<double>("cell_gap_warn_threshold", 0.1);

        // Fetch initial values
        socket_path_ = this->get_parameter("socket_path").as_string();
        frame_id_ = this->get_parameter("frame_id").as_string();
        location_ = this->get_parameter("location").as_string();
        design_capacity_ = this->get_parameter("design_capacity").as_double();
        
        over_temp_threshold_ = this->get_parameter("over_temp_threshold").as_double();
        low_soc_threshold_ = this->get_parameter("low_soc_warn_threshold").as_double();
        cell_gap_threshold_ = this->get_parameter("cell_gap_warn_threshold").as_double();

        pub_ = this->create_publisher<sensor_msgs::msg::BatteryState>("battery_state", 10);
        
        double rate = this->get_parameter("publish_rate").as_double();
        auto period = std::chrono::duration<double>(1.0 / rate);
        timer_ = this->create_wall_timer(period, std::bind(&BmsRosBridge::on_timer, this));

        RCLCPP_INFO(this->get_logger(), "\033[1;32mBMS ROS Bridge Initialized. Monitoring thresholds: Gap > %.2fV, SoC < %.0f%%\033[0m", 
                    cell_gap_threshold_, low_soc_threshold_ * 100.0);
    }

    ~BmsRosBridge() {
        if (sock_fd_ >= 0) close(sock_fd_);
    }

private:
    int sock_fd_;
    std::string socket_path_;
    std::string frame_id_;
    std::string location_;
    double design_capacity_;
    double over_temp_threshold_;
    double low_soc_threshold_;
    double cell_gap_threshold_;
    long long last_print_time_;

    rclcpp::Publisher<sensor_msgs::msg::BatteryState>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;

    bool try_connect() {
        if (sock_fd_ >= 0) return true;
        sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd_ < 0) return false;
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        if (connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock_fd_); sock_fd_ = -1;
            return false;
        }
        RCLCPP_INFO(this->get_logger(), "\033[1;32mConnected to BMS Daemon.\033[0m");
        return true;
    }

    void on_timer() {
        if (!try_connect()) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000, "\033[1;31mDaemon NOT reachable at %s\033[0m", socket_path_.c_str());
            return;
        }

        bms::BatteryStatus s;
        if (read(sock_fd_, &s, sizeof(s)) == sizeof(s)) {
            auto msg = sensor_msgs::msg::BatteryState();
            msg.header.stamp = this->now();
            msg.header.frame_id = frame_id_;
            msg.location = location_;
            
            msg.voltage = s.voltage;
            msg.current = s.current;
            msg.temperature = s.temperature;
            msg.percentage = s.percentage;
            msg.charge = s.charge;
            msg.capacity = s.capacity;
            msg.design_capacity = design_capacity_;
            
            // --- 1. Cell Gap Monitoring ---
            float gap = s.max_cell_voltage - s.min_cell_voltage;
            if (gap > cell_gap_threshold_) {
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000, 
                    "\033[1;33m[警告] 电芯极差过大: %.3fV (阈值: %.2fV)\033[0m", gap, cell_gap_threshold_);
            }
            msg.cell_voltage = { (float)s.min_cell_voltage, (float)s.max_cell_voltage };

            // --- 2. SoC & Temp Monitoring ---
            if (s.percentage < low_soc_threshold_) {
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 10000, 
                    "\033[1;33m[警告] 电池电量低: %.1f%%\033[0m", s.percentage * 100.0);
            }
            if (s.temperature > over_temp_threshold_) {
                RCLCPP_ERROR(this->get_logger(), "\033[1;31m[严重] 电池过热: %.1f°C! 请立即关机检查。\033[0m", s.temperature);
            }

            // --- 3. 5s Status Overview ---
            auto now_ms = this->now().nanoseconds() / 1000000;
            if (now_ms - last_print_time_ > 5000) {
                std::cout << "\033[1;32m[BMS Alive] " << s.voltage << "V | " << s.current << "A | " 
                          << (s.percentage * 100.0) << "% | Gap: " << gap << "V\033[0m" << std::endl;
                last_print_time_ = now_ms;
            }

            // --- Status Mapping ---
            if (s.work_state & 0x01) msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_CHARGING;
            else if (s.work_state & 0x02) msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_DISCHARGING;
            else msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_NOT_CHARGING;

            msg.serial_number = std::string(s.serial_number);
            msg.present = true;
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
