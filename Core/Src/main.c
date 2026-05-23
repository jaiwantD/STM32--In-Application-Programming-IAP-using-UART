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
#include "stm32f4xx_hal.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_ADDRESS        0x08008000U          /* sector 2 start            */
#define FLASH_END_ADDRESS  0x08080000U          /* 512 KB device end         */
#define SRAM_START         0x20000000U
#define SRAM_END           0x20020000U          /* 128 KB SRAM               */


#define CMD_HANDSHAKE      0x7FU
#define RESP_ACK           0x79U
#define RESP_NACK          0x1FU

#define CHUNK_SIZE         256U

#define BL_WINDOW_MS       8000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void uart_send(uint8_t b)
{
    HAL_UART_Transmit(&huart1, &b, 1, 200);
}

static uint32_t addr_to_sector(uint32_t addr)
{
    if      (addr < 0x08004000U) return FLASH_SECTOR_0;   /* 16K */
    else if (addr < 0x08008000U) return FLASH_SECTOR_1;   /* 16K */
    else if (addr < 0x0800C000U) return FLASH_SECTOR_2;   /* 16K */
    else if (addr < 0x08010000U) return FLASH_SECTOR_3;   /* 16K */
    else if (addr < 0x08020000U) return FLASH_SECTOR_4;   /* 64K */
    else if (addr < 0x08040000U) return FLASH_SECTOR_5;   /* 128K */
    else if (addr < 0x08060000U) return FLASH_SECTOR_6;   /* 128K */
    else                         return FLASH_SECTOR_7;   /* 128K */
}


// -----------FLASH ERASE------------------------- (note: 2.7-3V to erase)
/*typedef enum
{
  HAL_OK       = 0x00U,
  HAL_ERROR    = 0x01U,
  HAL_BUSY     = 0x02U,
  HAL_TIMEOUT  = 0x03U
} HAL_StatusTypeDef; */


static HAL_StatusTypeDef flash_erase_app(uint32_t size)
{
    uint32_t first = addr_to_sector(APP_ADDRESS);
    uint32_t last  = addr_to_sector(APP_ADDRESS + size - 1U);

    FLASH_EraseInitTypeDef e = {0};
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Banks        = FLASH_BANK_1;
    e.Sector       = first;
    e.NbSectors    = (last - first) + 1U;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t err = 0;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &err);
    HAL_FLASH_Lock();
    return st;
}

//----------writing in IAP from AN4657---------------

static HAL_StatusTypeDef flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < len; i += 4U) {
        uint32_t word =  (uint32_t)data[i]
                      | ((uint32_t)data[i + 1] << 8)
                      | ((uint32_t)data[i + 2] << 16)
                      | ((uint32_t)data[i + 3] << 24);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }
    HAL_FLASH_Lock();
    return HAL_OK;
}


//_______JUMP TO APPLICATION________________

static uint8_t app_is_valid(void)
{
    uint32_t sp = *(volatile uint32_t *)APP_ADDRESS;   /* initial stack ptr  */
    return (sp >= SRAM_START && sp <= SRAM_END);       /* not 0xFFFFFFFF      */
}

static void jump_to_app(void)
{
    uint32_t app_sp    = *(volatile uint32_t *)(APP_ADDRESS);
    uint32_t app_entry = *(volatile uint32_t *)(APP_ADDRESS + 4U);

    __disable_irq();

    HAL_UART_DeInit(&huart1);
    HAL_RCC_DeInit();
    HAL_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    SCB->VTOR = APP_ADDRESS;  //Important

    __set_MSP(app_sp);
    __DSB();
    __ISB();
    __enable_irq();

    ((void (*)(void))app_entry)();
    while (1) { }
}


/* ====================================================================== */
/*  Firmware update sequence                                               */
/* ====================================================================== */
static void receive_and_program(void)
{
    uart_send(RESP_ACK);                       /* ack the handshake          */

    /* 1. read the 4-byte size */
    uint8_t szb[4];
    if (HAL_UART_Receive(&huart1, szb, 4, 5000) != HAL_OK) { uart_send(RESP_NACK); return; }
    uint32_t size = (uint32_t)szb[0] | ((uint32_t)szb[1] << 8)
                  | ((uint32_t)szb[2] << 16) | ((uint32_t)szb[3] << 24);

    if (size == 0 || size > (FLASH_END_ADDRESS - APP_ADDRESS)) { uart_send(RESP_NACK); return; }

    /* 2. erase only the sectors we need */
    if (flash_erase_app(size) != HAL_OK) { uart_send(RESP_NACK); return; }
    uart_send(RESP_ACK);                       /* ready for data             */

    /* 3. receive + program in 256-byte chunks */
    uint32_t addr      = APP_ADDRESS;
    uint32_t remaining = size;
    uint8_t  buf[CHUNK_SIZE];

    while (remaining > 0) {
        uint32_t n = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;

        if (HAL_UART_Receive(&huart1, buf, n, 5000) != HAL_OK) { uart_send(RESP_NACK); return; }

        uint32_t padded = (n + 3U) & ~3U;      /* round up to a word         */
        for (uint32_t i = n; i < padded; i++) buf[i] = 0xFFU;

        if (flash_write(addr, buf, padded) != HAL_OK) { uart_send(RESP_NACK); return; }

        addr      += n;
        remaining -= n;
        uart_send(RESP_ACK);                   /* tell host to send next     */
    }

    HAL_Delay(20);
    jump_to_app();                             /* done - run the new app     */
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
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  uint8_t  b;
  uint8_t  go_update = 0;
  uint32_t t0   = HAL_GetTick();
  uint32_t tled = t0;

  while ((HAL_GetTick() - t0) < BL_WINDOW_MS) {
      if (HAL_UART_Receive(&huart1, &b, 1, 10) == HAL_OK && b == CMD_HANDSHAKE) {
          go_update = 1;
          break;
      }
      if ((HAL_GetTick() - tled) > 150U) {       /* ~3 Hz = "in bootloader" */
          HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
          tled = HAL_GetTick();
      }
  }

  if (go_update) {
      receive_and_program();                 /* jumps to app on success    */
  }

  if (app_is_valid()) {
      jump_to_app();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
      HAL_Delay(80);
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
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
  if (HAL_UART_Init(&huart1) != HAL_OK)
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
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
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
