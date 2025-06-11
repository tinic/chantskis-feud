#ifndef SEENSAY_H
#define SEENSAY_H

#include <stddef.h>
#include <stdint.h>

#include <array>

#include "hardware/dma.h"

class Feud {
 private:
    bool initialized = false;
    void btn_gpio_init();
    void init();
 public:
    static Feud& instance();
    void update();
};

#endif  // SEENSAY_H
