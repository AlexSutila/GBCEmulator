#ifndef __DEBUG_PRINT_H
#define __DEBUG_PRINT_H

#include "cpu/interrupts.hpp"
#include "cpu/lr35902.hpp"
#include "gbc.hpp"
#include <string>

namespace Debug {

[[nodiscard]] std::string to_string(const LR35902::ProcessorState &s);
[[nodiscard]] std::string to_string(const InterruptBits &i);
[[nodiscard]] std::string to_string(const runtime_sys_info &r);

} // namespace Debug

#endif // __DEBUG_PRINT_H
