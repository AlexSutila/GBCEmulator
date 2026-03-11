#ifndef GBC_LOGGER_HPP
#define GBC_LOGGER_HPP

#pragma once
#include <mutex>
#include <string>
#include <vector>

enum class LogLevel { Debug, Info, Status, Warning, Error };

struct LogMessage {
  LogLevel level;
  std::string type;
  std::string summary;
  std::string message;
  time_t timestamp = std::time(nullptr);
};

class Logger {
public:
  // Called by ANY thread (CPU, PPU, Audio)
  static void push(const LogLevel level, const std::string &type, const std::string &summary,
                   const std::string &message) {
    std::lock_guard lock(m_mutex);
    m_queue.push_back({level, type, summary, message});
  }

  // Called ONLY by the GUI thread
  // Returns all pending messages and clears the internal queue
  static std::vector<LogMessage> consume() {
    std::lock_guard lock(m_mutex);
    if (m_queue.empty())
      return {};
    std::vector<LogMessage> result = std::move(m_queue);
    m_queue.clear();
    return result;
  }

private:
  static inline std::vector<LogMessage> m_queue;
  static inline std::mutex m_mutex;
};

#endif // GBC_LOGGER_HPP
