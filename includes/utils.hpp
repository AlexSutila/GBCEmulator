#ifndef GBC_UTILS_HPP
#define GBC_UTILS_HPP

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace IroGB {
inline void collect_args(std::ostringstream &oss) {}

template <typename T, typename... Args>
void collect_args(std::ostringstream &oss, T &&value, Args &&...args) {
  oss << value << '\0';
  collect_args(oss, std::forward<Args>(args)...);
}

template <typename... Args> std::string format(const std::string &fmt, Args &&...args) {
  std::ostringstream arg_stream{};
  collect_args(arg_stream, std::forward<Args>(args)...);

  std::string arg_data = arg_stream.str();
  std::vector<std::string> parsed_args{};

  std::size_t start = 0;
  while (true) {
    std::size_t pos = arg_data.find('\0', start);
    if (pos == std::string::npos)
      break;
    parsed_args.emplace_back(arg_data.substr(start, pos - start));
    start = pos + 1;
  }

  std::string result{};
  std::size_t arg_index = 0;

  for (std::size_t i{0}; i < fmt.size(); ++i) {
    if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      if (arg_index >= parsed_args.size()) [[unlikely]]
        throw std::runtime_error("Too few arguments for format");
      result += parsed_args[arg_index++];
      ++i;
    } else
      result += fmt[i];
  }

  if (arg_index != parsed_args.size()) [[unlikely]]
    throw std::runtime_error("Too many arguments for format");
  return result;
}

}; // namespace IroGB

#endif // GBC_UTILS_HPP
