/**
  ******************************************************************************
  * @file    dht11.c
  * @brief   DHT11 single-bus driver implementation
  * @author  MengYang
  *
  * Hardware connection, timer PSC configuration, usage and return codes:
  * see the file header of dht11.h.
  *
  * ===================== Single-bus protocol timing (basis of this implementation) =====================
  *   1. Start signal: MCU pulls DATA low for 20ms (18~30ms per datasheet), then releases
  *   2. After releasing, the bus must be pulled high again (step 0),
  *      otherwise the residual 0V is mistaken for the sensor response
  *   3. Sensor response: low for 80us + high for 80us (datasheet says "about 80us",
  *      in practice it drifts to 70~90us, so the validation window is 60~100us)
  *   4. Data bits: each bit starts with a 50us low level, followed by a high level
  *      26~28us = 0, about 70us = 1 → 40us is used as the 0/1 threshold
  *   5. 40 bits in total: humidity integer + humidity decimal + temperature integer
  *      + temperature decimal + checksum
  *      checksum = low 8 bits of the sum of the first 4 bytes
  ******************************************************************************
  */

#include "dht11.h"

/**
 * @brief Initialize the DHT11: bind GPIO pin and timer, enable the GPIO clock
 * @param dht   DHT11 handle (must be zero-initialized with ={0} before use,
 *              otherwise garbage values are treated as the cache timestamp)
 * @param gpiox port of the DATA pin (GPIOA/B/C)
 * @param pin   pin of the DATA line
 * @param htim  timer used to measure pulse widths (PSC already set to 1us/tick, see dht11.h)
 * @note  The GPIO clock is enabled here, so the pin does not need to be configured in CubeMX
 */
void DHT11_Init(DHT11_HandleTypeDef* dht,GPIO_TypeDef* gpiox,uint16_t pin,TIM_HandleTypeDef* htim){
	dht->port = gpiox; 
	dht->pin = pin;
	dht->htim = htim;
	// Enable the port clock (only A/B/C handled; add a branch for other ports)
	if(dht->port == GPIOA){
		__HAL_RCC_GPIOA_CLK_ENABLE();
	}
	else if(dht->port == GPIOB){
		__HAL_RCC_GPIOB_CLK_ENABLE();
	}
	else if(dht->port == GPIOC){
		__HAL_RCC_GPIOC_CLK_ENABLE();
	}
//	__disable_irq();
	HAL_TIM_Base_Start(dht->htim); // start the counter used for pulse width measurement
}


/**
  * @brief  Switch the working mode of the DHT11 data pin (output/input)
  * @param  dht:  DHT11 handle (contains port and pin information)
  * @param  mode: DHT11_MODE_OUTPUT (MCU drives the bus to send the start signal)
  *               DHT11_MODE_INPUT  (release the bus, read the level driven by the sensor)
  * @retval none
  * @note   Only switches the GPIO mode, does not enable the GPIO clock —
  *         the clock is enabled by DHT11_Init
  */

void DHT11_SetGPIO_Mode(DHT11_HandleTypeDef* dht,uint8_t mode){
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	GPIO_InitStruct.Pin = dht->pin;
	if(mode == DHT11_MODE_OUTPUT){
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	}
	else if (mode == DHT11_MODE_INPUT){
		GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	}
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	
	HAL_GPIO_Init(dht->port,&GPIO_InitStruct);
}


/**
 * @brief Read temperature and humidity from the DHT11 once
 * @param dht initialized handle
 * @retval DHT11_OK=0 success; 1 invalid response pulse width; 2 timeout;
 *         3 checksum failed (see dht11.h for details)
 * @note   1. Repeated calls within 2 seconds return the previous result
 *            (cache), to avoid hammering the sensor
 *         2. The call interval must be >= 1s; after a failure the main loop
 *            must also wait 1s before retrying
 *         3. Returns if no edge transition occurs within the 200us timeout,
 *            never blocks forever
 */
