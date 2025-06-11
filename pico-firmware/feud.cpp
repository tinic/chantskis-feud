#include "feud.h"
#include "usb_serial.h"
#include "ws2812_controller.h"

#include <stdint.h>
#include <stdio.h>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

// Static instance pointer for callbacks
static Feud* feud_instance = nullptr;

Feud& Feud::instance() {
    static Feud feud;
    if (!feud.initialized) {
        feud.initialized = true;
        feud_instance = &feud;
        feud.init();
    }
    return feud;
}

void Feud::init() {
    btn_gpio_init();
    led_init();
}

void Feud::btn_gpio_init() {
    // Initialize button pins as inputs with pull-up resistors
    gpio_init(PLAYER_A_BUTTON_PIN);
    gpio_set_dir(PLAYER_A_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(PLAYER_A_BUTTON_PIN);
    
    gpio_init(PLAYER_B_BUTTON_PIN);
    gpio_set_dir(PLAYER_B_BUTTON_PIN, GPIO_IN);
    gpio_pull_up(PLAYER_B_BUTTON_PIN);
    
    // Set up interrupts for button presses (falling edge)
    // Clear any existing interrupts first
    gpio_set_irq_enabled(PLAYER_A_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, false);
    gpio_set_irq_enabled(PLAYER_B_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, false);
    
    // Set up interrupt callbacks
    gpio_set_irq_callback(&gpio_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);
    
    // Enable interrupts for both pins
    gpio_set_irq_enabled(PLAYER_A_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PLAYER_B_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true);
}

void Feud::led_init() {
    // Initialize LED pins as outputs
    gpio_init(PLAYER_A_LED_PIN);
    gpio_set_dir(PLAYER_A_LED_PIN, GPIO_OUT);
    gpio_put(PLAYER_A_LED_PIN, 0);
    
    gpio_init(PLAYER_B_LED_PIN);
    gpio_set_dir(PLAYER_B_LED_PIN, GPIO_OUT);
    gpio_put(PLAYER_B_LED_PIN, 0);
    
    // TODO: Initialize WS2812 LED strip for timer visualization
    // For now, we'll use a simple GPIO pin
    gpio_init(TIMER_LED_STRIP_PIN);
    gpio_set_dir(TIMER_LED_STRIP_PIN, GPIO_OUT);
    gpio_put(TIMER_LED_STRIP_PIN, 0);
}

void Feud::update() {
    update_timer();
    update_buttons();
    update_leds();
    
    // Send status updates at regular intervals when game is active
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    if (current_state != GameState::IDLE && 
        (current_time - last_status_time) >= STATUS_INTERVAL_MS) {
        send_status_directly();
        last_status_time = current_time;
    }
}

void Feud::update_timer() {
    if (current_state == GameState::TIMER_RUNNING) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        uint32_t elapsed = current_time - timer_start_time;
        
        if (elapsed >= timer_duration_ms) {
            // Timer expired naturally
            time_remaining = 0;
            timer_expired_naturally = true;
            current_state = GameState::IDLE;
            
            // Set all LED strips to red when timer expires
            WS2812Controller& ws2812 = WS2812Controller::instance();
            ws2812.stop_animation();  // Stop any running animation
            ws2812.set_all(Colors::RED);
            ws2812.force_update();
            
            send_status_directly(); // Send final status update
        } else {
            time_remaining = (timer_duration_ms - elapsed) / 1000; // Convert to seconds
        }
    }
    // TIMER_PAUSED state: time_remaining stays at paused value, no updates needed
}

void Feud::update_buttons() {
    // Button state is handled in interrupts
}

