#include "ws2812_controller.h"
#include <cstring>
#include <algorithm>
#include <cmath>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "pico/time.h"

// PIO program for WS2812 timing
// This generates the precise timing required for WS2812B LEDs
static const uint16_t ws2812_program[] = {
    //     .wrap_target
    0x6221,  //  0: out    x, 1            side 0 [2]
    0x1023,  //  1: jmp    !x, 3           side 1 [0]
    0x1400,  //  2: jmp    0               side 1 [4]
    0x4001,  //  3: nop                    side 0 [4]
    //     .wrap
};

static const struct pio_program ws2812_pio_program = {
    .instructions = ws2812_program,
    .length = 4,
    .origin = -1,
    .pio_version = 0,
};

// Static instance pointer for singleton
static WS2812Controller* ws2812_instance = nullptr;

WS2812Controller& WS2812Controller::instance() {
    static WS2812Controller controller;
    if (!controller.initialized) {
        controller.initialized = true;
        ws2812_instance = &controller;
        controller.init();
    }
    return controller;
}

void WS2812Controller::init() {
    // Clear all buffers
    for (auto& strip_buffer : led_buffers) {
        strip_buffer.fill(RGB(0, 0, 0));
    }
    
    // Initialize PIO and DMA
    init_pio();
    init_dma();
    
    // Initial clear of all strips
    clear_all();
}

void WS2812Controller::init_pio() {
    // Add the WS2812 program to PIO memory
    uint offset = pio_add_program(pio, &ws2812_pio_program);
    
    // Configure GPIO pins and state machines for each strip
    const uint pins[NUM_STRIPS] = {
        WS2812_PIN_STRIP_0,
        WS2812_PIN_STRIP_1
    };
    
    for (uint i = 0; i < NUM_STRIPS; i++) {
        // Claim a state machine
        sm[i] = pio_claim_unused_sm(pio, true);
        
        // Configure the GPIO pin
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);  // Set as output
        gpio_put(pins[i], 0);             // Set initial state to low
        pio_gpio_init(pio, pins[i]);      // Give control to PIO
        
        // Configure the state machine
        pio_sm_config config = pio_get_default_sm_config();
        
        // Set the pin direction
        pio_sm_set_consecutive_pindirs(pio, sm[i], pins[i], 1, true);
        
        // Configure side-set pins
        sm_config_set_sideset_pins(&config, pins[i]);
        
        // Configure the output shift register
        sm_config_set_out_shift(&config, false, true, 24);  // Shift left, autopull at 24 bits
        
        // Configure FIFO join - join RX to TX for deeper FIFO
        sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
        
        // Calculate clock divider for correct timing
        // WS2812B bit period = 1.25us, we have 5 instructions per bit
        float div = clock_get_hz(clk_sys) / (800000 * 5);  // 800kHz * 5 instructions
        sm_config_set_clkdiv(&config, div);
        
        // Set wrap points
        sm_config_set_wrap(&config, offset + 0, offset + 3);
        
        // Set side-set options
        sm_config_set_sideset(&config, 1, false, false);
        
        // Initialize the state machine
        pio_sm_init(pio, sm[i], offset, &config);
        
        // Enable the state machine
        pio_sm_set_enabled(pio, sm[i], true);
    }
}

void WS2812Controller::init_dma() {
    // Claim DMA channels for each strip
    for (uint i = 0; i < NUM_STRIPS; i++) {
        dma_channels[i] = dma_claim_unused_channel(true);
        
        // Configure DMA channel
        dma_channel_config dma_config = dma_channel_get_default_config(dma_channels[i]);
        
        // Transfer 32-bit words
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        
        // Increment read address, fixed write address (PIO FIFO)
        channel_config_set_read_increment(&dma_config, true);
        channel_config_set_write_increment(&dma_config, false);
        
        // Pace transfers based on PIO TX FIFO availability
        channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm[i], true));
        
        // Configure the channel
        dma_channel_configure(
            dma_channels[i],
            &dma_config,
            &pio->txf[sm[i]],           // Write to PIO TX FIFO
            dma_buffers[i].data(),      // Read from DMA buffer
            LEDS_PER_STRIP,             // Number of transfers
            false                       // Don't start yet
        );
    }
}

void WS2812Controller::update() {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Always update animations
    update_animations();
    
    // Check if it's time for a frame update
    if ((current_time - last_update_time) >= UPDATE_INTERVAL_MS) {
        last_update_time = current_time;
        
        // Always update all buffers
        for (uint i = 0; i < NUM_STRIPS; i++) {
            prepare_dma_buffer(i);
            trigger_dma_transfer(i);
            buffers_dirty[i] = false;
        }
    }
}

