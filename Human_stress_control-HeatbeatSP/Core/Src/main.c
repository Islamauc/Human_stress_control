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
#include "max30102.h"
#include "u8g2.h"
#include "display.h"
#include "hrv.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BUZZER_FREQ(f)  do { \
    __HAL_TIM_SET_AUTORELOAD(&htim16, (80000000UL / (79+1)) / (f) - 1); \
    __HAL_TIM_SET_COMPARE(&htim16, TIM_CHANNEL_1, \
    (__HAL_TIM_GET_AUTORELOAD(&htim16) + 1) / 2); \
} while(0)

#define BUZZER_STOP()   HAL_TIM_PWM_Stop(&htim16, TIM_CHANNEL_1)
#define BUZZER_START()  HAL_TIM_PWM_Start(&htim16, TIM_CHANNEL_1)
// The musical notes
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_G4_  415  
#define NOTE_A4_  466  
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;
DMA_HandleTypeDef hdma_i2c1_rx;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim6;
TIM_HandleTypeDef htim16;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
u8g2_t  u8g2;
HRV_t   hrv;

static uint32_t live_bpm = 0;
static int32_t  ppg_prev      = 0;
static int32_t  ppg_prev2     = 0;
static uint32_t last_peak_ms  = 0;
static int32_t  ppg_min       = 0;
static int32_t  ppg_max       = 0;
static uint8_t recalibrate_peaks = 0;

static uint32_t last_beat_time = 0;
static int32_t dc_estimate = 0;
static int32_t prev_filtered = 0;
static int32_t derivative = 0;
static int32_t prev_derivative = 0;

#define REFRACTORY_MS 300



uint32_t t_fifo = 0;
uint32_t t_hrv  = 0;
uint32_t t_disp = 0;

int32_t  wave_buf[128] = {0};
uint8_t  wave_idx      = 0;

static uint32_t dbg_fifo_reads   = 0;
static uint32_t dbg_fifo_empty   = 0;
static uint32_t dbg_fifo_errors  = 0;
static uint32_t dbg_beat_count   = 0;
static uint32_t dbg_disp_count   = 0;
static uint8_t  max_ok           = 0; 
static int32_t  last_ir          = 0;
typedef enum {
    BREATH_IDLE = 0,
    BREATH_INHALE,
    BREATH_HOLD,
    BREATH_EXHALE
} BreathState_t;

static BreathState_t breath_state = BREATH_IDLE;

static uint32_t breath_timer = 0;
static uint8_t  breath_radius = 8;
static uint8_t finger_detected = 0;

static uint32_t buzzer_timer    = 0;
static uint8_t  buzzer_active   = 0;
static uint32_t buzzer_duration = 0;
static BreathState_t prev_breath_state = BREATH_IDLE;
static const uint16_t calm_melody[][2] = {
    {NOTE_C4, 1200}, {NOTE_E4, 1200}, {NOTE_G4, 1200},
    {NOTE_A4, 1600}, {0,        400}, 
    {NOTE_G4, 1200}, {NOTE_E4, 1200}, {NOTE_D4, 1200},
    {NOTE_C4, 2000}, {0,        600},
    {NOTE_E4, 1200}, {NOTE_G4, 1200}, {NOTE_A4, 1200},
    {NOTE_C5, 2000}, {0,        600},
    {NOTE_A4, 1200}, {NOTE_G4, 1200}, {NOTE_E4, 1200},
    {NOTE_C4, 2400}, {0,       1000}
};
#define MELODY_LEN (sizeof(calm_melody) / sizeof(calm_melody[0]))

static uint8_t  melody_note_idx  = 0;
static uint32_t melody_note_time = 0;
static uint8_t  melody_playing   = 0;
static uint32_t dbg_status_timer = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C3_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM6_Init(void);
static void MX_TIM16_Init(void);
/* USER CODE BEGIN PFP */
uint8_t u8x8_byte_stm32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_delay_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
void Breathing_Update(uint32_t now, uint8_t stress);
void draw_breathing(u8g2_t *u8g2);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define FINGER_THRESHOLD 30000UL
#define DBG(fmt, ...) \
    do { \
        char _b[80]; \
        int  _n = snprintf(_b, sizeof(_b), fmt, ##__VA_ARGS__); \
        HAL_UART_Transmit(&huart2, (uint8_t*)_b, (uint16_t)_n, 100); \
    } while(0)

static void stage(const char *name) {
    DBG("> STG: %s\r\n", name);
}

static void i2c_scan(I2C_HandleTypeDef *hi2c, const char *bus) {
    DBG("Scan %s:\r\n", bus);
    uint8_t found = 0;
    for (uint16_t addr = 1; addr < 128; addr++) {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(addr << 1), 3, 5) == HAL_OK) {
            DBG(" +0x%02X\r\n", addr);
            found++;
        }
    }
    if (found == 0) DBG(" NONE\r\n");
}

