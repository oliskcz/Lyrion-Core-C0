/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "config.h"

#if ENABLE_WS2812
	#include "ws2812.h"
#endif
#if ENABLE_OLED
	    #include "ssd1306.h"
	#include "ssd1306_fonts.h"
#endif
#if ENABLE_TMP102
	#include "tmp102.h"
#endif
#if ENABLE_CC1101
	#include "cc1101.h"
	#include "cc1101_port.h"
#endif
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim3;
DMA_HandleTypeDef hdma_tim3_ch3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint8_t rxByte;
uint32_t lastTick = 0;
volatile uint8_t led_event = 0;       /* set to 1 by EXTI callback (LED toggled) */
#if ENABLE_I2C_SCAN
uint32_t i2c_scan_lastTick = 0;
uint32_t i2c_scan_address = 0;   /* 0 = idle, 1-127 = currently scanning */
uint8_t  i2c_scan_found = 0;    /* device count during current scan */
uint8_t  i2c_found_addrs[16];   /* addresses of found devices (7-bit) */
#endif
  #if ENABLE_CC1101
  #if CC1101_ENABLE_RADIO1
  cc1101_t radio1;
  #endif
  #if CC1101_ENABLE_RADIO2
  cc1101_t radio2;
  #endif
  static cc1101_t *cc1101_radio1     = NULL;
  static char       cc1101_rx_msg[22];
  static int8_t     cc1101_rx_rssi   = 0;
  static uint8_t    cc1101_rx_lqi    = 0;
  static volatile bool cc1101_rx_ready = false;
  #endif
  /* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
static void MX_NVIC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Tiny uint-to-string helper for the OLED uptime line (avoids pulling stdio). */
static int uitoa(uint32_t value, char *out)
{
    char tmp[11];
    int i = 0;
    int len = 0;

    if (value == 0)
    {
        out[0] = '0';
        return 1;
    }

    while (value > 0)
    {
        tmp[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while (i > 0)
    {
        out[len++] = tmp[--i];
    }
    return len;
}

static const char hex_chars[] = "0123456789ABCDEF";

#if ENABLE_TMP102 && ENABLE_OLED
/* Draw temperature on OLED line y=24 (blue area). */
static void oled_show_temp(int16_t temp)
{
    char buf[16];
    ssd1306_SetCursor(2, 8);
    ssd1306_WriteString("Temp: ", Font_6x8, White);

    if (temp == TMP102_ERROR)
    {
        ssd1306_WriteString("ERR", Font_6x8, White);
        return;
    }

    /* Handle sign for negative temperatures. */
    int32_t abs_val = temp;
    int idx = 0;
    if (abs_val < 0)
    {
        buf[idx++] = '-';
        abs_val = -abs_val;
    }

#if TMP102_DECIMAL_PLACES == 0
    idx += uitoa((uint32_t)abs_val, &buf[idx]);
#elif TMP102_DECIMAL_PLACES == 1
    idx += uitoa((uint32_t)(abs_val / 10), &buf[idx]);
    buf[idx++] = '.';
    buf[idx++] = (char)('0' + (abs_val % 10));
#elif TMP102_DECIMAL_PLACES == 2
    idx += uitoa((uint32_t)(abs_val / 100), &buf[idx]);
    buf[idx++] = '.';
    buf[idx++] = (char)('0' + ((abs_val / 10) % 10));
    buf[idx++] = (char)('0' + (abs_val % 10));
#endif
    buf[idx++] = ' ';    /* space before "C" (0xB0 degree sign not in Font6x8) */
    buf[idx++] = 'C';
    buf[idx]   = '\0';
    ssd1306_WriteString(buf, Font_6x8, White);
}
#endif

#if ENABLE_CC1101
/* Called from EXTI (rising edge on GDO0) — must be tiny, just set a flag. */
static void on_cc1101_rx_data(void)
{
    cc1101_rx_ready = true;
}
#endif

#if ENABLE_CC1101 && ENABLE_OLED
static void oled_show_rx(const char *msg, int8_t rssi, uint8_t lqi)
{
    char buf[22];

    ssd1306_FillRectangle(2, 16, 127, 31, Black);

    /* Line 2 (y=16): message */
    ssd1306_SetCursor(2, 16);
    ssd1306_WriteString("RX: ", Font_6x8, White);
    ssd1306_WriteString((char *)msg, Font_6x8, White);

    /* Line 3 (y=24): RSSI and LQI */
    ssd1306_SetCursor(2, 24);
    int idx = 0;
    static const char rp[] = "RSSI: ";
    for (; idx < (int)sizeof(rp) - 1; idx++) buf[idx] = rp[idx];
    int32_t abs_rssi = (rssi < 0) ? -rssi : rssi;
    if (rssi < 0) buf[idx++] = '-';
    idx += uitoa((uint32_t)abs_rssi, &buf[idx]);
    buf[idx++] = ' ';
    buf[idx++] = 'd'; buf[idx++] = 'B'; buf[idx++] = 'm';
    buf[idx++] = ' '; buf[idx++] = ' ';
    buf[idx++] = 'L'; buf[idx++] = 'Q'; buf[idx++] = 'I'; buf[idx++] = ':';
    idx += uitoa(lqi, &buf[idx]);
    buf[idx] = '\0';
    ssd1306_WriteString(buf, Font_6x8, White);
}
#endif

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* If HSE failed to start, fall back to HSI (48 MHz internal oscillator). */
  if ((RCC->CR & RCC_CR_HSERDY) == 0)
  {
      RCC_OscInitTypeDef RCC_OscInitStruct = {0};
      RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

      RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
      RCC_OscInitStruct.HSIState = RCC_HSI_ON;
      RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
      if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
      {
          Error_Handler();
      }

      RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1;
      RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
      RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
      RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
      RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
      if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
      {
          Error_Handler();
      }
  }

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  #if ENABLE_I2C
  MX_I2C1_Init();
  #endif
  #if ENABLE_SPI
  MX_SPI1_Init();
  #endif
  #if ENABLE_UART1
  MX_USART1_UART_Init();
  #endif
  #if ENABLE_UART2
  MX_USART2_UART_Init();
  #endif
  #if ENABLE_ADC
  MX_ADC1_Init();
  #endif
  #if ENABLE_WS2812
  MX_TIM3_Init();
  #endif

  /* Initialize interrupts */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */
  /* Trigger the first OLED 1-second tick immediately on entering the
     main loop, so status text appears right away. */
  lastTick = HAL_GetTick() - 1000;

  #if ENABLE_UART1 && UART_DEBUG
  HAL_UART_Transmit(&huart1, (uint8_t *)"BOOT\r\n", 6, 100);
  #endif

 #if !ENABLE_UART1
  /* Disable USART1 interrupt if UART1 is not enabled (MX_NVIC_Init enables it unconditionally). */
  HAL_NVIC_DisableIRQ(USART1_IRQn);
  #endif
  #if ENABLE_WS2812
	  WS2812_Init();
  #endif
  #if ENABLE_OLED
	  ssd1306_Init();
  #endif

  #if ENABLE_WS2812
  WS2812_SetBrightness(0, 16);   // LED 0: 32/255 ~ 13%
  WS2812_SetBrightness(1, 16);   // LED 1: 32/255 ~ 13%
  #endif

  #if ENABLE_OLED
  ssd1306_Fill(Black);
  ssd1306_SetCursor(2, 0);
  ssd1306_WriteString("Lyrion Core C0", Font_6x8, White);
  #endif

  #if ENABLE_TMP102
  TMP102_SetConversionRate(&hi2c1, TMP102_CONV_RATE);
  #if ENABLE_OLED
  oled_show_temp(TMP102_ReadTemp(&hi2c1));
  #endif
  #endif

  #if ENABLE_OLED
  ssd1306_UpdateScreen();
  #endif

  #if ENABLE_UART1
  HAL_UART_Receive_IT(&huart1, &rxByte, 1);
  #endif

  #if ENABLE_I2C_SCAN
  /* Kick off the first I2C scan immediately at startup. */
  i2c_scan_address = 1;
  i2c_scan_found = 0;
  #endif

  #if ENABLE_CC1101
  /* SPI MISO pin for waitReady() polling (PA6 on this board). */
  #define CC1101_MISO_PORT  GPIOA
  #define CC1101_MISO_PIN   GPIO_PIN_6
  {
      uint8_t radios_ok = 0;
  #if CC1101_ENABLE_RADIO1
      cc1101_config_t cfg1 = {
          .spi = &hspi1,
          .cs_port = CS1_GPIO_Port,   .cs_pin   = CS1_Pin,
          .miso_port = CC1101_MISO_PORT, .miso_pin = CC1101_MISO_PIN,
          .gdo0_port = GDO0_1_GPIO_Port, .gdo0_pin = GDO0_1_Pin,
          .gdo2_port = GDO2_1_GPIO_Port, .gdo2_pin = GDO2_1_Pin,
      };
      cc1101_status_t st1 = cc1101_init(&radio1, &cfg1, CC1101_MOD_ASK_OOK,
                                         433.5f, 4.0f);
      if (st1 != CC1101_STATUS_OK) {
          #if ENABLE_UART1 && UART_DEBUG
          HAL_UART_Transmit(&huart1, (uint8_t *)"R1: FAIL\r\n", 9, 100);
          #endif
      } else {
          radios_ok++;
          cc1101_radio1 = &radio1;
          #if ENABLE_UART1 && UART_DEBUG
          HAL_UART_Transmit(&huart1, (uint8_t *)"R1: OK\r\n", 8, 100);
          #endif
      }
  #endif
  #if CC1101_ENABLE_RADIO2
      cc1101_config_t cfg2 = {
          .spi = &hspi1,
          .cs_port = CS2_GPIO_Port,   .cs_pin   = CS2_Pin,
          .miso_port = CC1101_MISO_PORT, .miso_pin = CC1101_MISO_PIN,
          .gdo0_port = GDO0_2_GPIO_Port, .gdo0_pin = GDO0_2_Pin,
          .gdo2_port = GDO2_2_GPIO_Port, .gdo2_pin = GDO2_2_Pin,
      };
      cc1101_status_t st2 = cc1101_init(&radio2, &cfg2, CC1101_MOD_ASK_OOK,
                                         433.5f, 4.0f);
      if (st2 != CC1101_STATUS_OK) {
          #if ENABLE_UART1 && UART_DEBUG
          HAL_UART_Transmit(&huart1, (uint8_t *)"R2: FAIL\r\n", 9, 100);
          #endif
      } else {
          radios_ok++;
          (void)radio2;
          #if ENABLE_UART1 && UART_DEBUG
          HAL_UART_Transmit(&huart1, (uint8_t *)"R2: OK\r\n", 8, 100);
          #endif
      }
  #endif
      (void)radios_ok;

      if (cc1101_radio1) {
          cc1101_t *r = cc1101_radio1;

          cc1101_set_modulation(r, CC1101_MOD_ASK_OOK);
          cc1101_set_frequency(r, 433.8f);
          cc1101_set_data_rate(r, 1.0f);
          cc1101_set_output_power(r, 10);

          cc1101_set_packet_length_mode(r, CC1101_PKT_LEN_MODE_VARIABLE, 255);
          cc1101_set_address_filtering_mode(r, CC1101_ADDR_FILTER_MODE_NONE);
          cc1101_set_preamble_length(r, 64);
          cc1101_set_sync_word(r, 0x1234);
          cc1101_set_sync_mode(r, CC1101_SYNC_MODE_16_16);
          cc1101_set_crc(r, true);
          cc1101_set_data_whitening(r, true);
          cc1101_set_manchester(r, false);
          cc1101_set_fec(r, false);

          cc1101_set_receive_action(r, on_cc1101_rx_data, CC1101_GDO0);
          cc1101_start_receive(r, 0);
      }
  }
  #endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	#if ENABLE_WS2812
	WS2812_Example1();
	#endif

	#if ENABLE_CC1101
	if (cc1101_radio1 && cc1101_rx_ready) {
	    cc1101_rx_ready = false;

	    uint8_t rx_buf[64];
	    size_t  rx_len = 0;

	    cc1101_status_t st = cc1101_read_data(cc1101_radio1, rx_buf,
	                                           sizeof(rx_buf), &rx_len);
	    if (st == CC1101_STATUS_OK) {
	        rx_buf[rx_len] = '\0';

	        size_t copy_len = rx_len;
	        if (copy_len >= sizeof(cc1101_rx_msg))
	            copy_len = sizeof(cc1101_rx_msg) - 1;
	        memcpy(cc1101_rx_msg, rx_buf, copy_len);
	        cc1101_rx_msg[copy_len] = '\0';

	        cc1101_rx_rssi  = cc1101_get_rssi(cc1101_radio1);
	        cc1101_rx_lqi   = cc1101_get_lqi(cc1101_radio1);

	        #if ENABLE_UART1 && UART_DEBUG
	        HAL_UART_Transmit(&huart1, (uint8_t *)"RX OK\r\n", 7, 100);
	        #endif
	    } else if (st == CC1101_STATUS_CRC_MISMATCH) {
	        memcpy(cc1101_rx_msg, "CRC ERR", 8);
	        cc1101_rx_rssi = cc1101_get_rssi(cc1101_radio1);
	        cc1101_rx_lqi  = cc1101_get_lqi(cc1101_radio1);
	        #if ENABLE_UART1 && UART_DEBUG
	        HAL_UART_Transmit(&huart1, (uint8_t *)"CRC ERR\r\n", 9, 100);
	        #endif
	    }

	    #if ENABLE_OLED
	    oled_show_rx(cc1101_rx_msg, cc1101_rx_rssi, cc1101_rx_lqi);
	    ssd1306_UpdateScreen();
	    #endif

	    cc1101_start_receive(cc1101_radio1, 0);
	}
	#endif

	if (HAL_GetTick() - lastTick >= 1000)
	{
	    lastTick = HAL_GetTick();

	    #if ENABLE_TMP102 && ENABLE_OLED
	    oled_show_temp(TMP102_ReadTemp(&hi2c1));
	    #endif

	    #if ENABLE_I2C_SCAN
	    /* Start I2C scan if 30 s elapsed and no scan in progress. */
	    if (i2c_scan_address == 0 && HAL_GetTick() - i2c_scan_lastTick >= 30000)
	    {
	        i2c_scan_lastTick = HAL_GetTick();
	        i2c_scan_address = 1;
	        i2c_scan_found = 0;
	        #if ENABLE_UART1 && UART_DEBUG
	        HAL_UART_Transmit(&huart1, (uint8_t *)"I2C scan...\r\n", 13, 100);
	        #endif
	    }
	    #endif
	    #if ENABLE_OLED
	    ssd1306_UpdateScreen();
	    #endif
	}

	#if ENABLE_I2C_SCAN
	/* Non-blocking I2C scan: one address per loop iteration. */
	if (i2c_scan_address >= 1 && i2c_scan_address < 128)
	{
	    uint8_t addr = i2c_scan_address++;
	    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1, 10) == HAL_OK)
	    {
	        if (i2c_scan_found < 16)
	            i2c_found_addrs[i2c_scan_found] = addr;
	        i2c_scan_found++;
	        #if ENABLE_UART1 && UART_DEBUG
	        char devMsg[] = "  Found 0x00\r\n";
	        devMsg[9] = hex_chars[(addr >> 4) & 0x0F];
	        devMsg[10] = hex_chars[addr & 0x0F];
	        HAL_UART_Transmit(&huart1, (uint8_t *)devMsg, sizeof(devMsg) - 1, 100);
	        #endif
	    }
	    if (i2c_scan_address >= 128)
	    {
	        i2c_scan_address = 0;
	        #if ENABLE_UART1 && UART_DEBUG
	        HAL_UART_Transmit(&huart1, (uint8_t *)"I2C scan done.\r\n", 16, 100);
	        #endif
	        #if ENABLE_OLED
	        {
	            char addrStr[5] = "0x00";
	            ssd1306_SetCursor(2, 56);
	            ssd1306_WriteString("I2C:", Font_6x8, White);
	            if (i2c_scan_found > 0)
	            {
	                uint8_t show = i2c_scan_found;
	                if (show > 16) show = 16;
	                for (uint8_t i = 0; i < show; i++)
	                {
	                    uint8_t a = i2c_found_addrs[i];
	                    addrStr[2] = hex_chars[(a >> 4) & 0x0F];
	                    addrStr[3] = hex_chars[a & 0x0F];
	                    ssd1306_WriteString(" ", Font_6x8, White);
	                    ssd1306_WriteString(addrStr, Font_6x8, White);
	                }
	            }
	            else
	            {
	                ssd1306_WriteString(" X", Font_6x8, White);
	            }
	        }
	        #endif
	        #if ENABLE_OLED
	        ssd1306_UpdateScreen();
	        #endif
	    }
	}
	#endif /* ENABLE_I2C_SCAN */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief NVIC Configuration.
  * @retval None
  */
