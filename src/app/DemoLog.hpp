#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace forgechain::demo {

class DemoLog {
public:
  explicit DemoLog(std::string node_name) : node_name_(std::move(node_name)) {}

  void log(const std::string &category, const std::string &message) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "."
               << std::setfill('0') << std::setw(3) << ms.count() << "] ["
               << node_name_ << "] [" << category << "] " << message
               << std::endl;
  }

  void log(const std::string &message) { log("INFO", message); }

private:
  std::string node_name_;
  std::mutex mutex_;
};

}