void Feud::update_leds() {
    switch (current_state) {
        case GameState::IDLE:
            gpio_put(PLAYER_A_LED_PIN, 0);
            gpio_put(PLAYER_B_LED_PIN, 0);
            gpio_put(TIMER_LED_STRIP_PIN, 0);
            break;
            
        case GameState::TIMER_RUNNING:
            // Flash both LEDs to indicate timer is running
            {
                uint32_t current_time = to_ms_since_boot(get_absolute_time());
                bool flash_on = (current_time / 250) % 2; // Flash every 250ms
                gpio_put(PLAYER_A_LED_PIN, flash_on ? 1 : 0);
                gpio_put(PLAYER_B_LED_PIN, flash_on ? 1 : 0);
                gpio_put(TIMER_LED_STRIP_PIN, 1); // Timer indicator on
            }
            break;
            
        case GameState::TIMER_PAUSED:
            // Slow flash to indicate paused
            {
                uint32_t current_time = to_ms_since_boot(get_absolute_time());
                bool flash_on = (current_time / 1000) % 2; // Flash every 1 second
                gpio_put(PLAYER_A_LED_PIN, flash_on ? 1 : 0);
                gpio_put(PLAYER_B_LED_PIN, flash_on ? 1 : 0);
                gpio_put(TIMER_LED_STRIP_PIN, flash_on ? 1 : 0);
            }
            break;
            
        case GameState::PLAYER_A_PRESSED:
            gpio_put(PLAYER_A_LED_PIN, 1);
            gpio_put(PLAYER_B_LED_PIN, 0);
            gpio_put(TIMER_LED_STRIP_PIN, 0);
            break;
            
        case GameState::PLAYER_B_PRESSED:
            gpio_put(PLAYER_A_LED_PIN, 0);
            gpio_put(PLAYER_B_LED_PIN, 1);
            gpio_put(TIMER_LED_STRIP_PIN, 0);
            break;
    }
}

void Feud::send_status_directly() {
    USBSerial& serial = USBSerial::instance();
    
    char status_msg[128];
    snprintf(status_msg, sizeof(status_msg), 
             "status: timer=%lu playera=%d playerb=%d active=%c expired=%d\n",
             time_remaining,
             player_a_pressed ? 1 : 0,
             player_b_pressed ? 1 : 0,
             get_active_player(),
             timer_expired_naturally ? 1 : 0);
    
    serial.send_data(reinterpret_cast<const uint8_t*>(status_msg), strlen(status_msg));
    
    // Clear the expired flag after sending
    timer_expired_naturally = false;
}

void Feud::gpio_callback(uint gpio, uint32_t events) {
    if (!feud_instance || !(events & GPIO_IRQ_EDGE_FALL)) return;
    
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if (gpio == PLAYER_A_BUTTON_PIN) {
        // Debounce check
        if (current_time - feud_instance->last_button_a_time > DEBOUNCE_MS) {
            feud_instance->last_button_a_time = current_time;
            
            if (feud_instance->current_state == GameState::TIMER_RUNNING) {
                // Pause the timer first
                feud_instance->pause_timer();
                // Then set player state
                feud_instance->player_a_pressed = true;
                feud_instance->current_state = GameState::PLAYER_A_PRESSED;
                
                // Set Player A LED strips (0 and 1) to cyan
                WS2812Controller& ws2812 = WS2812Controller::instance();
                ws2812.stop_animation();  // Stop any running animation
                ws2812.set_strip(0, Colors::CYAN);
                ws2812.set_strip(1, Colors::CYAN);
                ws2812.force_update();
                
                // Send final status update with player state
                feud_instance->send_status_directly();
            }
        }
    } else if (gpio == PLAYER_B_BUTTON_PIN) {
        // Debounce check
        if (current_time - feud_instance->last_button_b_time > DEBOUNCE_MS) {
            feud_instance->last_button_b_time = current_time;
            
            if (feud_instance->current_state == GameState::TIMER_RUNNING) {
                // Pause the timer first
                feud_instance->pause_timer();
                // Then set player state
                feud_instance->player_b_pressed = true;
                feud_instance->current_state = GameState::PLAYER_B_PRESSED;
                
                // Set Player B LED strips (2 and 3) to cyan
                WS2812Controller& ws2812 = WS2812Controller::instance();
                ws2812.stop_animation();  // Stop any running animation
                ws2812.set_strip(2, Colors::CYAN);
                ws2812.set_strip(3, Colors::CYAN);
                ws2812.force_update();
                
                // Send final status update with player state
                feud_instance->send_status_directly();
            }
        }
    }
}

