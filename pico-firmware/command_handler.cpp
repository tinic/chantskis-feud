#include "command_handler.h"
#include "usb_serial.h"
#include "feud.h"
#include <cstring>
#include <cctype>
#include <algorithm>
#include <ranges>
#include <bit>
#include <cstdio>

using namespace std::literals;

CommandHandler& CommandHandler::instance() {
    static CommandHandler handler;
    if (!handler.initialized) {
        handler.initialized = true;
        handler.init();
    }
    return handler;
}

void CommandHandler::init() {
}

void CommandHandler::handle_line(std::string_view line) {
    USBSerial& serial = USBSerial::instance();
    
    constexpr std::string_view prefix = "Received: ";
    serial.send_data(reinterpret_cast<const uint8_t*>(prefix.data()), prefix.size());
    serial.send_data(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    constexpr std::string_view newline = "\n";
    serial.send_data(reinterpret_cast<const uint8_t*>(newline.data()), newline.size());
    
    auto parsed = parse_command_line(line);
    if (!parsed) {
        return;
    }
    
    auto [command, args] = *parsed;
    
    auto cmd_it = std::ranges::find_if(commands, [this, command](const Command& cmd) {
        return str_equal_case_insensitive(cmd.name, command);
    });
    
    if (cmd_it != commands.end()) {
        cmd_it->handler(args);
    } else {
        constexpr std::string_view error_msg = "Unknown command: ";
        constexpr std::string_view help_msg = "\nType 'help' for available commands\n";
        
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        serial.send_data(reinterpret_cast<const uint8_t*>(command.data()), command.size());
        serial.send_data(reinterpret_cast<const uint8_t*>(help_msg.data()), help_msg.size());
    }
}


void CommandHandler::cmd_hello(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    
    if (args.empty()) {
        constexpr std::string_view msg = "Hello from Chantskis Feud!\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    } else {
        constexpr std::string_view prefix = "Hello, ";
        constexpr std::string_view suffix = "!\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(prefix.data()), prefix.size());
        serial.send_data(reinterpret_cast<const uint8_t*>(args.data()), args.size());
        serial.send_data(reinterpret_cast<const uint8_t*>(suffix.data()), suffix.size());
    }
}

void CommandHandler::cmd_status([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    char status_msg[256];
    const char* state_str;
    
    switch (feud.get_state()) {
        case GameState::IDLE:
            state_str = "idle";
            break;
        case GameState::TIMER_RUNNING:
            state_str = "timer_running";
            break;
        case GameState::TIMER_PAUSED:
            state_str = "timer_paused";
            break;
        case GameState::PLAYER_A_PRESSED:
            state_str = "player_a_pressed";
            break;
        case GameState::PLAYER_B_PRESSED:
            state_str = "player_b_pressed";
            break;
        default:
            state_str = "unknown";
    }
    
    snprintf(status_msg, sizeof(status_msg),
             "System Status: OK\n"
             "USB Serial: Connected\n"
             "Game State: %s\n"
             "Timer: %lu seconds\n"
             "Player A: %s\n"
             "Player B: %s\n"
             "Active Player: %c\n",
             state_str,
             feud.get_time_remaining(),
             feud.is_player_a_pressed() ? "PRESSED" : "Ready",
             feud.is_player_b_pressed() ? "PRESSED" : "Ready",
             feud.get_active_player());
    
    serial.send_data(reinterpret_cast<const uint8_t*>(status_msg), strlen(status_msg));
}

void CommandHandler::cmd_help([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    
    constexpr std::array help_lines = {
        std::string_view{"Available commands:\n"},
        std::string_view{"  hello [name]       - Say hello\n"},
        std::string_view{"  status             - Get system status\n"},
        std::string_view{"  start_timer <sec>  - Start game timer\n"},
        std::string_view{"  stop_timer         - Stop game timer\n"},
        std::string_view{"  pause_timer        - Pause running timer\n"},
        std::string_view{"  resume_timer       - Resume paused timer\n"},
        std::string_view{"  reset_game         - Reset game state\n"},
        std::string_view{"  force_reset        - Complete system reset\n"},
        std::string_view{"  help               - Show this help\n"}
    };
    
    for (const auto& line : help_lines) {
        serial.send_data(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    }
}

void CommandHandler::cmd_start_timer(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    if (args.empty()) {
        constexpr std::string_view error_msg = "Error: start_timer requires duration in seconds\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    // Parse duration from args
    uint32_t duration = 0;
    for (char c : args) {
        if (c >= '0' && c <= '9') {
            duration = duration * 10 + (c - '0');
        } else if (c == ' ' || c == '\t') {
            break; // Stop at first space
        } else {
            constexpr std::string_view error_msg = "Error: Invalid duration format\n";
            serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
            return;
        }
    }
    
    if (duration == 0 || duration > 300) {
        constexpr std::string_view error_msg = "Error: Duration must be between 1 and 300 seconds\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    feud.start_timer(duration);
    
    char response[64];
    snprintf(response, sizeof(response), "Timer started for %lu seconds\n", duration);
    serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
}

void CommandHandler::cmd_stop_timer([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    feud.stop_timer();
    
    constexpr std::string_view response = "Timer stopped\n";
    serial.send_data(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void CommandHandler::cmd_pause_timer([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    feud.pause_timer();
    
    constexpr std::string_view response = "Timer paused\n";
    serial.send_data(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void CommandHandler::cmd_resume_timer([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    feud.resume_timer();
    
    constexpr std::string_view response = "Timer resumed\n";
    serial.send_data(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void CommandHandler::cmd_reset_game([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    feud.reset_game();
    
    constexpr std::string_view response = "Game reset\n";
    serial.send_data(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

void CommandHandler::cmd_force_reset([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    Feud& feud = Feud::instance();
    
    feud.force_reset();
    
    constexpr std::string_view response = "System force reset complete\n";
    serial.send_data(reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

constexpr bool CommandHandler::str_equal_case_insensitive(std::string_view a, std::string_view b) const noexcept {
    return std::ranges::equal(a, b, [](char ca, char cb) {
        return std::tolower(ca) == std::tolower(cb);
    });
}


std::string_view CommandHandler::trim_whitespace(std::string_view str) const noexcept {
    constexpr std::string_view whitespace = " \t\r\n";
    
    const auto start = str.find_first_not_of(whitespace);
    if (start == std::string_view::npos) {
        return {};
    }
    
    const auto end = str.find_last_not_of(whitespace);
    
    return str.substr(start, end - start + 1);
}

std::optional<std::pair<std::string_view, std::string_view>> 
CommandHandler::parse_command_line(std::string_view line) const noexcept {
    line = trim_whitespace(line);
    
    if (line.empty()) {
        return std::nullopt;
    }
    
    const auto space_pos = line.find(' ');
    
    if (space_pos == std::string_view::npos) {
        return std::make_pair(line, std::string_view{});
    }
    
    auto command = line.substr(0, space_pos);
    auto args = trim_whitespace(line.substr(space_pos + 1));
    
    return std::make_pair(command, args);
}
