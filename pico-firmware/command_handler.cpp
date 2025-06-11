#include "command_handler.h"
#include "usb_serial.h"
#include "feud.h"
#include <cstring>
#include <cctype>
#include <algorithm>
#include <ranges>
#include <bit>

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
    
    constexpr std::array status_lines = {
        std::string_view{"System Status: OK\n"},
        std::string_view{"USB Serial: Connected\n"},
        std::string_view{"Game: Ready\n"}
    };
    
    for (const auto& line : status_lines) {
        serial.send_data(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    }
}

void CommandHandler::cmd_help([[maybe_unused]] std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    
    constexpr std::array help_lines = {
        std::string_view{"Available commands:\n"},
        std::string_view{"  hello [name]  - Say hello\n"},
        std::string_view{"  status        - Get system status\n"},
        std::string_view{"  help          - Show this help\n"}
    };
    
    for (const auto& line : help_lines) {
        serial.send_data(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    }
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
