#include "ws2812.h"

#include "main.h"

#define WS2812_BITS_PER_LED 24
#define WS2812_RESET_SLOTS 80
#define WS2812_LEADING_SLOTS 1
#define WS2812_PWM_HIGH_0 3
#define WS2812_PWM_HIGH_1 6
#define WS2812_Example1_SPEED 50
#define WS2812_SCALE_COLOR(value, brightness) \
    ((uint8_t)(((uint16_t)(value) * ((uint16_t)(brightness) + 1U)) >> 8))
uint8_t hue = 0;

extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_tim3_ch3;

static volatile uint8_t data_sent;
static uint8_t led_data[WS2812_LED_COUNT][3];   /* G, R, B */
static uint8_t led_brightness[WS2812_LED_COUNT];
static uint16_t pwm_data[(WS2812_BITS_PER_LED * WS2812_LED_COUNT) +
                         WS2812_RESET_SLOTS +
                         WS2812_LEADING_SLOTS];

void HueToRGB(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (h < 85)
    {
        *r = 255 - (h * 3);
        *g = h * 3;
        *b = 0;
    }
    else if (h < 170)
    {
        h -= 85;
        *r = 0;
        *g = 255 - (h * 3);
        *b = h * 3;
    }
    else
    {
        h -= 170;
        *r = h * 3;
        *g = 0;
        *b = 255 - (h * 3);
    }
}

static void WS2812_DMA_TransferComplete(DMA_HandleTypeDef *hdma)
{
    if (hdma == &hdma_tim3_ch3)
    {
        __HAL_TIM_DISABLE_DMA(&htim3, TIM_DMA_UPDATE);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
        data_sent = 1;
    }
}

void WS2812_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_TIM_DISABLE(&htim3);
    __HAL_TIM_DISABLE_DMA(&htim3, TIM_DMA_CC3);
    __HAL_TIM_DISABLE_DMA(&htim3, TIM_DMA_UPDATE);
    (void)HAL_DMA_Abort(&hdma_tim3_ch3);
    (void)HAL_DMA_DeInit(&hdma_tim3_ch3);

    hdma_tim3_ch3.Instance = DMA1_Channel1;
    hdma_tim3_ch3.Init.Request = DMA_REQUEST_TIM3_UP;
    hdma_tim3_ch3.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tim3_ch3.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim3_ch3.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim3_ch3.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_tim3_ch3.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_tim3_ch3.Init.Mode = DMA_NORMAL;
    hdma_tim3_ch3.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_tim3_ch3) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_LINKDMA(&htim3, hdma[TIM_DMA_ID_UPDATE], hdma_tim3_ch3);

    GPIO_InitStruct.Pin = WS2812B_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(WS2812B_GPIO_Port, &GPIO_InitStruct);

    for (uint8_t led = 0; led < WS2812_LED_COUNT; led++)
    {
        led_brightness[led] = 255;
    }

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
}

void WS2812_SetLED(uint8_t led, uint8_t red, uint8_t green, uint8_t blue)
{
    if (led >= WS2812_LED_COUNT)
    {
        return;
    }

    led_data[led][0] = green;
    led_data[led][1] = red;
    led_data[led][2] = blue;
}

void WS2812_Example1(void)
{
	uint8_t r, g, b;
	// LED 0 follows hue
	HueToRGB(hue, &r, &g, &b);
	WS2812_SetLED(0, r, g, b);
	// LED 1 offset = creates moving effect
	HueToRGB(hue + 128, &r, &g, &b);
	WS2812_SetLED(1, r, g, b);
	WS2812_Send();
	hue++;               // slow animation
	HAL_Delay(50/WS2812_Example1_SPEED);      // speed control
}

void WS2812_SetBrightness(uint8_t led, uint8_t brightness)
{
    if (led >= WS2812_LED_COUNT)
    {
        return;
    }

    led_brightness[led] = brightness;
}

void WS2812_Send(void)
{
    uint32_t indx = 0;

    pwm_data[indx++] = 0;

    for (uint8_t led = 0; led < WS2812_LED_COUNT; led++)
    {
        for (uint8_t color = 0; color < 3; color++)
        {
            uint8_t value = WS2812_SCALE_COLOR(led_data[led][color],
                                               led_brightness[led]);

            for (int8_t bit = 7; bit >= 0; bit--)
            {
                pwm_data[indx++] = (value & (1U << bit)) ?
                                   WS2812_PWM_HIGH_1 :
                                   WS2812_PWM_HIGH_0;
            }
        }
    }

    for (uint8_t i = 0; i < WS2812_RESET_SLOTS; i++)
    {
        pwm_data[indx++] = 0;
    }

    data_sent = 0;
    __HAL_TIM_DISABLE(&htim3);
    __HAL_TIM_DISABLE_DMA(&htim3, TIM_DMA_UPDATE);
    (void)HAL_DMA_Abort(&hdma_tim3_ch3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
    __HAL_TIM_SET_COUNTER(&htim3, 0);

    hdma_tim3_ch3.XferCpltCallback = WS2812_DMA_TransferComplete;
    hdma_tim3_ch3.XferHalfCpltCallback = NULL;

    if (HAL_DMA_Start_IT(&hdma_tim3_ch3,
                         (uint32_t)pwm_data,
                         (uint32_t)&htim3.Instance->CCR3,
                         indx) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_UPDATE);

    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    while (!data_sent) { }

    HAL_Delay(1);
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_3);
}