void Feud::start_timer(uint32_t duration_seconds) {
    timer_duration_ms = duration_seconds * 1000;
    timer_start_time = to_ms_since_boot(get_absolute_time());
    time_remaining = duration_seconds;
    current_state = GameState::TIMER_RUNNING;
    player_a_pressed = false;
    player_b_pressed = false;
    timer_expired_naturally = false; // Reset expiration flag
    
    // Set all LED strips to black when timer starts
    WS2812Controller& ws2812 = WS2812Controller::instance();
    ws2812.stop_animation();  // Stop any running animation
    ws2812.clear_all();
    ws2812.force_update();
    
    // Send initial status immediately
    send_status_directly();
    last_status_time = to_ms_since_boot(get_absolute_time());
}

void Feud::stop_timer() {
    current_state = GameState::IDLE;
    time_remaining = 0;
    paused_time_remaining = 0;
    timer_expired_naturally = false; // Ensure manual stop doesn't trigger expiration
    
    // Send final status update
    send_status_directly();
}

void Feud::pause_timer() {
    if (current_state == GameState::TIMER_RUNNING) {
        // Calculate remaining time when pausing
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        uint32_t elapsed = current_time - timer_start_time;
        
        if (elapsed < timer_duration_ms) {
            paused_time_remaining = (timer_duration_ms - elapsed) / 1000;
        } else {
            paused_time_remaining = 0;
        }
        
        current_state = GameState::TIMER_PAUSED;
        time_remaining = paused_time_remaining;
        
        // Send status update
        send_status_directly();
    }
}

void Feud::resume_timer() {
    // Allow resume from TIMER_PAUSED or PLAYER_PRESSED states
    if ((current_state == GameState::TIMER_PAUSED || 
         current_state == GameState::PLAYER_A_PRESSED ||
         current_state == GameState::PLAYER_B_PRESSED) && 
        paused_time_remaining > 0) {
        
        // Resume with remaining time
        timer_duration_ms = paused_time_remaining * 1000;
        timer_start_time = to_ms_since_boot(get_absolute_time());
        time_remaining = paused_time_remaining;
        current_state = GameState::TIMER_RUNNING;
        
        // Clear player pressed states when resuming
        player_a_pressed = false;
        player_b_pressed = false;
        
        // Send status update
        send_status_directly();
    }
}

void Feud::reset_game() {
    current_state = GameState::IDLE;
    timer_duration_ms = 0;
    timer_start_time = 0;
    time_remaining = 0;
    paused_time_remaining = 0;
    player_a_pressed = false;
    player_b_pressed = false;
    timer_expired_naturally = false; // Reset expiration flag
    
    // Clear all LED strips on reset and restart rainbow animation
    WS2812Controller& ws2812 = WS2812Controller::instance();
    ws2812.clear_all();
    ws2812.force_update();
    // Restart rainbow animation after reset
    ws2812.set_animation(AnimationMode::RAINBOW, 100);
    
    // Send reset status update
    send_status_directly();
}

void Feud::force_reset() {
    // Complete system reset including timing
    current_state = GameState::IDLE;
    timer_duration_ms = 0;
    timer_start_time = 0;
    time_remaining = 0;
    paused_time_remaining = 0;
    player_a_pressed = false;
    player_b_pressed = false;
    timer_expired_naturally = false; // Reset expiration flag
    last_status_time = 0;
    last_button_a_time = 0;
    last_button_b_time = 0;
    
    // Reset all LEDs
    gpio_put(PLAYER_A_LED_PIN, 0);
    gpio_put(PLAYER_B_LED_PIN, 0);
    gpio_put(TIMER_LED_STRIP_PIN, 0);
    
    // Clear all LED strips on force reset and restart rainbow animation
    WS2812Controller& ws2812 = WS2812Controller::instance();
    ws2812.clear_all();
    ws2812.force_update();
    // Restart rainbow animation after force reset
    ws2812.set_animation(AnimationMode::RAINBOW, 100);
    
    // Send reset status update
    send_status_directly();
}

char Feud::get_active_player() const {
    switch (current_state) {
        case GameState::PLAYER_A_PRESSED:
            return 'A';
        case GameState::PLAYER_B_PRESSED:
            return 'B';
        default:
            return 'N'; // None
    }
}
