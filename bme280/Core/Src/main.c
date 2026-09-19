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
#include <stdio.h>
#include <string.h>

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

I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* Types used by the compensation routines from the BME280 datasheet */
typedef int32_t  BME280_S32_t;
typedef uint32_t BME280_U32_t;
typedef int64_t  BME280_S64_t;

/* SDO low -> 0x76, SDO high -> 0x77. Probed at start-up. */
static uint8_t bme280_addr;
/* BMP280 is pin- and register-compatible but has no humidity channel. */
static uint8_t has_humidity;

uint8_t rawData[8];
int32_t tRaw, pRaw, hRaw;
char sendData[80];
int len;
int32_t temp, pressure, humid;

/* Compensation parameters, read out of the sensor at start-up */
uint16_t dig_T1, dig_P1;
int16_t  dig_T2, dig_T3;
int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
uint8_t  dig_H1, dig_H3;
int16_t  dig_H2, dig_H4, dig_H5;
int8_t   dig_H6;

BME280_S32_t t_fine;

/* Report over the same UART the measurements go out on, so a failing
   start-up is visible instead of silently producing zeroes. */
static void BME280_Report(const char *msg)
{
	HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
}

/* Returns 1 and sets bme280_addr/has_humidity if a BME280 or BMP280 answers. */
static int BME280_Detect(void)
{
	uint8_t id;

	for (bme280_addr = (0x76 << 1); bme280_addr <= (0x77 << 1); bme280_addr += 2)
	{
		if (HAL_I2C_IsDeviceReady(&hi2c1, bme280_addr, 3, 100) != HAL_OK)
		{
			continue;
		}
		if (HAL_I2C_Mem_Read(&hi2c1, bme280_addr, 0xD0, I2C_MEMADD_SIZE_8BIT,
		                     &id, 1, 1000) != HAL_OK)
		{
			continue;
		}
		len = sprintf(sendData, "# found 0x%02X at addr 0x%02X\r\n",
		              id, bme280_addr >> 1);
		BME280_Report(sendData);
		if (id == 0x60 || id == 0x58)   /* 0x60 = BME280, 0x58 = BMP280 */
		{
			has_humidity = (id == 0x60);
			return 1;
		}
	}
	return 0;
}

static void BME280_Init(void)
{
	uint8_t calib[26] = {0}, calibH[7] = {0}, cfg;

	if (!BME280_Detect())
	{
		BME280_Report("# no BME280/BMP280 on the bus\r\n");
		return;
	}

	if (HAL_I2C_Mem_Read(&hi2c1, bme280_addr, 0x88, I2C_MEMADD_SIZE_8BIT, calib, 26, 1000) != HAL_OK ||
	    (has_humidity &&
	     HAL_I2C_Mem_Read(&hi2c1, bme280_addr, 0xE1, I2C_MEMADD_SIZE_8BIT, calibH, 7, 1000) != HAL_OK))
	{
		BME280_Report("# calibration read failed\r\n");
		return;
	}

	dig_T1 = (uint16_t)(calib[1] << 8 | calib[0]);
	dig_T2 = (int16_t) (calib[3] << 8 | calib[2]);
	dig_T3 = (int16_t) (calib[5] << 8 | calib[4]);
	dig_P1 = (uint16_t)(calib[7] << 8 | calib[6]);
	dig_P2 = (int16_t) (calib[9] << 8 | calib[8]);
	dig_P3 = (int16_t) (calib[11] << 8 | calib[10]);
	dig_P4 = (int16_t) (calib[13] << 8 | calib[12]);
	dig_P5 = (int16_t) (calib[15] << 8 | calib[14]);
	dig_P6 = (int16_t) (calib[17] << 8 | calib[16]);
	dig_P7 = (int16_t) (calib[19] << 8 | calib[18]);
	dig_P8 = (int16_t) (calib[21] << 8 | calib[20]);
	dig_P9 = (int16_t) (calib[23] << 8 | calib[22]);
	dig_H1 = calib[25];

	dig_H2 = (int16_t)(calibH[1] << 8 | calibH[0]);
	dig_H3 = calibH[2];
	/* dig_H4 and dig_H5 are signed 12 bit values packed into 3 bytes */
	dig_H4 = (int16_t)(((int8_t)calibH[3]) * 16) | (int16_t)(calibH[4] & 0x0F);
	dig_H5 = (int16_t)(((int8_t)calibH[5]) * 16) | (int16_t)(calibH[4] >> 4);
	dig_H6 = (int8_t)calibH[6];

	if (has_humidity)
	{
		/* ctrl_hum only takes effect when ctrl_meas is written afterwards */
		cfg = 0x01;                 /* osrs_h = x1                          */
		HAL_I2C_Mem_Write(&hi2c1, bme280_addr, 0xF2, I2C_MEMADD_SIZE_8BIT, &cfg, 1, 1000);
	}
	cfg = 0xA0;                     /* t_sb = 1000 ms, filter off           */
	HAL_I2C_Mem_Write(&hi2c1, bme280_addr, 0xF5, I2C_MEMADD_SIZE_8BIT, &cfg, 1, 1000);
	cfg = (1 << 5) | (1 << 2) | 3;  /* osrs_t = x1, osrs_p = x1, normal mode */
	HAL_I2C_Mem_Write(&hi2c1, bme280_addr, 0xF4, I2C_MEMADD_SIZE_8BIT, &cfg, 1, 1000);
}

