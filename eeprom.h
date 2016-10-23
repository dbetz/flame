/**
 * Propeller overlay demo.
 * Copyright (c) 2016 by Marco Maccaferri.
 *
 * MIT Licensed.
 */

#ifndef EEPROM_H_
#define EEPROM_H_

/*
 * I2C bus ports.
 */
#define I2C_SCL         28
#define I2C_SDA         29

#define I2C_ACK         0
#define I2C_NAK         1
#define I2C_WRITE       0
#define I2C_READ        1

#define HIGH_EEPROM_OFFSET(a)  ((uint32_t)(a) - 0xc0000000 + 0x8000)

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Initializes EEPROM access.
 */
void eeprom_init(void);

/**
 * Writes a data block to the EEPROM.
 *
 * addr - EEPROM address
 * ptr - Pointer to the data block to write.
 * count - Number of bytes to write.
 */
int32_t eeprom_read(uint32_t addr, uint8_t * ptr, uint32_t count);

/**
 * Reads a data block from the EEPROM.
 *
 * addr - EEPROM address
 * ptr - Pointer to the data block to write to.
 * count - Number of bytes to read.
 */
int32_t eeprom_write(uint32_t addr, uint8_t * ptr, uint32_t count);

/**
 * Loads overlay code from EEPROM.
 *
 * n - Overlay number to load.
 */
void eeprom_load_overlay(int n);

void i2c_start();
void i2c_stop();
int32_t i2c_write(uint8_t data);
uint8_t i2c_read(int32_t ack);

#ifdef __cplusplus
}
#endif

#endif /* EEPROM_H_ */
