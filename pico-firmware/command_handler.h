#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <cstring>
#include <cstddef>
#include <array>
#include <string_view>
#include <optional>

class CommandHandler {
 private:
    bool initialized = false;
    
    using CommandFunction = void (*)(std::string_view args);
    
    static void cmd_hello(std::string_view args);
    static void cmd_status(std::string_view args);
    static void cmd_help(std::string_view args);
    
    struct Command {
        std::string_view name;
        CommandFunction handler;
    };
    
    static constexpr std::array<Command, 3> commands{{
        {"hello", cmd_hello},
        {"status", cmd_status},
        {"help", cmd_help}
    }};
    
    void init();
    
    [[nodiscard]] constexpr bool str_equal_case_insensitive(std::string_view a, std::string_view b) const noexcept;
    [[nodiscard]] std::string_view trim_whitespace(std::string_view str) const noexcept;
    [[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> parse_command_line(std::string_view line) const noexcept;
    
 public:
    static CommandHandler& instance();
    
    void handle_line(std::string_view line);
};

#endif  // COMMAND_HANDLER_H