#include "command_handler.h"
#include "usb_serial.h"
#include "feud.h"
#include "ws2812_controller.h"
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
    
    auto cmd_it = std::ranges::find_if(commands, [command](const Command& cmd) {
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
        std::string_view{"  led_set <strip> <led> <r> <g> <b> - Set single LED\n"},
        std::string_view{"  led_strip <strip> <r> <g> <b>     - Set entire strip\n"},
        std::string_view{"  led_all <r> <g> <b>                - Set all LEDs\n"},
        std::string_view{"  led_clear [strip]                  - Clear LEDs\n"},
        std::string_view{"  led_animate <mode> [speed]         - Set animation\n"},
        std::string_view{"  led_brightness <0-100>             - Set brightness\n"},
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

constexpr bool CommandHandler::str_equal_case_insensitive(std::string_view a, std::string_view b) noexcept {
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

void CommandHandler::cmd_led_set(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    // Parse: strip led r g b
    uint32_t strip = 0, led = 0, r = 0, g = 0, b = 0;
    int parsed = 0;
    
    // Simple parsing - extract 5 numbers
    const char* ptr = args.data();
    const char* end = ptr + args.size();
    uint32_t* values[] = {&strip, &led, &r, &g, &b};
    
    for (int i = 0; i < 5 && ptr < end; i++) {
        // Skip whitespace
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ptr++;
        if (ptr >= end) break;
        
        // Parse number
        uint32_t val = 0;
        bool found_digit = false;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            val = val * 10 + (*ptr - '0');
            ptr++;
            found_digit = true;
        }
        
        if (found_digit) {
            *values[i] = val;
            parsed++;
        }
    }
    
    if (parsed != 5) {
        constexpr std::string_view error_msg = "Error: led_set requires: strip led r g b\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    if (!ws2812.is_led_valid(strip, led)) {
        constexpr std::string_view error_msg = "Error: Invalid strip or LED index\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    if (r > 255 || g > 255 || b > 255) {
        constexpr std::string_view error_msg = "Error: RGB values must be 0-255\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    ws2812.set_led(strip, led, r, g, b);
    ws2812.set_animation(AnimationMode::STATIC);
    
    char response[64];
    snprintf(response, sizeof(response), "LED set: strip %lu, led %lu = (%lu,%lu,%lu)\n", 
             strip, led, r, g, b);
    serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
}

void CommandHandler::cmd_led_strip(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    // Parse: strip r g b
    uint32_t strip = 0, r = 0, g = 0, b = 0;
    int parsed = 0;
    
    const char* ptr = args.data();
    const char* end = ptr + args.size();
    uint32_t* values[] = {&strip, &r, &g, &b};
    
    for (int i = 0; i < 4 && ptr < end; i++) {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ptr++;
        if (ptr >= end) break;
        
        uint32_t val = 0;
        bool found_digit = false;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            val = val * 10 + (*ptr - '0');
            ptr++;
            found_digit = true;
        }
        
        if (found_digit) {
            *values[i] = val;
            parsed++;
        }
    }
    
    if (parsed != 4) {
        constexpr std::string_view error_msg = "Error: led_strip requires: strip r g b\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    if (!ws2812.is_strip_valid(strip)) {
        constexpr std::string_view error_msg = "Error: Invalid strip index\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    if (r > 255 || g > 255 || b > 255) {
        constexpr std::string_view error_msg = "Error: RGB values must be 0-255\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    ws2812.set_strip(strip, RGB(r, g, b));
    ws2812.set_animation(AnimationMode::STATIC);
    
    char response[64];
    snprintf(response, sizeof(response), "Strip %lu set to (%lu,%lu,%lu)\n", strip, r, g, b);
    serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
}

void CommandHandler::cmd_led_all(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    // Parse: r g b
    uint32_t r = 0, g = 0, b = 0;
    int parsed = 0;
    
    const char* ptr = args.data();
    const char* end = ptr + args.size();
    uint32_t* values[] = {&r, &g, &b};
    
    for (int i = 0; i < 3 && ptr < end; i++) {
        while (ptr < end && (*ptr == ' ' || *ptr == '\t')) ptr++;
        if (ptr >= end) break;
        
        uint32_t val = 0;
        bool found_digit = false;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            val = val * 10 + (*ptr - '0');
            ptr++;
            found_digit = true;
        }
        
        if (found_digit) {
            *values[i] = val;
            parsed++;
        }
    }
    
    if (parsed != 3) {
        constexpr std::string_view error_msg = "Error: led_all requires: r g b\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    if (r > 255 || g > 255 || b > 255) {
        constexpr std::string_view error_msg = "Error: RGB values must be 0-255\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    ws2812.set_all(RGB(r, g, b));
    ws2812.set_animation(AnimationMode::STATIC);
    
    char response[64];
    snprintf(response, sizeof(response), "All LEDs set to (%lu,%lu,%lu)\n", r, g, b);
    serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
}

void CommandHandler::cmd_led_clear(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    if (args.empty()) {
        // Clear all
        ws2812.clear_all();
        ws2812.set_animation(AnimationMode::STATIC);
        constexpr std::string_view response = "All LEDs cleared\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(response.data()), response.size());
    } else {
        // Parse strip number
        uint32_t strip = 0;
        for (char c : args) {
            if (c >= '0' && c <= '9') {
                strip = strip * 10 + (c - '0');
            } else if (c == ' ' || c == '\t') {
                break;
            }
        }
        
        if (!ws2812.is_strip_valid(strip)) {
            constexpr std::string_view error_msg = "Error: Invalid strip index\n";
            serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
            return;
        }
        
        ws2812.clear_strip(strip);
        ws2812.set_animation(AnimationMode::STATIC);
        
        char response[64];
        snprintf(response, sizeof(response), "Strip %lu cleared\n", strip);
        serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
    }
}

void CommandHandler::cmd_led_animate(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    // Parse animation mode and optional speed
    auto space_pos = args.find(' ');
    std::string_view mode_str = (space_pos != std::string_view::npos) 
        ? args.substr(0, space_pos) 
        : args;
    
    AnimationMode mode = AnimationMode::STATIC;
    if (str_equal_case_insensitive(mode_str, "static")) {
        mode = AnimationMode::STATIC;
    } else if (str_equal_case_insensitive(mode_str, "fade")) {
        mode = AnimationMode::FADE;
    } else if (str_equal_case_insensitive(mode_str, "rainbow")) {
        mode = AnimationMode::RAINBOW;
    } else if (str_equal_case_insensitive(mode_str, "chase")) {
        mode = AnimationMode::CHASE;
    } else if (str_equal_case_insensitive(mode_str, "pulse")) {
        mode = AnimationMode::PULSE;
    } else if (str_equal_case_insensitive(mode_str, "sparkle")) {
        mode = AnimationMode::SPARKLE;
    } else {
        constexpr std::string_view error_msg = "Error: Invalid animation mode. Use: static, fade, rainbow, chase, pulse, sparkle\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    // Parse optional speed
    uint32_t speed = 100;  // Default speed
    if (space_pos != std::string_view::npos) {
        std::string_view speed_str = args.substr(space_pos + 1);
        for (char c : speed_str) {
            if (c >= '0' && c <= '9') {
                speed = speed * 10 + (c - '0');
            } else if (c == ' ' || c == '\t') {
                break;
            }
        }
    }
    
    ws2812.set_animation(mode, speed);
    char response[64];
    snprintf(response, sizeof(response), "Animation set to %.*s (speed: %lums)\n", 
             (int)mode_str.size(), mode_str.data(), speed);
    serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
}

void CommandHandler::cmd_led_brightness(std::string_view args) {
    USBSerial& serial = USBSerial::instance();
    WS2812Controller& ws2812 = WS2812Controller::instance();
    
    if (args.empty()) {
        constexpr std::string_view error_msg = "Error: led_brightness requires brightness value (0-100)\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    // Parse brightness percentage
    uint32_t brightness = 0;
    for (char c : args) {
        if (c >= '0' && c <= '9') {
            brightness = brightness * 10 + (c - '0');
        } else if (c == ' ' || c == '\t') {
            break;
        } else {
            constexpr std::string_view error_msg = "Error: Invalid brightness format\n";
            serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
            return;
        }
    }
    
    if (brightness > 100) {
        constexpr std::string_view error_msg = "Error: Brightness must be 0-100\n";
        serial.send_data(reinterpret_cast<const uint8_t*>(error_msg.data()), error_msg.size());
        return;
    }
    
    ws2812.set_brightness(brightness / 100.0f);
    
    char response[64];
    snprintf(response, sizeof(response), "Brightness set to %lu%%\n", brightness);
    serial.send_data(reinterpret_cast<const uint8_t*>(response), strlen(response));
}
