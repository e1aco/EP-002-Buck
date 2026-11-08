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
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* ==========================================================================
   * 双定时器同频 PWM + 硬件主从触发死区（相移）
   *
   *   TIM1 (主) : PWM CH1 -> PA8   TRGO = Update Event
   *   TIM3 (从) : PWM CH2 -> PA7   Slave = Trigger Mode, TRGI = ITR0 (= TIM1 TRGO)
   *
   * 与 F4 参考工程 PWMGenerate 的实现保持一致:
   *   主从配置、CNT 预置、启动顺序全部集中在 MX_PWM_Start_PhaseLocked() 中,
   *   且放在 USER CODE 区, CubeMX 重新生成代码不会丢失。
   * ========================================================================== */

  /* 1) 设定两路 PWM 的占空比 */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, PWM_PULSE);    /* PA8: 高 [0, 1800)  */
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, PWM_PULSE_T3); /* PA7: 高 [5, 1795)  */

  /* 2) 启动两路 PWM, 并一次性锁定 TIM1 / TIM3 的相位(死区) */
  MX_PWM_Start_PhaseLocked();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* ---- 调试用: 持续锁存寄存器, 在 Keil 的 Watch 窗口观察 -------------
     * g_dbg_tim3_cnt  : 若在不停变化 -> TIM3 计数器已被 TRGO 启动(正常)
     *                    若恒等于 3595   -> 没收到触发, 卡在预置值
     * g_dbg_tim3_sr   : bit6(TIF)=1 表示收到过 TRIGGER 事件
     * g_dbg_tim3_cr1  : bit0(CEN)=0 是触发模式的正常现象(由硬件 CNT_EN 启动)
     * g_dbg_tim1_cr2  : bit6:4(MMS) 应为 010 = TRGO 选更新事件
     * g_dbg_tim3_smcr : 应为 0x0006 (SMS=110 触发模式, TS=000 ITR0)
     * ------------------------------------------------------------------ */
    g_dbg_tim1_cr1  = TIM1->CR1;
    g_dbg_tim1_cr2  = TIM1->CR2;
    g_dbg_tim1_cnt  = TIM1->CNT;
    g_dbg_tim3_cr1  = TIM3->CR1;
    g_dbg_tim3_smcr = TIM3->SMCR;
    g_dbg_tim3_cnt  = TIM3->CNT;
    g_dbg_tim3_sr   = TIM3->SR;
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

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
