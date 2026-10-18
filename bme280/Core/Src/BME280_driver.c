/**
  ******************************************************************************
  * @file    BME280_driver.c
  * @brief   Driver for Bosch BME280 / BMP280 over I2C.
  *
  * The compensation formulas are Bosch reference code, transcribed from
  * BME280 datasheet BST-BME280-DS002 rev 1.23, section 4.2.3. They are not
  * something to derive - the polynomials correct for per-die manufacturing
  * variation using trim constants burned into the NVM of each chip at the
  * factory.
  ******************************************************************************
  */

#include "BME280_driver.h"

/* --- Module state --------------------------------------------------------- */

static I2C_HandleTypeDef *s_hi2c    = NULL;
static uint8_t            s_addr8   = 0;    /* 7-bit address already shifted   */
static uint8_t            s_chip_id = 0;
static uint8_t            s_has_hum = 0;

#define BME280_TIMEOUT  100u   /* ms, generous for a 100 kHz bus              */

/* Calibration constants, read once at init.
 * Signedness here is not cosmetic - it is specified per-word in datasheet
 * Table 16, and getting one wrong produces readings that look plausible but
 * are quietly incorrect.
 */
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;
static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;

/* Fine temperature, carried from the temperature compensation into the
 * pressure and humidity ones. This coupling is why temperature must always be
 * compensated first.
 */
static int32_t t_fine;

/* --- I2C helpers ---------------------------------------------------------- */

static HAL_StatusTypeDef bme_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(s_hi2c, s_addr8, reg, I2C_MEMADD_SIZE_8BIT,
                          buf, len, BME280_TIMEOUT);
}

static HAL_StatusTypeDef bme_write(uint8_t reg, uint8_t val)
{
  return HAL_I2C_Mem_Write(s_hi2c, s_addr8, reg, I2C_MEMADD_SIZE_8BIT,
                           &val, 1, BME280_TIMEOUT);
}

/* --- Calibration readout (datasheet 4.2.2) -------------------------------- */

static HAL_StatusTypeDef bme_read_calibration(void)
{
  uint8_t buf[26];
  uint8_t h[7];

  /* 0x88..0xA1: dig_T1..dig_P9 in the first 24 bytes, 0xA0 reserved,
   * then dig_H1 at 0xA1. Every 16-bit word is little-endian, LSB first.
   */
  if (bme_read(BME280_REG_CALIB00, buf, 26) != HAL_OK) return HAL_ERROR;

  dig_T1 = (uint16_t)(buf[1]  << 8 | buf[0]);
  dig_T2 = (int16_t) (buf[3]  << 8 | buf[2]);
  dig_T3 = (int16_t) (buf[5]  << 8 | buf[4]);

  dig_P1 = (uint16_t)(buf[7]  << 8 | buf[6]);
  dig_P2 = (int16_t) (buf[9]  << 8 | buf[8]);
  dig_P3 = (int16_t) (buf[11] << 8 | buf[10]);
  dig_P4 = (int16_t) (buf[13] << 8 | buf[12]);
  dig_P5 = (int16_t) (buf[15] << 8 | buf[14]);
  dig_P6 = (int16_t) (buf[17] << 8 | buf[16]);
  dig_P7 = (int16_t) (buf[19] << 8 | buf[18]);
  dig_P8 = (int16_t) (buf[21] << 8 | buf[20]);
  dig_P9 = (int16_t) (buf[23] << 8 | buf[22]);

  dig_H1 = buf[25];

  if (!s_has_hum) return HAL_OK;   /* a BMP280 has no humidity block */

  /* 0xE1..0xE7. dig_H4 and dig_H5 are 12-bit values that share the two
   * nibbles of register 0xE5, which is why this block looks stranger than
   * the first one.
   */
  if (bme_read(BME280_REG_CALIB26, h, 7) != HAL_OK) return HAL_ERROR;

  dig_H2 = (int16_t)(h[1] << 8 | h[0]);
  dig_H3 = h[2];
  dig_H4 = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F));
  dig_H5 = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));
  dig_H6 = (int8_t)h[6];

  return HAL_OK;
}

/* --- Compensation formulas (datasheet 4.2.3, Bosch reference code) -------- */

/* Returns temperature in DegC, resolution 0.01 DegC. 5123 equals 51.23 DegC. */
static int32_t compensate_T(int32_t adc_T)
{
  int32_t var1, var2, T;

  var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
          ((int32_t)dig_T3)) >> 14;

  t_fine = var1 + var2;
  T = (t_fine * 5 + 128) >> 8;
  return T;
}

