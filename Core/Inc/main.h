/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32c0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ADC_Pin GPIO_PIN_0
#define ADC_GPIO_Port GPIOA
#define UserButton_Pin GPIO_PIN_1
#define UserButton_GPIO_Port GPIOA
#define UserButton_EXTI_IRQn EXTI0_1_IRQn
#define GDO0_2_Pin GPIO_PIN_2
#define GDO0_2_GPIO_Port GPIOA
#define GDO0_2_EXTI_IRQn EXTI2_3_IRQn
#define WS2812B_Pin GPIO_PIN_0
#define WS2812B_GPIO_Port GPIOB
#define CS3_Pin GPIO_PIN_1
#define CS3_GPIO_Port GPIOB
#define CS2_Pin GPIO_PIN_6
#define CS2_GPIO_Port GPIOC
#define CS1_Pin GPIO_PIN_11
#define CS1_GPIO_Port GPIOA
#define GDO0_1_Pin GPIO_PIN_12
#define GDO0_1_GPIO_Port GPIOA
#define GDO0_1_EXTI_IRQn EXTI4_15_IRQn
#define GDO2_2_Pin GPIO_PIN_15
#define GDO2_2_GPIO_Port GPIOA
#define GDO2_2_EXTI_IRQn EXTI4_15_IRQn
#define GDO2_1_Pin GPIO_PIN_3
#define GDO2_1_GPIO_Port GPIOB
#define GDO2_1_EXTI_IRQn EXTI2_3_IRQn
#define Blink2_Pin GPIO_PIN_4
#define Blink2_GPIO_Port GPIOB
#define Blink1_Pin GPIO_PIN_5
#define Blink1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
