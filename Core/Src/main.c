/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "arm_math.h"
#include "arm_const_structs.h"
#include "semphr.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

//#define SEND_BUFFER_SIZE 245760/8//
// FFT defines
#define REAL_FFT_SIZE 4096
#define FFT_SIZE (REAL_FFT_SIZE/2)

// Low-pass filter parameters
#define SAMPLE_RATE 8000 // Sample rate in Hz
#define CUTOFF_FREQ 8000  // Cutoff frequency in Hz

// ADC Defines
#define ADC_BUFFER_SIZE 4096

//Transmit Defines
#define TRANSMIT_BUFFER_SIZE ADC_BUFFER_SIZE
#define ESCAPE_CHAR 0xFFFF
#define START_CHAR 0xFFEE
#define END_CHAR 0xFFFE

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart4;
UART_HandleTypeDef huart3;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for AudioCapTask */
osThreadId_t AudioCapTaskHandle;
const osThreadAttr_t AudioCapTask_attributes = {
  .name = "AudioCapTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for FFTTask */
osThreadId_t FFTTaskHandle;
const osThreadAttr_t FFTTask_attributes = {
  .name = "FFTTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for SendDataTask */
osThreadId_t SendDataTaskHandle;
const osThreadAttr_t SendDataTask_attributes = {
  .name = "SendDataTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh1,
};
/* Definitions for AudioCapSem01 */
osSemaphoreId_t AudioCapSem01Handle;
const osSemaphoreAttr_t AudioCapSem01_attributes = {
  .name = "AudioCapSem01"
};
/* Definitions for FFTSem02 */
osSemaphoreId_t FFTSem02Handle;
const osSemaphoreAttr_t FFTSem02_attributes = {
  .name = "FFTSem02"
};
/* Definitions for SendDataSem03 */
osSemaphoreId_t SendDataSem03Handle;
const osSemaphoreAttr_t SendDataSem03_attributes = {
  .name = "SendDataSem03"
};
/* Definitions for ADC_Buffer0Sem04 */
osSemaphoreId_t ADC_Buffer0Sem04Handle;
const osSemaphoreAttr_t ADC_Buffer0Sem04_attributes = {
  .name = "ADC_Buffer0Sem04"
};
/* Definitions for ADC_Buffer1Sem05 */
osSemaphoreId_t ADC_Buffer1Sem05Handle;
const osSemaphoreAttr_t ADC_Buffer1Sem05_attributes = {
  .name = "ADC_Buffer1Sem05"
};
/* USER CODE BEGIN PV */

// Variables for ADC Conversion
uint32_t adc_buffer[ADC_BUFFER_SIZE];
float32_t adc_buffer_float[ADC_BUFFER_SIZE];
uint32_t adc_buffer_0[ADC_BUFFER_SIZE];
uint32_t adc_buffer_1[ADC_BUFFER_SIZE];
bool recording_mode = false;
bool flag_value, transfer_complete;

// Variables for FFT
//uint32_t fft_real_length = 4096;			      // Value for FFT initialization
float32_t fft_buffer[REAL_FFT_SIZE];          //fft_buffer be twice as large to account for complex values per sample
float32_t bin[FFT_SIZE];				              // Array of bin values
uint32_t bin_int[FFT_SIZE];				            // Array of bin integer values
int bin_point = 0;						                // Used to point to element in array
int offset = 165; 						                //variable noisefloor offset
uint32_t expected_bin = 511, testIndex = 0;		// Expected Bin value of frequency Input and index comparator
float32_t maxValue;						                // Maximum Magnitude found in bin array
uint32_t maxIndex = 0;					              // Index location of maximum magnitude
uint32_t ifftFlag = 0;					              // 0 for FFT, 1 for inverse FFT
uint32_t doBitReverse = 1;				            // Bit reversal parameter
bool frequency_detected = false;

// Variables for sending data
int counter = 0; // Counter for counting how many times we send 1.875 seconds worth of data

//local global buffer for copy of every other sample from the ADC
volatile int32_t adc_buffer_copy[ADC_BUFFER_SIZE];

// create global buffer to hold transmit data
volatile uint8_t transmit_buffer[TRANSMIT_BUFFER_SIZE];


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USB_OTG_HS_USB_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_UART4_Init(void);
void StartDefaultTask(void *argument);
void StartAudioCapTask(void *argument);
void StartFFTTask(void *argument);
void StartSendDataTask(void *argument);

/* USER CODE BEGIN PFP */

bool FrequencyDetected(float32_t data[REAL_FFT_SIZE]);
float32_t Magnitude(float32_t real, float32_t compl);
void low_pass_filter(float32_t* input, float32_t* output, uint32_t num_samples);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
arm_rfft_fast_instance_f32 fft_handler;
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
  MX_DMA_Init();
  MX_USB_OTG_HS_USB_Init();
  MX_ADC1_Init();
  MX_USART3_UART_Init();
  MX_TIM1_Init();
  MX_UART4_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_Base_Start(&htim1);
  HAL_TIM_OC_Start(&htim1, TIM_CHANNEL_1);

  //float32_t maxValue;

  // Initialize RFFT
  arm_rfft_fast_init_f32(&fft_handler, REAL_FFT_SIZE);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of AudioCapSem01 */
  AudioCapSem01Handle = osSemaphoreNew(1, 1, &AudioCapSem01_attributes);

  /* creation of FFTSem02 */
  FFTSem02Handle = osSemaphoreNew(1, 1, &FFTSem02_attributes);

  /* creation of SendDataSem03 */
  SendDataSem03Handle = osSemaphoreNew(1, 1, &SendDataSem03_attributes);

  /* creation of ADC_Buffer0Sem04 */
  ADC_Buffer0Sem04Handle = osSemaphoreNew(1, 1, &ADC_Buffer0Sem04_attributes);

  /* creation of ADC_Buffer1Sem05 */
  ADC_Buffer1Sem05Handle = osSemaphoreNew(1, 1, &ADC_Buffer1Sem05_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */

  // Initialize Sem values other than AudioCap to 0 before starting code
  osSemaphoreAcquire(FFTSem02Handle, osWaitForever);
  osSemaphoreAcquire(SendDataSem03Handle, osWaitForever);

  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of AudioCapTask */
  AudioCapTaskHandle = osThreadNew(StartAudioCapTask, NULL, &AudioCapTask_attributes);

  /* creation of FFTTask */
  FFTTaskHandle = osThreadNew(StartFFTTask, NULL, &FFTTask_attributes);

  /* creation of SendDataTask */
  SendDataTaskHandle = osThreadNew(StartSendDataTask, NULL, &SendDataTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();
  /* We should never get here as control is now taken by the scheduler */
  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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

  /*AXI clock gating */
  RCC->CKGAENR = 0xFFFFFFFF;

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 24;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
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

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_16B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T1_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISINGFALLING;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_ONESHOT;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 6000-1;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TOGGLE;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_OC_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 430000;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_HalfDuplex_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart3, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart3, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief USB_OTG_HS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_HS_USB_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_Init 0 */

  /* USER CODE END USB_OTG_HS_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_Init 1 */

  /* USER CODE END USB_OTG_HS_Init 1 */
  /* USER CODE BEGIN USB_OTG_HS_Init 2 */

  /* USER CODE END USB_OTG_HS_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_FS_PWR_EN_GPIO_Port, USB_FS_PWR_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_FS_PWR_EN_Pin */
  GPIO_InitStruct.Pin = USB_FS_PWR_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_FS_PWR_EN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin LD3_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_FS_OVCR_Pin */
  GPIO_InitStruct.Pin = USB_FS_OVCR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_FS_OVCR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_FS_VBUS_Pin */
  GPIO_InitStruct.Pin = USB_FS_VBUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_FS_VBUS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_FS_ID_Pin */
  GPIO_InitStruct.Pin = USB_FS_ID_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG1_HS;
  HAL_GPIO_Init(USB_FS_ID_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_FS_N_Pin USB_FS_P_Pin */
  GPIO_InitStruct.Pin = USB_FS_N_Pin|USB_FS_P_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
 * @brief Function detects frequencies between (0 - 8kHz) and returns boolean
 * @param
 * @retval boolean true or false
 */
bool FrequencyDetected(float32_t data[REAL_FFT_SIZE])
{
	// Process the data through the RFFT module. Will output elements that are Real and Imaginary
	// in fft_bufer as a single array same size as data[].
	//arm_rfft_fast_f32(&fft_handler, (float32_t *) data, fft_buffer, ifftFlag);
	arm_rfft_fast_f32(&fft_handler, adc_buffer_float, fft_buffer, ifftFlag);

	// Reset bin value and offset
	bin_point = 0;

	// Calculate magnitude for each bin using real and Imaginary numbers from fft_buffer output
	 for (int i=0; i< REAL_FFT_SIZE; i=i+2) {

		bin[bin_point] =((Magnitude(fft_buffer[i], fft_buffer[i+1])))-offset;
		// bin[bin_point has chance of rolling back if magnitude is not greater than offset (165)
		// If bin[point_point] rolls back set to 0

		/*
		if ((bin[bin_point] < 0) || (bin[bin_point] > 5000))
		{
			bin[bin_point]=0;
		}
		*/

		bin_point++;

	 }
	// Negate DC value
	bin[0] = 0;

	// Check highest magnitude in bins
	arm_max_f32(bin, FFT_SIZE, &maxValue, &maxIndex);

	// Correct index
	maxIndex += 1;

	bool threshold_crossed = false;

	// Going through bin array, checking if a magnitude crosses threshold of 40
	for(int j=1; j < (FFT_SIZE); j++){

		if(bin[j] >= 40)
		{
			threshold_crossed = true;
			break;
		}
	}

	//HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
	if(threshold_crossed == true)
	{
		   return true;
	}
	// if highest magnitude is at desired bin (wanted frequency) return true
	else if(threshold_crossed == false)
		{
			return false;
		}

	//add return statement
}

/**
 * @brief Returns magnitude of FFT buffer outputs
 * @param Real & Complex elements of FFT output
 * @retval Magnitude at specific frequency
 */
float32_t Magnitude(float32_t real, float32_t compl)
{

	float32_t sqrt_input = (real*real + compl*compl);
	float32_t sqrt_output = 0;
	float32_t magnitude = 0;
	float32_t log_output = 0;

	arm_sqrt_f32(sqrt_input, &sqrt_output);
	log_output = logf(sqrt_output);
	magnitude = 20* (log_output);
	return magnitude;
}
// low_pass_filter(ADC_BUFFER_0, ADC_BUFFER_0, 4096)

void low_pass_filter(float32_t* input, float32_t* output, uint32_t num_samples) {
  float32_t32_t alpha = 2 * PI * CUTOFF_FREQ / SAMPLE_RATE;
  float32_t32_t b0 = (1 - arm_cos_f32(alpha)) / 2;
  float32_t32_t b1 = 1 - arm_cos_f32(alpha);
  float32_t32_t b2 = (1 - arm_cos_f32(alpha)) / 2;
  float32_t32_t a1 = -2 * arm_cos_f32(alpha);
  float32_t32_t a2 = arm_cos_f32(alpha) - 1;

  // Initialize filter state variables
  float32_t32_t x1 = 0, x2 = 0, y1 = 0, y2 = 0;

  // Apply the filter to the input samples
  for (uint32_t i = 0; i < num_samples; i++) {
    output[i] = b0 * input[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = input[i];
    y2 = y1;
    y1 = output[i];
  }
}


/**
 * @brief This function executes when adc buffer is full setting flag true
 * @param
 * @retval
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_8);
	flag_value = true;			// Set buffer conversion complete flag
	//HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_buffer, adc_buff_size);
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread. Toggling Red LED to ensure RTOS operation
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
	// Toggling LD3 (red) to see if it ever enters this default state
	//HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_14);
    //osDelay(500); /* Insert delay of 50ms */
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartAudioCapTask */
/**
* @brief Function implementing the AudioCapTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAudioCapTask */
void StartAudioCapTask(void *argument)
{
  /* USER CODE BEGIN StartAudioCapTask */
  
  //Fill ADC_Buffer1
  HAL_ADC_Start_DMA(&hadc1, adc_buffer_1, ADC_BUFFER_SIZE);

  //wait for it to finish
  while(flag_value != true);
  flag_value = false;

  //set initial adc buffer
  uint32_t adc_buffer_num = 0;

  /* Infinite loop */
  for(;;)
  {

	  //set record mode REMOVE, THIS IS FOR TESTING ONLY
	  //recording_mode = false;

	  //wait for fft or data sending task to complete
	  //osSemaphoreAcquire(AudioCapSem01Handle, osWaitForever);

	  /* Test Pin */
	  // HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
	  // osDelay(500);

	  // Start ADC using alternating buffers
    if(adc_buffer_num == 0) {

      //obtain adc_buffer_0
      osSemaphoreAcquire(ADC_Buffer0Sem04Handle, osWaitForever);

      //set buffer to fill
		  HAL_ADC_Start_DMA(&hadc1, adc_buffer_0, ADC_BUFFER_SIZE);

      //release tasks to process buffer 1
      //check for record mode
      if (recording_mode) {

        // Send the data
        osSemaphoreRelease(SendDataSem03Handle);
      }
      else  {
        
        // start fft task on adc buffer 1
        osSemaphoreRelease(FFTSem02Handle);
      }

      // Coming back from Send Data do we need to redo initializations?

      //wait for buffer to fill
      while(flag_value != true);
      flag_value = false;

      // Stop ADC
      HAL_ADC_Stop_DMA(&hadc1);
      
      //release adc_buffer_0
      osSemaphoreRelease(ADC_Buffer0Sem04Handle);

      //switch to other buffer on next loop
      adc_buffer_num = 1;

      //Wait for FFT Task to Finish
    }
    else {

      //obtain adc_buffer_1
      osSemaphoreAcquire(ADC_Buffer1Sem05Handle, osWaitForever);

      //set buffer to fill
		  HAL_ADC_Start_DMA(&hadc1, adc_buffer_1, ADC_BUFFER_SIZE);

      //release tasks to process buffer 0
      //check for record mode
      if (recording_mode) {

        // Send the data
        osSemaphoreRelease(SendDataSem03Handle);
      }
      else  {

        // start fft task on adc buffer 1
        osSemaphoreRelease(FFTSem02Handle);
      }

      // Coming back from Send Data do we need to redo initializations?

      //wait for buffer to fill
      while(flag_value != true);
      flag_value = false;

      // Stop ADC
      HAL_ADC_Stop_DMA(&hadc1);

      //release adc_buffer_1
      osSemaphoreRelease(ADC_Buffer1Sem05Handle);

      //switch to other buffer on next loop
      adc_buffer_num = 0;
    }
  }

  // In case we accidentally exit from task loop
  osThreadTerminate(NULL);
  /* USER CODE END StartAudioCapTask */
}

/* USER CODE BEGIN Header_StartFFTTask */
/**
* @brief Function implementing the FFTTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartFFTTask */
void StartFFTTask(void *argument)
{
  /* USER CODE BEGIN StartFFTTask */

  uint32_t count0;
  uint32_t count1;
  uint32_t none_acquired;

  /* Infinite loop */
  for(;;)
  {

    // wait for audio cap task to tell this task to start
    osSemaphoreAcquire(FFTSem02Handle, osWaitForever);
    HAL_GPIO_TogglePin(GPIOE, GPIO_PIN_1);
	  //osDelay(500);

    //check which adc buffer is available
    count0 = osSemaphoreGetCount(ADC_Buffer0Sem04Handle);
    count1 = osSemaphoreGetCount(ADC_Buffer1Sem05Handle);
    none_acquired = 0;

    //acquire whichever is available
    if(count0 == 1) {
      osSemaphoreAcquire(ADC_Buffer0Sem04Handle, osWaitForever);

      // Convert samples to float as required by FFT
		  for(int i = 0; i < REAL_FFT_SIZE; i++){
			  adc_buffer_float[i] = adc_buffer_0[i];
		  }
      
      //release adc buffer
      osSemaphoreRelease(ADC_Buffer0Sem04Handle);
      
    }
    else if (count1 == 1) {
      osSemaphoreAcquire(ADC_Buffer1Sem05Handle, osWaitForever);

      // Convert samples to float as required by FFT
      for(int i = 0; i < REAL_FFT_SIZE; i++){
			  adc_buffer_float[i] = adc_buffer_1[i];
		  }

      //release adc buffer
      osSemaphoreRelease(ADC_Buffer1Sem05Handle);
      
    }
    else {
      none_acquired = 1;
    }

    if (!none_acquired) {
      
      /* Reset frequency_detected bool */
      //frequency_detected = false; //not needed gets reasigned in frequency detected function

      /* Call FFT function that returns true if freqs between 0-8kHz are detected */
      frequency_detected = FrequencyDetected(adc_buffer_float);

      // TODO second frequency detected is redundant
      // if(frequency_detected == true){
      //   // HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, 1);
      //   // HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0);
      // }

      if(frequency_detected == true){
        recording_mode = true;
        // release semaphore for record task
      }  

      //start audio cap task -- should always be running, get rid of this
      //osSemaphoreRelease(AudioCapSem01Handle);
    }
  }

  // In case we accidentally exit from task loop
  osThreadTerminate(NULL);
  /* USER CODE END StartFFTTask */
}

/* USER CODE BEGIN Header_StartSendDataTask */
/**
* @brief Function implementing the SendDataTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSendDataTask */
void StartSendDataTask(void *argument)
{
  /* USER CODE BEGIN StartSendDataTask */
  /* Infinite loop */


  // variables to check which ADC buffer to use
  uint8_t count0;
  uint8_t count1;
  uint8_t none_acquired;

  // variables 
  uint8_t loop = 0;
  volatile uint8_t tx_data = 0;
  uint16_t i = 0;
  uint16_t j = 0;
  uint16_t max_transmit_index = 0;
  uint16_t amplification = 1;
  uint32_t val = 0;

  for(;;)
  {
    //wait for send data task to complete
    osSemaphoreAcquire(SendDataSem03Handle, osWaitForever);

    //check which adc buffer is available
    count0 = osSemaphoreGetCount(ADC_Buffer0Sem04Handle);
    count1 = osSemaphoreGetCount(ADC_Buffer1Sem05Handle);
    none_acquired = 0;



    //acquire whichever is available
    if(count0 == 1) {
      osSemaphoreAcquire(ADC_Buffer0Sem04Handle, osWaitForever);

      // new lowpass filter  : TO TEST
      low_pass_filter(ADC_BUFFER_0, ADC_BUFFER_0, 4096);

      //copy every other adc buffer
      for (int i = 0; i < TRANSMIT_BUFFER_SIZE; i+=2) {
        //adc_buffer_copy[i/2] = adc_buffer_0[i];
        val = adc_buffer_0[i];
        val -= 0x8000;
        val *= amplification;
        adc_buffer_copy[i/2] = val;
        //adc_buffer_copy[i/2] = (adc_buffer_copy[i/2] - 0x8000) * amplification;
      }

      //release adc buffer
      osSemaphoreRelease(ADC_Buffer0Sem04Handle);
      
    }
    else if (count1 == 1) {
      osSemaphoreAcquire(ADC_Buffer1Sem05Handle, osWaitForever);

      //copy every other adc buffer
      for (int i = 0; i < ADC_BUFFER_SIZE; i+=2) {
        adc_buffer_copy[i/2] = adc_buffer_1[i];
        adc_buffer_copy[i/2] = (adc_buffer_copy[i/2] - 0x8000) * amplification;
      }

      //release adc buffer
      osSemaphoreRelease(ADC_Buffer1Sem05Handle);
      
    }
    else {
      none_acquired = 1;
    }

    //transmit data
    if (!none_acquired) {

      //fill transmit buffer with data
      i = 0; //index of input buffer
      j = 0; //index of output buffer
      while (i < TRANSMIT_BUFFER_SIZE / 2) {

    	if (i > 2040) {
    		i++;
    		i--;
    	}
        
        //check if last 16 bits of data is the same as the escape char
        if (!((adc_buffer_copy[i] & 0xFFFF) ^ ESCAPE_CHAR)) {
          
          //add escape char twice to indicate that it is actually the data being sent
          transmit_buffer[j] = ESCAPE_CHAR >> 8;
          j++;
          transmit_buffer[j] = ESCAPE_CHAR & 0x00FF;
          j++;
          
          transmit_buffer[j] = ESCAPE_CHAR >> 8;
          j++;
          transmit_buffer[j] = ESCAPE_CHAR & 0x00FF;
          j++;

          //processed 1 sample
          i++;
        }
        else {

          //say sample = 0x1234;

          //add data to buffer
          transmit_buffer[j] = adc_buffer_copy[i] >> 8; // high byte (0x12)
          j++;
          transmit_buffer[j] = adc_buffer_copy[i] & 0x00FF; // low byte (0x34)
          j++;  

          //processed 1 sample
          i++;
        }
      }
      max_transmit_index = j;

      // First set of data
      if(loop == 0){

        //send escape character
        tx_data = 0xFF;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);
        tx_data = 0xFF;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);

        //send start char
        tx_data = 0xFF;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);
        tx_data = 0xEE;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);
      }

      // Data in between
      // TODO - For loop for number of samples of transmit_buffer
      for(int k = 0; k < max_transmit_index; k++){

        tx_data = transmit_buffer[k];
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);

        if (k > max_transmit_index - 2) {
        	k++;
        	k--;
        }
      }
      
      loop++;


      // Last set of data
      if(loop >= 58){

        //send escape char
        tx_data = 0xFF;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);
        tx_data = 0xFF;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);

        //send end char
        tx_data = 0xFF;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);
        tx_data = 0xFE;
        HAL_UART_Transmit(&huart4, (uint8_t *)&tx_data, 1, HAL_MAX_DELAY);

        //restart record mode
        loop = 0;
        recording_mode = false;
      }
    }
  }
  /* USER CODE END StartSendDataTask */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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

#ifdef  USE_FULL_ASSERT
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