static void max30102_reg_dump(I2C_HandleTypeDef *hi2c) {
    uint8_t regs[] = {0x00, 0x01, 0x04, 0x05, 0x06, 0x09, 0x0A, 0x0C, 0x0D, 0xFF};
    uint8_t val;
    DBG("REGS:\r\n");
    for (uint8_t i = 0; i < 10; i++) {
        if (HAL_I2C_Mem_Read(hi2c, 0xAE, regs[i], 1, &val, 1, 20) == HAL_OK)
            DBG(" %02X:%02X\r\n", regs[i], val);
    }
}
void Breathing_Update(uint32_t now, uint8_t stress)
{
    if (stress < 70) {
        breath_state = BREATH_IDLE;
        breath_radius = 8;
        return;
    }

    if (breath_state == BREATH_IDLE) {
        breath_state = BREATH_INHALE;
        breath_timer = now;
    }

    switch (breath_state)
    {
        case BREATH_INHALE:

            if (breath_radius < 24)
                breath_radius++;

            if (now - breath_timer >= 4000) {
                breath_state = BREATH_HOLD;
                breath_timer = now;
            }
            break;

        case BREATH_HOLD:

            if (now - breath_timer >= 2000) {
                breath_state = BREATH_EXHALE;
                breath_timer = now;
            }
            break;

        case BREATH_EXHALE:

            if (breath_radius > 8)
                breath_radius--;

            if (now - breath_timer >= 6000) {
                breath_state = BREATH_INHALE;
                breath_timer = now;
            }
            break;

        default:
            break;
    }
}

void draw_breathing(u8g2_t *u8g2)
{
    u8g2_ClearBuffer(u8g2);

    u8g2_SetFont(u8g2, u8g2_font_ncenB08_tr);

    switch (breath_state)
    {
        case BREATH_INHALE:
            u8g2_DrawStr(u8g2, 34, 12, "INHALE");
            break;

        case BREATH_HOLD:
            u8g2_DrawStr(u8g2, 42, 12, "HOLD");
            break;

        case BREATH_EXHALE:
            u8g2_DrawStr(u8g2, 34, 12, "EXHALE");
            break;

        default:
            break;
    }

    u8g2_DrawCircle(u8g2, 64, 40, breath_radius, U8G2_DRAW_ALL);

    u8g2_SendBuffer(u8g2);
}
static uint32_t last_read_count = 0;
static uint32_t last_read_time  = 0;

void Music_Start(void)
{
    melody_playing   = 1;
    melody_note_idx  = 0;
    melody_note_time = HAL_GetTick();

    if (calm_melody[0][0] == 0) {
        BUZZER_STOP();
    } else {
        BUZZER_FREQ(calm_melody[0][0]);
        BUZZER_START();
    }
    DBG("MUSIC START\r\n");
}

void Music_Stop(void)
{
    melody_playing = 0;
    BUZZER_STOP();
    DBG("MUSIC STOP\r\n");
}

