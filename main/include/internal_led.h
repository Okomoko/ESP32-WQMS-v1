#ifndef INTERNAL_LED_H
#define INTERNAL_LED_H

#include <stdint.h>

/**
 * @brief Structure to represent an RGB color
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

/**
 * @brief Predefined Common Colors (Using moderate brightness to preserve power)
 */
#define LED_COLOR_WHITE   ((rgb_color_t){50, 50, 50})
#define LED_COLOR_OFF     ((rgb_color_t){ 0,  0,  0})

#define LED_COLOR_RED     ((rgb_color_t){50,  0,  0})
#define LED_COLOR_GREEN   ((rgb_color_t){ 0, 50,  0})
#define LED_COLOR_BLUE    ((rgb_color_t){ 0,  0, 50})

#define LED_COLOR_YELLOW  ((rgb_color_t){50, 50, 0})
#define LED_COLOR_CYAN    ((rgb_color_t){0, 50, 50})
#define LED_COLOR_MAGENTA ((rgb_color_t){50, 0, 50})
#define LED_COLOR_ORANGE  ((rgb_color_t){60, 20, 0})
#define LED_COLOR_PURPLE  ((rgb_color_t){20, 0, 50})

/**
 * @brief Initialize the internal LED peripheral. 
 *        Must be called once (e.g., at the start of app_main).
 */
void Internal_LED_Init(void);

/**
 * @brief Core functions to control the state
 */
void Internal_LED_On_Explicit(rgb_color_t color);
void Internal_LED_Off(void);
void Internal_LED_Blink(int howmanytimes, rgb_color_t color);
void Internal_LED_Rainbow(int howmanytimes);

/**
 * @brief Macro magic to allow optional color parameters.
 *        - Internal_LED_On()                 -> Defaults to White
 *        - Internal_LED_On(LED_COLOR_RED)    -> Uses the predefined macro
 */
#define GET_LED_MACRO(_1, NAME, ...) NAME
#define Internal_LED_On(...) GET_LED_MACRO(__VA_ARGS__, Internal_LED_On_Explicit, Internal_LED_On_Default)(__VA_ARGS__)

// Default implementation helper macro
#define Internal_LED_On_Default() Internal_LED_On_Explicit(LED_COLOR_WHITE)

#endif // INTERNAL_LED_H
