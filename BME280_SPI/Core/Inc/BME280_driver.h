/**
  ******************************************************************************
  * @file    BME280_driver.h
  * @brief   Driver for Bosch BME280 / BMP280 over SPI (4-wire, mode 0 or 3).
  *
  * Works with both parts. The BME280 is downward register compatible with the
  * BMP280 (datasheet section 5.2); the only difference is humidity, which the
  * BMP280 does not have. The chip ID read at init decides which is present and
  * the humidity path is skipped automatically on a BMP280.
  *
  *   chip ID 0x58 -> BMP280  (temperature + pressure)
  *   chip ID 0x60 -> BME280  (temperature + pressure + humidity)
  ******************************************************************************
  */
#ifndef BME280_DRIVER_H
#define BME280_DRIVER_H

#include "main.h"
#include <stdint.h>

/* --- SPI register access (datasheet 6.3) ----------------------------------
 * Only 7 bits of the register address are sent. Bit 7 of the control byte is
 * the direction: 1 = read, 0 = write. The sensor switches to SPI on the first
 * falling edge of CSB and stays there until it is power cycled.
 */
#define BME280_SPI_READ             0x80
#define BME280_SPI_WRITE_MASK       0x7F

/* --- Chip IDs, register 0xD0 --------------------------------------------- */
#define BME280_CHIPID_BMP280        0x58
#define BME280_CHIPID_BME280        0x60

/* --- Register map (datasheet Table 18) ----------------------------------- */
#define BME280_REG_CALIB00          0x88   /* dig_T1..dig_P9, then dig_H1     */
#define BME280_REG_ID               0xD0
#define BME280_REG_RESET            0xE0
#define BME280_REG_CALIB26          0xE1   /* dig_H2..dig_H6                  */
#define BME280_REG_CTRL_HUM         0xF2
#define BME280_REG_STATUS           0xF3
#define BME280_REG_CTRL_MEAS        0xF4
#define BME280_REG_CONFIG           0xF5
#define BME280_REG_PRESS_MSB        0xF7   /* start of the burst read         */

#define BME280_RESET_WORD           0xB6   /* write to 0xE0 for a soft reset  */

/* --- ctrl_meas / ctrl_hum oversampling values ---------------------------- */
#define BME280_OSRS_SKIP            0x00
#define BME280_OSRS_X1              0x01
#define BME280_OSRS_X2              0x02
#define BME280_OSRS_X4              0x03
#define BME280_OSRS_X8              0x04
#define BME280_OSRS_X16             0x05

/* --- Power modes, ctrl_meas[1:0] ----------------------------------------- */
#define BME280_MODE_SLEEP           0x00
#define BME280_MODE_FORCED          0x01
#define BME280_MODE_NORMAL          0x03

/* --- config[7:5], standby time between measurements in normal mode ------- */
#define BME280_STANDBY_0_5MS        0x00
#define BME280_STANDBY_62_5MS       0x01
#define BME280_STANDBY_125MS        0x02
#define BME280_STANDBY_250MS        0x03
#define BME280_STANDBY_500MS        0x04
#define BME280_STANDBY_1000MS       0x05

/* --- config[4:2], IIR filter coefficient --------------------------------- */
#define BME280_FILTER_OFF           0x00
#define BME280_FILTER_X2            0x01
#define BME280_FILTER_X4            0x02
#define BME280_FILTER_X8            0x03
#define BME280_FILTER_X16           0x04

/**
  * @brief One set of compensated readings.
  *
  * Fixed point throughout - no floating point is used anywhere in this driver,
  * which keeps it cheap on a Cortex-M4 without an FPU enabled.
  */
typedef struct
{
  int32_t  temperature;   /* hundredths of a degree C: 2143 -> 21.43 C        */
  uint32_t pressure;      /* whole Pascals:           96386 -> 963.86 hPa     */
  uint32_t humidity;      /* hundredths of a %RH:      4633 -> 46.33 %RH      */
                          /* humidity stays 0 on a BMP280                     */
} BME280_Data;

/**
  * @brief Probe the sensor, reset it, read its calibration and start it measuring.
  * @param hspi     initialised SPI handle, 8-bit frames, CPOL/CPHA 0/0 or 1/1
  * @param cs_port  GPIO port of the chip-select pin, e.g. BME_CS_GPIO_Port
  * @param cs_pin   chip-select pin, e.g. BME_CS_Pin (must idle high)
  * @retval HAL_OK on success, HAL_ERROR if the chip ID is not recognised
  */
HAL_StatusTypeDef BME280_Init(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);

/**
  * @brief Read and compensate one full set of measurements.
  * @param out  destination, untouched if the call fails
  */
HAL_StatusTypeDef BME280_ReadAll(BME280_Data *out);

/** @brief Chip ID read during init: 0x58 (BMP280) or 0x60 (BME280). */
uint8_t BME280_GetChipID(void);

/** @brief 1 if a BME280 with humidity is present, 0 for a BMP280. */
uint8_t BME280_HasHumidity(void);

#endif /* BME280_DRIVER_H */