void WS2812Controller::prepare_dma_buffer(uint strip_index) {
    if (!is_strip_valid(strip_index)) return;
    
    // Convert RGB values to GRB format with brightness adjustment
    for (uint i = 0; i < LEDS_PER_STRIP; i++) {
        RGB adjusted = apply_brightness(led_buffers[strip_index][i]);
        dma_buffers[strip_index][i] = adjusted.to_grb() << 8u;  // Shift for PIO format
    }
}

void WS2812Controller::trigger_dma_transfer(uint strip_index) {
    if (!is_strip_valid(strip_index)) return;
    
    // Wait for any previous transfer to complete
    dma_channel_wait_for_finish_blocking(dma_channels[strip_index]);
    
    // Reset the transfer
    dma_channel_set_read_addr(dma_channels[strip_index], dma_buffers[strip_index].data(), false);
    dma_channel_set_trans_count(dma_channels[strip_index], LEDS_PER_STRIP, true);
}

void WS2812Controller::set_led(uint strip, uint led_index, const RGB& color) {
    if (!is_led_valid(strip, led_index)) return;
    
    led_buffers[strip][led_index] = color;
    buffers_dirty[strip] = true;
}

void WS2812Controller::set_led(uint strip, uint led_index, uint8_t r, uint8_t g, uint8_t b) {
    set_led(strip, led_index, RGB(r, g, b));
}

void WS2812Controller::set_strip(uint strip, const RGB& color) {
    if (!is_strip_valid(strip)) return;
    
    led_buffers[strip].fill(color);
    buffers_dirty[strip] = true;
}

void WS2812Controller::set_all(const RGB& color) {
    for (uint i = 0; i < NUM_STRIPS; i++) {
        set_strip(i, color);
    }
}

void WS2812Controller::clear_strip(uint strip) {
    set_strip(strip, RGB(0, 0, 0));
}

void WS2812Controller::clear_all() {
    set_all(RGB(0, 0, 0));
}

void WS2812Controller::set_brightness(float new_brightness) {
    brightness = std::max(0.0f, std::min(1.0f, new_brightness));
    
    // Mark all buffers as dirty to apply new brightness
    for (uint i = 0; i < NUM_STRIPS; i++) {
        buffers_dirty[i] = true;
    }
}

void WS2812Controller::set_range(uint strip, uint start_index, uint count, const RGB& color) {
    if (!is_strip_valid(strip)) return;
    
    uint end_index = std::min(start_index + count, (uint)LEDS_PER_STRIP);
    for (uint i = start_index; i < end_index; i++) {
        led_buffers[strip][i] = color;
    }
    buffers_dirty[strip] = true;
}

void WS2812Controller::set_gradient(uint strip, uint start_index, uint count, const RGB& start_color, const RGB& end_color) {
    if (!is_strip_valid(strip) || count == 0) return;
    
    uint end_index = std::min(start_index + count, (uint)LEDS_PER_STRIP);
    uint actual_count = end_index - start_index;
    
    for (uint i = 0; i < actual_count; i++) {
        float ratio = (float)i / (float)(actual_count - 1);
        RGB interpolated(
            start_color.r + (end_color.r - start_color.r) * ratio,
            start_color.g + (end_color.g - start_color.g) * ratio,
            start_color.b + (end_color.b - start_color.b) * ratio
        );
        led_buffers[strip][start_index + i] = interpolated;
    }
    buffers_dirty[strip] = true;
}

void WS2812Controller::set_animation(AnimationMode mode, uint32_t speed_ms) {
    current_animation = mode;
    animation_speed = speed_ms;
    animation_start_time = to_ms_since_boot(get_absolute_time());
}

void WS2812Controller::set_animation_colors(const RGB& primary, const RGB& secondary) {
    primary_color = primary;
    secondary_color = secondary;
}


RGB WS2812Controller::get_led(uint strip, uint led_index) const {
    if (!is_led_valid(strip, led_index)) return RGB(0, 0, 0);
    return led_buffers[strip][led_index];
}

RGB WS2812Controller::apply_brightness(const RGB& color) const {
    return RGB(
        color.r * brightness,
        color.g * brightness,
        color.b * brightness
    );
}

void WS2812Controller::update_animations() {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t elapsed_ms = current_time - animation_start_time;
    
    switch (current_animation) {
        case AnimationMode::STATIC:
            // For STATIC mode, mark all buffers as dirty to force continuous updates
            for (uint i = 0; i < NUM_STRIPS; i++) {
                buffers_dirty[i] = true;
            }
            break;
        case AnimationMode::RAINBOW:
            animate_rainbow(elapsed_ms);
            break;
        case AnimationMode::CHASE:
            animate_chase(elapsed_ms);
            break;
        case AnimationMode::PULSE:
            animate_pulse(elapsed_ms);
            break;
        case AnimationMode::SPARKLE:
            animate_sparkle(elapsed_ms);
            break;
        case AnimationMode::FADE:
            animate_fade(elapsed_ms);
            break;
        default:
            break;
    }
}

