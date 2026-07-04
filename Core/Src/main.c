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
    ssd1306_SetCursor(2, 24);
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

/* USER CODE END 0 */

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
  #if ENABLE_UART1
    MX_USART1_UART_Init();
  #endif
  #if ENABLE_UART2
    MX_USART2_UART_Init();
  #endif
  #if ENABLE_SPI
    MX_SPI1_Init();
  #endif
  #if ENABLE_I2C
    MX_I2C1_Init();
  #endif
  #if ENABLE_WS2812
    MX_TIM3_Init();
  #endif
  #if ENABLE_ADC
    MX_ADC1_Init();
  #endif




  /* Initialize interrupts */
  MX_NVIC_Init();
  /* USER CODE BEGIN 2 */
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
  ssd1306_SetCursor(2, 8);
  ssd1306_WriteString("Hello, World!", Font_6x8, White);
  #endif

  #if ENABLE_TMP102
  TMP102_SetConversionRate(&hi2c1, TMP102_CONV_RATE);
  #if ENABLE_OLED
  oled_show_temp(TMP102_ReadTemp(&hi2c1));
  #endif
  #endif

  #if ENABLE_OLED
  ssd1306_SetCursor(2, 32);
  if (HAL_GPIO_ReadPin(Blink1_GPIO_Port, Blink1_Pin))
      ssd1306_WriteString("Led: ON", Font_6x8, White);
  else
      ssd1306_WriteString("Led: OFF", Font_6x8, White);
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

	/* Instant LED-state feedback (outside 1-second tick). */
	#if ENABLE_OLED
	if (led_event)
	{
	    led_event = 0;
	    ssd1306_SetCursor(2, 32);
	    if (HAL_GPIO_ReadPin(Blink1_GPIO_Port, Blink1_Pin))
	        ssd1306_WriteString("Led: ON", Font_6x8, White);
	    else
	        ssd1306_WriteString("Led: OFF", Font_6x8, White);
	    ssd1306_UpdateScreen();
	}
	#endif

	if (HAL_GetTick() - lastTick >= 1000)
	{
	    lastTick = HAL_GetTick();

	    #if ENABLE_UART1 && UART_DEBUG
	    HAL_UART_Transmit(&huart1, (uint8_t *)"Hello, World!\r\n", 15, 100);
	    #endif

	    /* Live uptime counter on the OLED (blue, line 1). */
	    #if ENABLE_OLED
	    char buf[8];
	    int len = uitoa(lastTick / 1000U, buf);
	    buf[len]     = 's';
	    buf[len + 1] = '\0';
	    ssd1306_SetCursor(2, 16);
	    ssd1306_WriteString("Up: ", Font_6x8, White);
	    ssd1306_WriteString(buf, Font_6x8, White);
	    #endif

	    #if ENABLE_TMP102 && ENABLE_OLED
	    oled_show_temp(TMP102_ReadTemp(&hi2c1));
	    #endif

	    #if ENABLE_OLED
	    ssd1306_SetCursor(2, 32);
	    if (HAL_GPIO_ReadPin(Blink1_GPIO_Port, Blink1_Pin))
	        ssd1306_WriteString("Led: ON", Font_6x8, White);
	    else
	        ssd1306_WriteString("Led: OFF", Font_6x8, White);
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

  /*Configure GPIO pin : UserButton_Pin */
  GPIO_InitStruct.Pin = UserButton_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(UserButton_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : GDO0_2_Pin GDO0_1_Pin GDO2_2_Pin */
  GPIO_InitStruct.Pin = GDO0_2_Pin|GDO0_1_Pin|GDO2_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
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
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GDO2_1_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

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
}

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