BME280_S32_t BME280_compensate_T_int32(BME280_S32_t adc_T)
{
	BME280_S32_t var1, var2, T;
	var1 = ((((adc_T>>3) - ((BME280_S32_t)dig_T1<<1))) * ((BME280_S32_t)dig_T2)) >> 11;
	var2 = (((((adc_T>>4) - ((BME280_S32_t)dig_T1)) * ((adc_T>>4) - ((BME280_S32_t)dig_T1)))
	>> 12) * ((BME280_S32_t)dig_T3)) >> 14;
	t_fine = var1 + var2;
	T = (t_fine * 5 + 128) >> 8;
	return T;
}

BME280_U32_t BME280_compensate_P_int64(BME280_S32_t adc_P)
{
	BME280_S64_t var1, var2, p;
	var1 = ((BME280_S64_t)t_fine) - 128000;
	var2 = var1 * var1 * (BME280_S64_t)dig_P6;
	var2 = var2 + ((var1*(BME280_S64_t)dig_P5)<<17);
	var2 = var2 + (((BME280_S64_t)dig_P4)<<35);
	var1 = ((var1 * var1 * (BME280_S64_t)dig_P3)>>8) + ((var1 * (BME280_S64_t)dig_P2)<<12);
	var1 = (((((BME280_S64_t)1)<<47)+var1))*((BME280_S64_t)dig_P1)>>33;
	if (var1 == 0)
	{
		return 0; // avoid exception caused by division by zero
	}
	p = 1048576-adc_P;
	p = (((p<<31)-var2)*3125)/var1;
	var1 = (((BME280_S64_t)dig_P9) * (p>>13) * (p>>13)) >> 25;
	var2 = (((BME280_S64_t)dig_P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((BME280_S64_t)dig_P7)<<4);
	return (BME280_U32_t)p;
}

BME280_U32_t bme280_compensate_H_int32(BME280_S32_t adc_H)
{
	BME280_S32_t v_x1_u32r;
	v_x1_u32r = (t_fine - ((BME280_S32_t)76800));
	v_x1_u32r = (((((adc_H << 14) - (((BME280_S32_t)dig_H4) << 20) - (((BME280_S32_t)dig_H5) *
	v_x1_u32r)) + ((BME280_S32_t)16384)) >> 15) * (((((((v_x1_u32r *
	((BME280_S32_t)dig_H6)) >> 10) * (((v_x1_u32r * ((BME280_S32_t)dig_H3)) >> 11) +
	((BME280_S32_t)32768))) >> 10) + ((BME280_S32_t)2097152)) * ((BME280_S32_t)dig_H2) +
	8192) >> 14));
	v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
	((BME280_S32_t)dig_H1)) >> 4));
	v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
	v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
	return (BME280_U32_t)(v_x1_u32r>>12);
}
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
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  BME280_Init();

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	if (HAL_I2C_Mem_Read(&hi2c1, bme280_addr, 0xF7, I2C_MEMADD_SIZE_8BIT,
	                     rawData, has_humidity ? 8 : 6, 1000) != HAL_OK)
	{
		BME280_Report("# read failed\r\n");
		HAL_Delay(1000);
		continue;
	}

	pRaw = (rawData[0]<<12)|(rawData[1]<<4)|(rawData[2]>>4);
	tRaw = (rawData[3]<<12)|(rawData[4]<<4)|(rawData[5]>>4);
	hRaw = (rawData[6]<<8)|(rawData[7]);
	temp = BME280_compensate_T_int32(tRaw);
	pressure = (int32_t)BME280_compensate_P_int64(pRaw);
	humid = has_humidity ? (int32_t)bme280_compensate_H_int32(hRaw) : 0;

	/* temp in 0.01 degC, pressure in Q24.8 Pa, humidity in Q22.10 %RH */
	len = sprintf(sendData, "%d,%d,%d\r\n", (int)temp, (int)pressure, (int)humid);

	HAL_UART_Transmit(&huart2, (uint8_t*) sendData, len, 10);
	HAL_Delay(1000);
    /* USER CODE END WHILE */


    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
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
  hi2c1.Init.Timing = 0x10D19CE4;
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
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pins : USART_TX_Pin USART_RX_Pin */
  GPIO_InitStruct.Pin = USART_TX_Pin|USART_RX_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
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