void WS2812Controller::animate_rainbow(uint32_t elapsed_ms) {
    uint32_t phase = (elapsed_ms / animation_speed) % 256;
    
    for (uint strip = 0; strip < NUM_STRIPS; strip++) {
        for (uint i = 0; i < LEDS_PER_STRIP; i++) {
            uint32_t hue = (phase + (i * 256 / LEDS_PER_STRIP)) & 0xFF;
            
            // Simple HSV to RGB conversion (S=1, V=1)
            uint8_t region = hue / 43;
            uint8_t remainder = (hue - (region * 43)) * 6;
            
            uint8_t p = 0;
            uint8_t q = (255 * (255 - remainder)) / 255;
            uint8_t t = (255 * remainder) / 255;
            
            switch (region) {
                case 0: led_buffers[strip][i] = RGB(255, t, p); break;
                case 1: led_buffers[strip][i] = RGB(q, 255, p); break;
                case 2: led_buffers[strip][i] = RGB(p, 255, t); break;
                case 3: led_buffers[strip][i] = RGB(p, q, 255); break;
                case 4: led_buffers[strip][i] = RGB(t, p, 255); break;
                default: led_buffers[strip][i] = RGB(255, p, q); break;
            }
        }
        buffers_dirty[strip] = true;
    }
}

void WS2812Controller::animate_chase(uint32_t elapsed_ms) {
    uint32_t position = (elapsed_ms / animation_speed) % LEDS_PER_STRIP;
    
    for (uint strip = 0; strip < NUM_STRIPS; strip++) {
        for (uint i = 0; i < LEDS_PER_STRIP; i++) {
            if (i == position || i == (position + 1) % LEDS_PER_STRIP) {
                led_buffers[strip][i] = primary_color;
            } else {
                led_buffers[strip][i] = secondary_color;
            }
        }
        buffers_dirty[strip] = true;
    }
}

void WS2812Controller::animate_pulse(uint32_t elapsed_ms) {
    float phase = (float)(elapsed_ms % (animation_speed * 2)) / (float)animation_speed;
    float intensity = (phase < 1.0f) ? phase : (2.0f - phase);
    
    RGB pulsed_color(
        primary_color.r * intensity,
        primary_color.g * intensity,
        primary_color.b * intensity
    );
    
    set_all(pulsed_color);
}

void WS2812Controller::animate_sparkle(uint32_t elapsed_ms) {
    // Random sparkle effect
    if ((elapsed_ms / animation_speed) % 2 == 0) {
        // Add random sparkles
        for (uint strip = 0; strip < NUM_STRIPS; strip++) {
            for (uint i = 0; i < 3; i++) {  // Add 3 sparkles per strip
                uint pos = rand() % LEDS_PER_STRIP;
                led_buffers[strip][pos] = primary_color;
            }
            buffers_dirty[strip] = true;
        }
    } else {
        // Fade all LEDs
        for (uint strip = 0; strip < NUM_STRIPS; strip++) {
            for (uint i = 0; i < LEDS_PER_STRIP; i++) {
                led_buffers[strip][i] = RGB(
                    led_buffers[strip][i].r * 0.9f,
                    led_buffers[strip][i].g * 0.9f,
                    led_buffers[strip][i].b * 0.9f
                );
            }
            buffers_dirty[strip] = true;
        }
    }
}

void WS2812Controller::animate_fade(uint32_t elapsed_ms) {
    float phase = (float)(elapsed_ms % (animation_speed * 2)) / (float)(animation_speed * 2);
    
    RGB faded_color;
    if (phase < 0.5f) {
        // Fade from primary to secondary
        float ratio = phase * 2.0f;
        faded_color = RGB(
            primary_color.r + (secondary_color.r - primary_color.r) * ratio,
            primary_color.g + (secondary_color.g - primary_color.g) * ratio,
            primary_color.b + (secondary_color.b - primary_color.b) * ratio
        );
    } else {
        // Fade from secondary to primary
        float ratio = (phase - 0.5f) * 2.0f;
        faded_color = RGB(
            secondary_color.r + (primary_color.r - secondary_color.r) * ratio,
            secondary_color.g + (primary_color.g - secondary_color.g) * ratio,
            secondary_color.b + (primary_color.b - secondary_color.b) * ratio
        );
    }
    
    set_all(faded_color);
}

