/**
  ******************************************************************************
  * @file    dht11.h
  * @brief   DHT11 single-bus temperature & humidity sensor driver
  * @author  MengYang
  *
  * ============================ Hardware connection ============================
  *   DATA (single bus) ---> any GPIO, e.g. PA0 (specified by DHT11_Init() parameter)
  *   VCC              ---> 3.3V or 5V (match the module's rated supply)
  *   GND              ---> GND (must share ground with the MCU)
  *
  *   ★ A pull-up resistor is REQUIRED: connect 4.7K ~ 10K between DATA and VCC.
  *     DHT11 modules with a PCB usually have one onboard; bare sensors need an
  *     external one. Without a pull-up the read returns DHT11_ERROR_TIMEOUT(2)
  *     — the bus cannot be pulled high within 200us after being released
  *     (see step 0 in DHT11_read).
  *
  * ==================== Timer configuration (required) ====================
  *   The driver uses a timer counter to measure microsecond-level pulse widths,
  *   so **1 counter tick must equal 1us**:
  *
  *       PSC (prescaler) = timer clock frequency / 1MHz - 1
  *
  *   STM32F1 timer clock: equals PCLK when the APB prescaler = 1;
  *   equals 2×PCLK when the APB prescaler > 1 (see the RCC chapter of the
  *   reference manual).
  *
  *   This project: SYSCLK = 64MHz, APB2 prescaler = 1
  *     → TIM1CLK = 64MHz → Prescaler = 63, Period = 9999 in CubeMX
  *
  *   ★ After changing the system clock, the timer, or the APB prescaler you
  *     MUST recalculate PSC, otherwise every pulse width measurement is
  *     scaled incorrectly.
  *
  *   The timeout threshold DHT11_TIMEOUT_US = 200us, so ARR only needs an
  *   overflow period > 200us (9999 → 10ms here, plenty).
  *
  * ==================== GPIO configuration ====================
  *   Output phase: push-pull GPIO_MODE_OUTPUT_PP (sends the start signal,
  *                 switched automatically by the driver)
  *   Input phase:  floating input GPIO_MODE_INPUT + GPIO_NOPULL
  *                (the level depends entirely on the external pull-up, so
  *                 without a pull-up no reading is possible)
  *   GPIO clock: DHT11_Init() enables GPIOA/B/C automatically; the pin does
  *               not need to be configured in CubeMX
  *
  * ==================== Usage ====================
  *   DHT11_HandleTypeDef dht = {0};                     // ★ must be zero-initialized
  *   DHT11_Init(&dht, GPIOA, GPIO_PIN_0, &htim1);       // bind pin + timer
  *   HAL_Delay(1000);                                   // wait at least 1s after power-up
  *   while (1) {
  *       if (DHT11_read(&dht) == DHT11_OK) {
  *           dht.humidity;                              // humidity, integer %
  *           dht.temperature;                           // temperature, integer °C
  *       }
  *       HAL_Delay(1000);                               // ★ interval must be >= 1s
  *   }
  *
  * ==================== Return codes & troubleshooting ====================
  *   0 DHT11_OK             read succeeded
  *   1 DHT11_ERROR          response pulse width not in 60~100us → check trel/treh
  *   2 DHT11_ERROR_TIMEOUT  no edge transition in time:
  *                          step 0 timeout = missing pull-up / DATA wire broken
  *                          later timeout = sensor not responding (supply/wiring)
  *   3 DHT11_ERROR_CHECKSUM 40-bit data checksum failed → noise / long wire / bad timing
  ******************************************************************************
  */


#ifndef __DHT11_H
#define __DHT11_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* DHT11 return status codes */
typedef enum {
    DHT11_OK             = 0,   // read succeeded
    DHT11_ERROR          = 1,   // general error (invalid response pulse width)
    DHT11_ERROR_TIMEOUT  = 2,   // timed out waiting for an edge transition
    DHT11_ERROR_CHECKSUM = 3    // checksum mismatch, data cannot be trusted
} DHT11_Status;

#define DHT11_TIMEOUT_US  200   // timeout threshold in microseconds


typedef struct{
	GPIO_TypeDef* port;
	uint16_t pin;
	TIM_HandleTypeDef* htim;
	uint8_t temperature;
	uint8_t humidity;
	uint32_t last_read_tick;
	uint16_t trel;   // diagnosis: measured low-level duration of the response (us)
	uint16_t treh;   // diagnosis: measured high-level duration of the response (us)

}DHT11_HandleTypeDef;

void DHT11_Init(DHT11_HandleTypeDef* dht,GPIO_TypeDef* gpiox,uint16_t pin,TIM_HandleTypeDef* htim);
void DHT11_SetGPIO_Mode(DHT11_HandleTypeDef* dht,uint8_t mode);
DHT11_Status DHT11_read(DHT11_HandleTypeDef* dht);


/* Timer handle of the application layer (TIM1 measures pulse widths in this
   project); defined in main.c, declared here so callers can pass it to DHT11_Init */
extern TIM_HandleTypeDef htim1;

/* DHT11 data pin mode definitions */
#define DHT11_MODE_INPUT   0   // input mode: release the bus, the sensor drives the data line, MCU reads
#define DHT11_MODE_OUTPUT  1   // output mode: MCU drives the bus high/low to send the start signal

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H */
