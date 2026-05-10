/* USER CODE BEGIN Header */
/**
  * @file           : main.c
  * @brief          : 
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN Includes */
#include "max30102.h"
#include "u8g2.h"
#include "display.h"
#include "hrv.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;
DMA_HandleTypeDef hdma_i2c1_rx;
TIM_HandleTypeDef htim1;      
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
u8g2_t  u8g2;
HRV_t   hrv;
static int32_t  ppg_prev      = 0;
static int32_t  ppg_prev2     = 0;
static uint32_t last_peak_ms  = 0;
static int32_t  ppg_min       = 0;
static int32_t  ppg_max       = 0;
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
static uint8_t  prev_stress_high = 0; 
static uint8_t  melody_playing   = 0; 
typedef enum {
    BREATH_IDLE = 0,
    BREATH_INHALE,
    BREATH_HOLD,
    BREATH_EXHALE
} BreathState_t;

static BreathState_t breath_state = BREATH_IDLE;

static uint32_t breath_timer = 0;
static uint8_t  breath_radius = 8;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C3_Init(void);
static void MX_TIM2_Init(void);

/* USER CODE BEGIN PFP */
uint8_t u8x8_byte_stm32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_gpio_delay_stm32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
void Breathing_Update(uint32_t now, uint8_t stress);
void draw_breathing(u8g2_t *u8g2);
static void MX_TIM1_Init(void);       
void Melody_Start(void);              
void Melody_Stop(void);             
void Melody_Tick(uint32_t now);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

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
static void Buzzer_SetFreq(uint32_t hz)
{
    if (hz == 0) {
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
        return;
    }
    uint32_t period = (1000000UL / hz) - 1;
    __HAL_TIM_SET_AUTORELOAD(&htim1, period);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, period / 2);
}

typedef struct { uint32_t freq; uint32_t note_ms; uint32_t gap_ms; } MelodyNote_t;

static const MelodyNote_t melody[] = {
    { 523, 350, 60  },   
    { 659, 350, 60  },   
    { 784, 350, 60  },  
    { 880, 500, 80  },   
    { 784, 350, 60  },  
    { 659, 350, 60  },  
    { 523, 350, 60  },  
    { 440, 500, 300 },   
};
#define MELODY_LEN (sizeof(melody) / sizeof(melody[0]))

typedef enum { MEL_IDLE = 0, MEL_PLAYING_NOTE, MEL_GAP } MelState_t;

static MelState_t mel_state  = MEL_IDLE;
static uint8_t    mel_idx    = 0;
static uint32_t   mel_timer  = 0;
static uint8_t    mel_active = 0;

void Melody_Start(void) { mel_active = 1; mel_idx = 0; mel_state = MEL_IDLE; }
void Melody_Stop(void)  { mel_active = 0; mel_state = MEL_IDLE; Buzzer_SetFreq(0); }

void Melody_Tick(uint32_t now)
{
    if (!mel_active) return;
    switch (mel_state) {
        case MEL_IDLE:
            Buzzer_SetFreq(melody[mel_idx].freq);
            mel_timer = now;
            mel_state = MEL_PLAYING_NOTE;
            break;
        case MEL_PLAYING_NOTE:
            if (now - mel_timer >= melody[mel_idx].note_ms) {
                Buzzer_SetFreq(0);
                mel_timer = now;
                mel_state = MEL_GAP;
            }
            break;
        case MEL_GAP:
            if (now - mel_timer >= melody[mel_idx].gap_ms) {
                mel_idx++;
                if (mel_idx >= MELODY_LEN)
                    Melody_Stop();          
                else
                    mel_state = MEL_IDLE;
            }
            break;
    }
}

/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART2_UART_Init();
    HAL_Delay(10);

    DBG("\r\n** BOOT **\r\n");
    DBG("CLK: %uHz\r\n", (unsigned int)SystemCoreClock);

    stage("LED");
    for (int i = 0; i < 6; i++) {
        HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
        HAL_Delay(100);
    }
