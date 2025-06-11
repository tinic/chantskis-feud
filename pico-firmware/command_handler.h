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
    static void cmd_start_timer(std::string_view args);
    static void cmd_stop_timer(std::string_view args);
    static void cmd_pause_timer(std::string_view args);
    static void cmd_resume_timer(std::string_view args);
    static void cmd_reset_game(std::string_view args);
    static void cmd_force_reset(std::string_view args);
    
    struct Command {
        std::string_view name;
        CommandFunction handler;
    };
    
    static constexpr std::array<Command, 9> commands{{
        {"hello", cmd_hello},
        {"status", cmd_status},
        {"help", cmd_help},
        {"start_timer", cmd_start_timer},
        {"stop_timer", cmd_stop_timer},
        {"pause_timer", cmd_pause_timer},
        {"resume_timer", cmd_resume_timer},
        {"reset_game", cmd_reset_game},
        {"force_reset", cmd_force_reset}
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