/* Returns pressure in Q24.8 format: 24674867 represents 96386.2 Pa. */
static uint32_t compensate_P(int32_t adc_P)
{
  int64_t var1, var2, p;

  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
  var2 = var2 + (((int64_t)dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;

  if (var1 == 0) return 0;   /* avoid division by zero */

  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);

  return (uint32_t)p;
}

/* Returns humidity in Q22.10 format: 47445 represents 46.333 %RH. */
static uint32_t compensate_H(int32_t adc_H)
{
  int32_t v_x1_u32r;

  v_x1_u32r = (t_fine - ((int32_t)76800));
  v_x1_u32r = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * v_x1_u32r)) +
                 ((int32_t)16384)) >> 15) *
               (((((((v_x1_u32r * ((int32_t)dig_H6)) >> 10) *
                    (((v_x1_u32r * ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                  ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14));
  v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                             ((int32_t)dig_H1)) >> 4));
  v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
  v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

  return (uint32_t)(v_x1_u32r >> 12);
}

/* --- Public API ----------------------------------------------------------- */

HAL_StatusTypeDef BME280_Init(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
  uint8_t tries;

  s_hi2c  = hi2c;
  s_addr8 = (uint8_t)(addr7 << 1);   /* the HAL wants the 8-bit address form */

  /* 1. Identify the part. */
  if (bme_read(BME280_REG_ID, &s_chip_id, 1) != HAL_OK) return HAL_ERROR;

  if (s_chip_id == BME280_CHIPID_BME280)
  {
    s_has_hum = 1;
  }
  else if (s_chip_id == BME280_CHIPID_BMP280 ||
           s_chip_id == 0x56 || s_chip_id == 0x57)   /* BMP280 eng. samples */
  {
    s_has_hum = 0;
  }
  else
  {
    return HAL_ERROR;   /* not a part we recognise */
  }

  /* 2. Soft reset, so we start from a known state regardless of what any
   *    previous firmware left behind.
   */
  if (bme_write(BME280_REG_RESET, BME280_RESET_WORD) != HAL_OK) return HAL_ERROR;
  HAL_Delay(10);

  /* Wait for the NVM copy to finish - status bit 0 (im_update) clears when the
   * calibration data becomes readable. Bounded, so a dead sensor cannot hang us.
   */
  for (tries = 0; tries < 50; tries++)
  {
    uint8_t status;
    if (bme_read(BME280_REG_STATUS, &status, 1) != HAL_OK) return HAL_ERROR;
    if ((status & 0x01) == 0) break;
    HAL_Delay(2);
  }

  /* 3. Read the per-chip trim constants. */
  if (bme_read_calibration() != HAL_OK) return HAL_ERROR;

  /* 4. Humidity oversampling. This must be written BEFORE ctrl_meas - the
   *    datasheet notes that changes to ctrl_hum only take effect after a
   *    write to ctrl_meas, which is an easy thing to get wrong.
   */
  if (s_has_hum)
  {
    if (bme_write(BME280_REG_CTRL_HUM, BME280_OSRS_X1) != HAL_OK) return HAL_ERROR;
  }

  /* 5. config: 125 ms between measurements, IIR filter off. */
  if (bme_write(BME280_REG_CONFIG,
                (uint8_t)((BME280_STANDBY_125MS << 5) | (BME280_FILTER_OFF << 2)))
      != HAL_OK) return HAL_ERROR;

  /* 6. ctrl_meas: x1 oversampling on temperature and pressure, normal mode.
   *    Raise the oversampling here for lower noise, at the cost of power and
   *    conversion time. This write also starts the sensor - it boots into
   *    sleep mode and measures nothing until told otherwise.
   */
  if (bme_write(BME280_REG_CTRL_MEAS,
                (uint8_t)((BME280_OSRS_X1 << 5) | (BME280_OSRS_X1 << 2) | BME280_MODE_NORMAL))
      != HAL_OK) return HAL_ERROR;

  HAL_Delay(50);   /* let the first conversion complete */
  return HAL_OK;
}

HAL_StatusTypeDef BME280_ReadAll(BME280_Data *out)
{
  uint8_t  buf[8];
  uint16_t len;
  int32_t  adc_P, adc_T;

  if (out == NULL || s_hi2c == NULL) return HAL_ERROR;

  len = s_has_hum ? 8u : 6u;

  /* One burst read from 0xF7 so every byte comes from the same measurement.
   * Three separate reads could straddle a sensor update and mix two of them.
   *   buf[0..2] press_msb / press_lsb / press_xlsb
   *   buf[3..5] temp_msb  / temp_lsb  / temp_xlsb
   *   buf[6..7] hum_msb   / hum_lsb            (BME280 only)
   */
  if (bme_read(BME280_REG_PRESS_MSB, buf, len) != HAL_OK) return HAL_ERROR;

  /* Pressure and temperature are 20-bit, packed across a byte, a byte, and the
   * top nibble of a third. Humidity is a plain 16-bit value.
   */
  adc_P = ((int32_t)buf[0] << 12) | ((int32_t)buf[1] << 4) | (buf[2] >> 4);
  adc_T = ((int32_t)buf[3] << 12) | ((int32_t)buf[4] << 4) | (buf[5] >> 4);

  /* Temperature first - it sets t_fine, which the other two depend on. */
  out->temperature = compensate_T(adc_T);
  out->pressure    = compensate_P(adc_P) / 256u;          /* Q24.8 -> Pa */

  if (s_has_hum)
  {
    int32_t adc_H = ((int32_t)buf[6] << 8) | buf[7];
    out->humidity = (compensate_H(adc_H) * 100u) / 1024u; /* Q22.10 -> 0.01 %RH */
  }
  else
  {
    out->humidity = 0;
  }

  return HAL_OK;
}

uint8_t BME280_GetChipID(void)   { return s_chip_id; }
uint8_t BME280_HasHumidity(void) { return s_has_hum; }