DHT11_Status DHT11_read(DHT11_HandleTypeDef* dht){
	// Repeated calls within 2 seconds reuse the last result
	// (non-zero guard: first call has last_read_tick=0 so no cache is used)
	if (dht->last_read_tick != 0 &&
		HAL_GetTick() - dht->last_read_tick < 2000) {
		return DHT11_OK;
	}

	uint16_t trel = 0;
	uint16_t treh = 0;
	HAL_TIM_Base_Start(dht->htim);
	// Step 1: send the start signal
	/* The MCU pulls the single bus low for a period (18~30ms) to tell the
	   sensor that it is ready to receive data */
	DHT11_SetGPIO_Mode(dht,DHT11_MODE_OUTPUT);
	HAL_GPIO_WritePin(dht->port,dht->pin,GPIO_PIN_RESET);
	HAL_Delay(20);
		
	DHT11_SetGPIO_Mode(dht,DHT11_MODE_INPUT);
	
	// Step 2: release the bus and wait for the sensor's response signal

	// 0. First wait for the bus to be truly released high
	//    The start signal pulled the line low for 20ms, so right after
	//    switching to input the line is still at 0V; the pull-up needs time
	//    to charge it high before the sensor's response edge can be caught
	__HAL_TIM_SET_COUNTER(dht->htim, 0);
	while (HAL_GPIO_ReadPin(dht->port, dht->pin) == GPIO_PIN_RESET) {
		if (__HAL_TIM_GET_COUNTER(dht->htim) > DHT11_TIMEOUT_US) {
			HAL_TIM_Base_Stop(dht->htim);
			return DHT11_ERROR_TIMEOUT;   // not high within 200us = missing pull-up
		}
	}

	// 1. Wait for the sensor to pull the bus low (the response signal begins)

	__HAL_TIM_SET_COUNTER(dht->htim, 0);                           // clear the counter
	while (HAL_GPIO_ReadPin(dht->port, dht->pin) == GPIO_PIN_SET) { // wait for the pin to go low
		if (__HAL_TIM_GET_COUNTER(dht->htim) > DHT11_TIMEOUT_US) {
			HAL_TIM_Base_Stop(dht->htim);
//			__enable_irq();
			return DHT11_ERROR_TIMEOUT;                             // timeout, sensor not responding
		}
	}		
	// 2. Wait for the DHT11 to pull low for 80us then go high
	__HAL_TIM_SET_COUNTER(dht->htim, 0);                            // clear the counter
	while (HAL_GPIO_ReadPin(dht->port, dht->pin) == GPIO_PIN_RESET) { // wait for the pin to go high
		if (__HAL_TIM_GET_COUNTER(dht->htim) > DHT11_TIMEOUT_US) {
			HAL_TIM_Base_Stop(dht->htim);
//			__enable_irq();
			return DHT11_ERROR_TIMEOUT;                             // timeout: low level persisted too long
		}
	}
	trel = (uint16_t)__HAL_TIM_GET_COUNTER(dht->htim);
	// 3. Wait for the DHT11 to pull high for 80us then go low (data bits about to start)
	__HAL_TIM_SET_COUNTER(dht->htim, 0);                            // clear the counter
	while (HAL_GPIO_ReadPin(dht->port, dht->pin) == GPIO_PIN_SET) {  // wait for the pin to go low
		if (__HAL_TIM_GET_COUNTER(dht->htim) > DHT11_TIMEOUT_US) {
			HAL_TIM_Base_Stop(dht->htim);
//			__enable_irq();
			return DHT11_ERROR_TIMEOUT;                             // timeout: high level persisted too long
		}
	}
	treh = (uint16_t)__HAL_TIM_GET_COUNTER(dht->htim);
	
	/*
	The 80us in the DHT11 datasheet is a "typical value", not a hard
	threshold. The datasheet says "about 80us", but in reality it
	fluctuates between 70 and 90us. Writing a strict comparison like
	if (t_high == 80) would misjudge easily.
	*/
		
	 if ((trel < 60 || trel > 100) || (treh < 60 || treh > 100)) {
			HAL_TIM_Base_Stop(dht->htim);
			dht->trel = trel;   // diagnosis: store the measured low-level duration for OLED display
			dht->treh = treh;   // diagnosis: store the measured high-level duration for OLED display
			return DHT11_ERROR; // invalid response
    }
	 
	// Step 3: the host continuously reads 40 bits of data
	uint8_t data[5] = {0};   // five bytes, exactly 40 bits
	
	for (int i = 0; i < 40; i++) {
			// 1. Wait for the low level to end (every bit starts with a 50us low level)
			__HAL_TIM_SET_COUNTER(dht->htim, 0);
			while (HAL_GPIO_ReadPin(dht->port, dht->pin) == GPIO_PIN_RESET) {
					if (__HAL_TIM_GET_COUNTER(dht->htim) > DHT11_TIMEOUT_US) {
						HAL_TIM_Base_Stop(dht->htim);
//						__enable_irq();
						return DHT11_ERROR_TIMEOUT;
					}
			}

			// 2. Starting at the high level, clear the counter and measure the high-level duration
			__HAL_TIM_SET_COUNTER(dht->htim, 0);
			while (HAL_GPIO_ReadPin(dht->port, dht->pin) == GPIO_PIN_SET) {
					if (__HAL_TIM_GET_COUNTER(dht->htim) > DHT11_TIMEOUT_US) {
						HAL_TIM_Base_Stop(dht->htim);
//						__enable_irq();
						return DHT11_ERROR_TIMEOUT;
					}
			}
			uint16_t duration = __HAL_TIM_GET_COUNTER(dht->htim);

			// 3. Decide whether it is 0 or 1 and store it in the corresponding byte
			/*
			The high-level part of data bit BitX = "0" is at most 27us
			The high-level part of data bit BitX = "1" is at most 74us
			so 40us is used as the 0/1 threshold
			*/
			if (duration > 40) {
					data[i / 8] |= (1 << (7 - (i % 8)));   // data bit 1
			} else {
					data[i / 8] &= ~(1 << (7 - (i % 8)));  // data bit 0
			}
	}
	// Step 4: data checksum
	uint8_t genParity = data[0] + data[1] + data[2] + data[3];

	if (genParity != data[4]) {
		HAL_TIM_Base_Stop(dht->htim);
//		__enable_irq();
		return DHT11_ERROR_CHECKSUM;   // checksum failed, data cannot be trusted
	}
	// Step 5: update the temperature and humidity values
	dht->humidity    = data[0];   // humidity integer part
	dht->temperature = data[2];   // temperature integer part
	dht->last_read_tick = HAL_GetTick();   // record the time of this read
	return DHT11_OK;
}