void Music_Update(uint32_t now)
{
    if (!melody_playing) return;

    uint32_t duration = calm_melody[melody_note_idx][1];

    if (now - melody_note_time >= duration)
    {
        melody_note_idx = (melody_note_idx + 1) % MELODY_LEN;
        melody_note_time = now;

        uint16_t freq = calm_melody[melody_note_idx][0];

        if (freq == 0) {
            BUZZER_STOP();  
        } else {
            BUZZER_FREQ(freq);
            BUZZER_START();
        }
    }
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
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_I2C3_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_TIM1_Init();
  MX_TIM6_Init();
  MX_TIM16_Init();
  /* USER CODE BEGIN 2 */
  HAL_Delay(10);

  DBG("\r\n** BOOT **\r\n");
  DBG("CLK: %uHz\r\n", (unsigned int)SystemCoreClock);

  stage("LED");
  for (int i = 0; i < 6; i++) {
      HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
      HAL_Delay(100);
  }

  stage("I2C3");
  i2c_scan(&hi2c3, "I2C3");

  stage("I2C1");
  i2c_scan(&hi2c1, "I2C1");

  stage("TIM2");
  if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
      DBG("TIM2 FAIL\r\n");
  }

  stage("MAX30102");
  if (MAX30102_Init(&hi2c1) != HAL_OK) {
      DBG("MAX FAIL\r\n");
      max_ok = 0;
  } else {
      DBG("MAX OK\r\n");
      max_ok = 1;
      max30102_reg_dump(&hi2c1);
  }

  stage("OLED");
  u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_stm32_hw_i2c, u8x8_gpio_delay_stm32);
  u8g2_InitDisplay(&u8g2);
  u8g2_SetPowerSave(&u8g2, 0);
  u8g2_ClearBuffer(&u8g2);
  u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
  u8g2_DrawStr(&u8g2, 10, 35, max_ok ? "MAX OK" : "MAX FAIL");
  u8g2_SendBuffer(&u8g2);
  HAL_Delay(1000);

  stage("HRV");
  memset(&hrv, 0, sizeof(hrv));
	
		stage("TIM16 PWM");
		HAL_TIM_Base_Start(&htim16);
		BUZZER_STOP();
		DBG("BUZZER OK\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    uint32_t now = HAL_GetTick();
		Music_Update(now);
    if (now - t_fifo >= 10)
    {
        t_fifo = now;

        uint8_t navail = MAX30102_SamplesAvailable(&hi2c1);
        if (navail == 0xFF) {
            DBG("!I2C ERR - bus reset\r\n");
        }
        if (navail == 0) {
            dbg_fifo_empty++;
        } else {
            for (uint8_t s = 0; s < navail && s < 32; s++)
            {
                uint32_t r_v = 0, i_v = 0;
                if (MAX30102_ReadRaw(&hi2c1, &r_v, &i_v) != HAL_OK) {
                    dbg_fifo_errors++;
                    break;
                }
                dbg_fifo_reads++;
                last_ir = (int32_t)i_v;

                if (i_v < FINGER_THRESHOLD) {
                    if (finger_detected == 1) {
                        DBG("FINGER REMOVED\r\n");
                        finger_detected = 0;
                    }
                } else {
                    if (finger_detected == 0) {
                        DBG("FINGER DETECTED - Resetting HRV\r\n");
                        finger_detected = 1;
                        memset(&hrv, 0, sizeof(hrv));
                        wave_idx = 0;
                        memset(wave_buf, 0, sizeof(wave_buf));
											  recalibrate_peaks = 1;
												prev_filtered   = 0;
												derivative      = 0;
												prev_derivative = 0;
												last_beat_time  = 0;
                    }
                }


                if (i_v < 1000 && (dbg_fifo_reads % 100 == 1)) DBG("!FINGER\r\n");

                int32_t flt = process_ppg_signal((int32_t)i_v);
                flt = -flt;
								derivative = flt - prev_filtered;
								prev_filtered = flt;
								
								if (recalibrate_peaks) {
									ppg_max = flt;
									ppg_min = flt;
									ppg_prev = flt;
									ppg_prev2 = flt;
									last_peak_ms = now;
									recalibrate_peaks = 0; 
							}
                wave_buf[wave_idx % 128] = flt;
                wave_idx++;

                if (dbg_fifo_reads == 1) { ppg_max = flt; ppg_min = flt; }
                if (flt > ppg_max) ppg_max = flt;
                if (flt < ppg_min) ppg_min = flt;

                if (dbg_fifo_reads % 500 == 0) {
                    int32_t mid = (ppg_max + ppg_min) / 2;
                    ppg_max = mid + ((ppg_max - mid) * 15) / 16;
                    ppg_min = mid + ((ppg_min - mid) * 15) / 16;
                }

                int32_t range     = ppg_max - ppg_min;
                int32_t threshold = ppg_min + (range * 7 / 10);

                int32_t dynamic_threshold = ppg_min + (range * 3 / 5);

uint32_t gap_ms = now - last_beat_time;

if (
    prev_derivative > 0 &&
    derivative <= 0 &&
    flt > dynamic_threshold &&
    range > 150 &&
    gap_ms > REFRACTORY_MS
)
{
    if (last_beat_time != 0)
    {
        uint32_t rr = gap_ms;

        if (rr >= 300 && rr <= 2000)
        {
            uint32_t ts = __HAL_TIM_GET_COUNTER(&htim2);
            HRV_OnBeat(&hrv, ts);
            live_bpm = 60000UL / rr;

            dbg_beat_count++;
            DBG("RR=%lu ms BPM=%lu\r\n",
                rr,
                live_bpm);
        }
    }

    last_beat_time = now;
}

prev_derivative = derivative;

                ppg_prev2 = ppg_prev;
                ppg_prev  = flt;
            }
        }

        if (navail >= 30) {
            DBG("!FIFO OVF - Hard Reset Logic\r\n");

            uint8_t zero = 0x00;
            uint8_t dummy;

            HAL_I2C_Mem_Read(&hi2c1, 0xAE, 0x00, 1, &dummy, 1, 10);
            HAL_I2C_Mem_Read(&hi2c1, 0xAE, 0x01, 1, &dummy, 1, 10);

            HAL_I2C_Mem_Write(&hi2c1, 0xAE, 0x04, 1, &zero, 1, 10);
            HAL_I2C_Mem_Write(&hi2c1, 0xAE, 0x05, 1, &zero, 1, 10);
            HAL_I2C_Mem_Write(&hi2c1, 0xAE, 0x06, 1, &zero, 1, 10);

            ppg_prev  = 0;
            ppg_prev2 = 0;
        }
    }

    if (now - t_hrv >= 30000)
    {
        t_hrv = now;
        HRV_Compute(&hrv);

        if (hrv.valid) {
            const char* status_label;

            if (hrv.stress_index > 70)      status_label = "HIGH STRESS";
            else if (hrv.stress_index > 40) status_label = "MEDIUM";
            else if (hrv.stress_index > 20) status_label = "RELAXED";
            else                            status_label = "VERY RELAXED";

            DBG("\r\n--- BIOMETRIC STATUS ---\r\n");
            DBG("MAX30102: %s\r\n", max_ok ? "OK" : "ERROR");
            DBG("HEART RATE: %u BPM\r\n", hrv.hr_bpm);
            DBG("STRESS:     %u/100\r\n", hrv.stress_index);
            DBG("STATE:      %s\r\n", status_label);
            DBG("------------------------\r\n");
					char ble_buf[48];
					int  ble_len = snprintf(ble_buf, sizeof(ble_buf),
                            "$HRV,%u,%u,%u\r\n",
                            hrv.hr_bpm,
                            hrv.stress_index,
                            (unsigned int)finger_detected);
					HAL_UART_Transmit(&huart1, (uint8_t*)ble_buf, ble_len, 100);
					DBG("BLE SENT: $HRV,%u,%u,%u\r\n", hrv.hr_bpm, hrv.stress_index, (unsigned int)finger_detected);
        } else {
            DBG("CALCULATING... (Need more beats)\r\n");
        }
    }

if (now - t_disp >= 80)
    {
        t_disp = now;
                        
        if (!finger_detected)
        {
            u8g2_ClearBuffer(&u8g2);
            u8g2_SetFont(&u8g2, u8g2_font_ncenB10_tr);
            u8g2_DrawStr(&u8g2, 15, 30, "PLACE FINGER");
            u8g2_DrawFrame(&u8g2, 0, 0, 128, 64);
            u8g2_SendBuffer(&u8g2);
            
            if (melody_playing) Music_Stop();
            prev_breath_state = BREATH_IDLE;
        }
        else if (!hrv.valid)
        {
            u8g2_ClearBuffer(&u8g2);
            u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
            u8g2_DrawStr(&u8g2, 15, 25, "ANALYZING...");
            u8g2_DrawStr(&u8g2, 15, 45, "Keep Still...");
            u8g2_SendBuffer(&u8g2);
            
            if (melody_playing) Music_Stop();
            prev_breath_state = BREATH_IDLE;
        }
        else if (hrv.stress_index > 70)
        {
            Breathing_Update(now, hrv.stress_index);
            draw_breathing(&u8g2);
            if (breath_state == BREATH_INHALE && prev_breath_state != BREATH_INHALE) {
                if (!melody_playing) Music_Start();
            }
            prev_breath_state = breath_state;
        }
        else
        {
            if (melody_playing) Music_Stop();
            prev_breath_state = BREATH_IDLE;
            
            int32_t render[128];
            for (int i = 0; i < 128; i++)
                render[i] = wave_buf[(wave_idx + i) % 128];

            display_update(&u8g2,
                           hrv.hr_bpm,
                           98,
                           hrv.stress_index,
                           render);
        }
    }
  /* USER CODE END 3 */
	}
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

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
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

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
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
  hi2c1.Init.Timing = 0xC0201A20;
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
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x10D19CE4;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

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

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
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
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 79;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 0xffffffff;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 0;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 65535;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 79;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 65535;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim16, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim16, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */
  HAL_TIM_MspPostInit(&htim16);

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
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
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
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

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
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

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
