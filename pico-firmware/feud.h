#ifndef FEUD_H
#define FEUD_H

#include <stddef.h>
#include <stdint.h>
#include <array>

#include "hardware/dma.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

// GPIO pin definitions
constexpr uint PLAYER_A_BUTTON_PIN = 29;
constexpr uint PLAYER_B_BUTTON_PIN = 28;
constexpr uint PLAYER_A_LED_PIN = 2;
constexpr uint PLAYER_B_LED_PIN = 3;  // Pin 8 is used for level shifter enable

enum class GameState {
    IDLE,
    TIMER_RUNNING,
    TIMER_PAUSED,
    PLAYER_A_PRESSED,
    PLAYER_B_PRESSED
};

enum class MessageType {
    STATUS_UPDATE,
    BUTTON_PRESS,
    TIMER_EXPIRED
};

struct GameMessage {
    MessageType type;
    char data[64];  // Fixed size buffer
};

template<typename T, size_t Size>
class CircularBuffer {
private:
    std::array<T, Size> buffer;
    size_t head = 0;
    size_t tail = 0;
    bool full = false;

public:
    bool push(const T& item) {
        if (full) return false;  // Buffer full
        
        buffer[head] = item;
        head = (head + 1) % Size;
        
        if (head == tail) {
            full = true;
        }
        return true;
    }
    
    bool pop(T& item) {
        if (empty()) return false;
        
        item = buffer[tail];
        tail = (tail + 1) % Size;
        full = false;
        return true;
    }
    
    bool empty() const {
        return (!full && (head == tail));
    }
};

class Feud {
 private:
    bool initialized = false;
    GameState current_state = GameState::IDLE;
    uint32_t timer_duration_ms = 0;
    uint32_t timer_start_time = 0;
    uint32_t time_remaining = 0;
    uint32_t paused_time_remaining = 0; // Time remaining when paused
    bool player_a_pressed = false;
    bool player_b_pressed = false;
    bool timer_expired_naturally = false; // Flag to track natural timer expiration
    
volatile bool debounce_a_pending = false;
volatile bool debounce_b_pending = false;
    // Debouncing
    uint32_t last_button_a_time = 0;
    uint32_t last_button_b_time = 0;
    static constexpr uint32_t DEBOUNCE_MS = 50;
    
    // Message buffer for asynchronous communication
    CircularBuffer<GameMessage, 8> message_buffer;
    uint32_t last_status_time = 0;
    static constexpr uint32_t STATUS_INTERVAL_MS = 100;
    
    void btn_gpio_init();
    void led_init();
    void init();
    void update_timer();
    void update_buttons();
    void update_leds();
    void send_status_directly();
    
    static void gpio_callback(uint gpio, uint32_t events);
    
 public:
    static Feud& instance();
    void update();
    
    // Game control methods
    void start_timer(uint32_t duration_seconds);
    void stop_timer();
    void pause_timer();
    void resume_timer();
    void reset_game();
    void force_reset(); // Complete system reset
    
    // Status getters
    GameState get_state() const { return current_state; }
    uint32_t get_time_remaining() const { return time_remaining; }
    bool is_player_a_pressed() const { return player_a_pressed; }
    bool is_player_b_pressed() const { return player_b_pressed; }
    char get_active_player() const;
};

#endif  // FEUD_H