stage("I2C3");
    MX_I2C3_Init();
    i2c_scan(&hi2c3, "I2C3");  

    stage("I2C1");
    MX_I2C1_Init();
    i2c_scan(&hi2c1, "I2C1");   

    stage("TIM2");
    MX_TIM2_Init();
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
		
		stage("BUZZER");        
		MX_TIM1_Init();   
		
    stage("HRV");
    memset(&hrv, 0, sizeof(hrv));

    uint32_t dbg_status_timer = HAL_GetTick();

    while (1)
    {
        uint32_t now = HAL_GetTick();
				Melody_Tick(now);             
			
        if (now - t_fifo >= 2)
        {
            t_fifo = now;

            uint8_t navail = MAX30102_SamplesAvailable(&hi2c1);

            if (navail == 0) {
                dbg_fifo_empty++;
            } else {
								for (uint8_t s = 0; s < navail; s++)                {
                    uint32_t r_v = 0, i_v = 0;
                    if (MAX30102_ReadRaw(&hi2c1, &r_v, &i_v) != HAL_OK) {
                        dbg_fifo_errors++;
                        break;
                    }
                    dbg_fifo_reads++;
                    last_ir = (int32_t)i_v;
										if (i_v < 10000) { 
                        if (dbg_fifo_reads % 100 == 1) DBG("!FINGER OFF\r\n");
                        ppg_max = 0; 
                        ppg_min = 0;
                    }
                    int32_t flt = process_ppg_signal((int32_t)i_v);
                    flt = -flt;
                    wave_buf[wave_idx % 128] = flt;
                    wave_idx++;

                    if (dbg_fifo_reads == 1) { ppg_max = flt; ppg_min = flt; }
                    if (flt > ppg_max) ppg_max = flt;
                    if (flt < ppg_min) ppg_min = flt;

                    if (dbg_fifo_reads % 100 == 0) {
                        int32_t mid = (ppg_max + ppg_min) / 2;
                        
                        ppg_max = mid + ((ppg_max - mid) * 3) / 4; 
                        ppg_min = mid + ((ppg_min - mid) * 3) / 4;
                        
                        if (ppg_max - ppg_min < 50) {
                            ppg_max += 25;
                            ppg_min -= 25;
                        }
                    }

                    int32_t range     = ppg_max - ppg_min;
                    int32_t threshold = ppg_min + (range * 6 / 10);

                    if (ppg_prev2 < ppg_prev &&
                        ppg_prev  > flt       &&
                        ppg_prev  > threshold &&
                        ppg_prev  > 0         &&
                        range     > 30)
                    {
                        uint32_t current_sample_ms = HAL_GetTick();

                        uint32_t gap_ms = current_sample_ms - last_peak_ms;

                        if (last_peak_ms == 0) {
                            last_peak_ms = current_sample_ms;
                        } else if (gap_ms >= 400 && gap_ms <= 2000) {
                            uint32_t ts = current_sample_ms * 1000; 
                            HRV_OnBeat(&hrv, ts);
                            dbg_beat_count++;
                            DBG("BEAT %u bpm\r\n", (unsigned int)(60000UL / gap_ms));
                            last_peak_ms = current_sample_ms;
                        } else if (gap_ms > 2000) {
                            last_peak_ms = current_sample_ms;
                        }
                    }

                    ppg_prev2 = ppg_prev;
                    ppg_prev  = flt;
                }
            }

						if (navail >= 30) {
								DBG("!FIFO OVF\r\n");

								uint32_t dummy_r, dummy_i;

								while (MAX30102_SamplesAvailable(&hi2c1) > 0) {
										MAX30102_ReadRaw(&hi2c1, &dummy_r, &dummy_i);
								}
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
								} else {
										DBG("CALCULATING... (Need more beats)\r\n");
								}
						}

										if (now - t_disp >= 80)
										{
												t_disp = now;
												dbg_disp_count++;

												Breathing_Update(now, hrv.stress_index);

											uint8_t stress_high = (hrv.stress_index > 70);
											if (stress_high && !prev_stress_high) {          
													DBG("STRESS: melody start\r\n");            
													Melody_Start();                             
											}                                               
											prev_stress_high = stress_high;      

												if (hrv.stress_index > 70)
												{
														draw_breathing(&u8g2);
												}
												else
												{
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

										if (now - dbg_status_timer >= 2000)
										{
												dbg_status_timer = now;
						DBG("ST| MAX:%s IR:%ld OK:%u B:%u\r\n",
														max_ok ? "OK" : "FAIL",
														(long)last_ir,
														(unsigned int)dbg_fifo_reads,
														(unsigned int)dbg_beat_count);
												HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
										}
								}
						}

/* Peripheral Inits */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) Error_Handler();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
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
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
    HAL_RCCEx_EnableMSIPLLMode();
}

static void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0xC0201A20;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_I2C3_Init(void) {
    hi2c3.Instance = I2C3;
    hi2c3.Init.Timing = 0x10D19CE4;
    hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    if (HAL_I2C_Init(&hi2c3) != HAL_OK) Error_Handler();
}

static void MX_TIM2_Init(void) {
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 79;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFF;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();
}
static void MX_TIM1_Init(void)                    
{
    __HAL_RCC_TIM1_CLK_ENABLE();
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 79;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 999;
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&htim1);
    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
}
static void MX_USART2_UART_Init(void) {
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LD3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    (void)GPIO_Pin;

}

void Error_Handler(void) {
    while (1) {
        HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
        for (volatile uint32_t d = 0; d < 800000UL; d++);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    DBG("[ASSERT FAILED] %s : line %u\r\n", file, line);
}
#endif