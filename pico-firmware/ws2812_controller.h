#ifndef WS2812_CONTROLLER_H
#define WS2812_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>
#include <array>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

// WS2812B Configuration
constexpr uint NUM_STRIPS = 2;
constexpr uint LEDS_PER_STRIP = 60;  // Adjust based on your actual strip length
constexpr uint TOTAL_LEDS = NUM_STRIPS * LEDS_PER_STRIP;

// GPIO pin definitions for WS2812B strips
constexpr uint WS2812_PIN_STRIP_0 = 7;   // First strip
constexpr uint WS2812_PIN_STRIP_1 = 6;   // Second strip (pin 7 used for PLAYER_B_LED, pin 8 for level shifter)

// Timing constants for WS2812B (in nanoseconds)
constexpr uint32_t WS2812_T0H_NS = 400;
constexpr uint32_t WS2812_T0L_NS = 850;
constexpr uint32_t WS2812_T1H_NS = 800;
constexpr uint32_t WS2812_T1L_NS = 450;
constexpr uint32_t WS2812_RESET_NS = 50000;  // 50us reset

// Color structure for RGB values
struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    
    constexpr RGB() : r(0), g(0), b(0) {}
    constexpr RGB(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    
    // Convert to WS2812B format (GRB)
    constexpr uint32_t to_grb() const {
        return ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
    }
};

// Animation modes for LED effects
enum class AnimationMode {
    STATIC,
    FADE,
    RAINBOW,
    CHASE,
    PULSE,
    SPARKLE
};

class WS2812Controller {
private:
    bool initialized = false;
    
    // PIO and DMA resources
    PIO pio = pio0;
    uint sm[NUM_STRIPS];  // State machines for each strip
    int dma_channels[NUM_STRIPS];  // DMA channels for each strip
    
    // LED buffers - one per strip
    std::array<std::array<RGB, LEDS_PER_STRIP>, NUM_STRIPS> led_buffers;
    std::array<std::array<uint32_t, LEDS_PER_STRIP>, NUM_STRIPS> dma_buffers;
    
    // Animation state
    AnimationMode current_animation = AnimationMode::STATIC;
    uint32_t animation_start_time = 0;
    uint32_t animation_speed = 100;  // ms per animation step
    RGB primary_color = RGB(0, 0, 0);
    RGB secondary_color = RGB(0, 0, 0);
    float brightness = 1.0f;
    
    // Update tracking
    bool buffers_dirty[NUM_STRIPS] = {false, false};
    uint32_t last_update_time = 0;
    static constexpr uint32_t UPDATE_INTERVAL_MS = 16;  // ~60 FPS
    
    // Private methods
    void init();
    void init_pio();
    void init_dma();
    void prepare_dma_buffer(uint strip_index);
    void trigger_dma_transfer(uint strip_index);
    void update_animations();
    RGB apply_brightness(const RGB& color) const;
    
    // Animation helpers
    void animate_rainbow(uint32_t elapsed_ms);
    void animate_chase(uint32_t elapsed_ms);
    void animate_pulse(uint32_t elapsed_ms);
    void animate_sparkle(uint32_t elapsed_ms);
    void animate_fade(uint32_t elapsed_ms);
    
public:
    static WS2812Controller& instance();
    void update();
    
    // Basic LED control
    void set_led(uint strip, uint led_index, const RGB& color);
    void set_led(uint strip, uint led_index, uint8_t r, uint8_t g, uint8_t b);
    void set_strip(uint strip, const RGB& color);
    void set_all(const RGB& color);
    void clear_strip(uint strip);
    void clear_all();
    
    // Advanced control
    void set_brightness(float brightness);  // 0.0 to 1.0
    void set_range(uint strip, uint start_index, uint count, const RGB& color);
    void set_gradient(uint strip, uint start_index, uint count, const RGB& start_color, const RGB& end_color);
    
    // Animation control
    void set_animation(AnimationMode mode, uint32_t speed_ms = 10);
    void set_animation_colors(const RGB& primary, const RGB& secondary);
    
    // Status getters
    AnimationMode get_animation_mode() const { return current_animation; }
    float get_brightness() const { return brightness; }
    RGB get_led(uint strip, uint led_index) const;
    
    // Utility methods
    bool is_strip_valid(uint strip) const { return strip < NUM_STRIPS; }
    bool is_led_valid(uint strip, uint led_index) const { 
        return is_strip_valid(strip) && led_index < LEDS_PER_STRIP; 
    }
    
};

// Predefined colors for convenience
namespace Colors {
    constexpr RGB BLACK(0, 0, 0);
    constexpr RGB WHITE(255, 255, 255);
    constexpr RGB RED(255, 0, 0);
    constexpr RGB GREEN(0, 255, 0);
    constexpr RGB BLUE(0, 0, 255);
    constexpr RGB YELLOW(255, 255, 0);
    constexpr RGB CYAN(0, 255, 255);
    constexpr RGB MAGENTA(255, 0, 255);
    constexpr RGB ORANGE(255, 128, 0);
    constexpr RGB PURPLE(128, 0, 255);
    constexpr RGB PINK(255, 192, 203);
}

#endif  // WS2812_CONTROLLER_H