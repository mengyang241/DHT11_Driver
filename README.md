# DHT11 STM32 Driver

> I am also a beginner learning STM32. This driver was written while learning,
> with comments kept as detailed as possible. I hope it helps others who are
> at the same stage — corrections and suggestions are welcome.

A HAL-based DHT11 single-bus temperature & humidity sensor driver for the
STM32F1 series (for other series, just recalculate the timer prescaler with
the formula below).

## Features

- Pure HAL implementation, polled reading, no interrupt dependency
- 200us hardware timeout protection: never hangs even if the sensor is
  disconnected or wired incorrectly
- Read cache: repeated calls within 2 seconds return the previous result
- Comments in English, with full configuration notes in the header file and
  protocol timing notes in the source file

## Hardware connection

| DHT11 pin | Connect to | Notes |
|-----------|-----------|-------|
| DATA      | any GPIO (e.g. PA0) | specified by the `DHT11_Init()` parameter |
| VCC       | 3.3V or 5V          | match the module's rated supply |
| GND       | GND                 | must share ground with the MCU |

★ **A pull-up resistor is required**: connect 4.7K ~ 10K between DATA and VCC.
DHT11 modules with a PCB usually have one onboard; bare sensors need an
external one. Without a pull-up the read returns a timeout error (the bus
cannot be pulled high after being released).

## Timer configuration (required)

The driver uses a timer counter to measure microsecond-level pulse widths,
so **1 counter tick must equal 1us**:

```
PSC (prescaler) = timer clock frequency / 1MHz - 1
```

STM32F1 timer clock: equals PCLK when the APB prescaler = 1; equals 2×PCLK
when the APB prescaler > 1.

This project: SYSCLK = 64MHz, APB2 prescaler = 1 → TIM1CLK = 64MHz
→ **Prescaler = 63, Period = 9999** in CubeMX.

After changing the system clock, the timer, or the APB prescaler you must
recalculate PSC, otherwise every pulse width measurement is scaled incorrectly.

## Directory structure

```
DHT11/
├─ Inc/
│  └─ dht11.h        driver header (wiring, timer, usage — all in the file header comment)
├─ Src/
│  └─ dht11.c        driver implementation (single-bus timing — in the file header comment)
├─ DHT11-Sensor-Library-for-STM32-main/   third-party reference library (not part of this driver)
└─ README.md
```

## Usage

```c
DHT11_HandleTypeDef dht = {0};                  // must be zero-initialized
DHT11_Init(&dht, GPIOA, GPIO_PIN_0, &htim1);    // bind pin + timer
HAL_Delay(1000);                                // wait at least 1s after power-up

while (1) {
    if (DHT11_read(&dht) == DHT11_OK) {
        // dht.humidity    humidity, integer %
        // dht.temperature temperature, integer °C
    }
    HAL_Delay(1000);                            // interval between reads must be >= 1s
}
```

- Both the pin and the timer can be changed: GPIO is a parameter of
  `DHT11_Init()`, and the timer just needs PSC configured in CubeMX before
  passing the handle
- The pin does not need to be configured in CubeMX: `DHT11_Init()` enables
  the GPIO clock and sets the mode automatically

## Return values

| Value | Name | Meaning |
|-------|------|---------|
| 0 | `DHT11_OK`             | read succeeded |
| 1 | `DHT11_ERROR`          | invalid response pulse width (see measured values in `dht->trel` / `dht->treh`) |
| 2 | `DHT11_ERROR_TIMEOUT`  | no edge transition in time: missing pull-up / broken wire / sensor not responding |
| 3 | `DHT11_ERROR_CHECKSUM` | 40-bit data checksum failed: noise / wire too long |

## Notes

- The read interval must be **≥ 1 second** (required by the DHT11 datasheet);
  after a failed read, wait 1 second before retrying
- The driver switches the GPIO output/input mode internally — do not change
  this pin's configuration from other code while a read is in progress