static void MX_NVIC_Init(void)
{
  /* EXTI0_1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(EXTI0_1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
  /* USART1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* EXTI4_15_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
  /* EXTI2_3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00201D2B;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* Dynamic SPI clock: if HSE failed (HSI fallback, 48 MHz), prescaler 2
     would give 24 MHz SPI ??? the CC1101 max is 10 MHz, causing bit errors.
     Adjust the prescaler so SPI ??? 6 MHz regardless of system clock.
     Run this FIRST because HAL_SPI_Init below would re-call HAL_SPI_MspInit
     which resets GPIO speed to LOW ??? so we fix GPIO speed AFTER. */
  {
      uint32_t apb1 = HAL_RCC_GetPCLK1Freq();
      uint32_t br_val = (hspi1.Instance->CR1 & SPI_CR1_BR) >> SPI_CR1_BR_Pos;
      uint32_t divisor = 2u << br_val;  /* 0???2, 1???4, 2???8, 3???16, ??? */
      uint32_t spi_speed = apb1 / divisor;
      if (spi_speed > 10000000) {
          hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
          __HAL_SPI_DISABLE(&hspi1);
          HAL_SPI_Init(&hspi1);
          __HAL_SPI_ENABLE(&hspi1);
      }
  }

  /* Reconfigure SPI GPIO pins to HIGH speed AFTER any potential re-init
     above (HAL_SPI_Init resets speed to LOW via HAL_SPI_MspInit). */
  {
      GPIO_InitTypeDef GPIO_InitStruct = {0};
      GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
      GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
      GPIO_InitStruct.Pull = GPIO_NOPULL;
      GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
      GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
      HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }

  /* Disable NSS pulse mode.  NSSP can cause inter-byte NSS pulses that
     confuse the CC1101 SPI state machine during burst writes. */
  CLEAR_BIT(hspi1.Instance->CR2, SPI_CR2_NSSP);
  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 10-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, CS3_Pin|Blink2_Pin|Blink1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS2_GPIO_Port, CS2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS1_GPIO_Port, CS1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : UserButton_Pin GDO2_2_Pin */
  GPIO_InitStruct.Pin = UserButton_Pin|GDO2_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : GDO0_2_Pin GDO0_1_Pin */
  GPIO_InitStruct.Pin = GDO0_2_Pin|GDO0_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : CS3_Pin Blink2_Pin Blink1_Pin */
  GPIO_InitStruct.Pin = CS3_Pin|Blink2_Pin|Blink1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : CS2_Pin */
  GPIO_InitStruct.Pin = CS2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CS1_Pin */
  GPIO_InitStruct.Pin = CS1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GDO2_1_Pin */
  GPIO_InitStruct.Pin = GDO2_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GDO2_1_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  #if ENABLE_CC1101
  /* The CC1101 outputs a 26 MHz clock on GDO0/GDO2 after power-on (before
   * IOCFG is written by cc1101_init). Mask the EXTI lines here so the edges
   * cannot fire ISRs and livelock the system. The CC1101 driver unmaskes a
   * line in cc1101_port_attach_interrupt() only after the corresponding GDO
   * signal has been configured (set_receive/transmit_action). */
  EXTI->IMR1 &= ~(GDO0_2_Pin | GDO0_1_Pin | GDO2_2_Pin | GDO2_1_Pin);
  EXTI->RPR1  =  (GDO0_2_Pin | GDO0_1_Pin | GDO2_2_Pin | GDO2_1_Pin);
  EXTI->FPR1  =  (GDO0_2_Pin | GDO0_1_Pin | GDO2_2_Pin | GDO2_1_Pin);
  #endif
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == UserButton_Pin)
  {
    HAL_GPIO_TogglePin(Blink1_GPIO_Port, Blink1_Pin);
    led_event = 1;
  }
  #if ENABLE_CC1101
  /* Dispatch GDO rising edges (RX-ready) to the CC1101 callback registry. */
  cc1101_on_rising_edge(GPIO_Pin);
  #endif
}

#if ENABLE_CC1101
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  /* Dispatch GDO falling edges (TX-complete) to the CC1101 callback registry. */
  cc1101_on_falling_edge(GPIO_Pin);
}
#endif

#if ENABLE_UART1
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // echo received byte
        HAL_UART_Transmit(&huart1, &rxByte, 1, 10);

        // restart interrupt reception
        HAL_UART_Receive_IT(&huart1, &rxByte, 1);
    }
}
#endif
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
