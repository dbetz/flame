/**
 * Propeller overlay demo.
 * Copyright (c) 2016 by Marco Maccaferri.
 *
 * MIT Licensed.
 */

#include <propeller.h>

#include "eeprom.h"

#define EEPROM_ADDR 0xA0

void eeprom_init(void)
{
    int i;

    OUTA = (1 << I2C_SCL);
    DIRA = (1 << I2C_SCL);

    DIRA &= ~(1 << I2C_SDA);                       // Set SDA as input
    for (i = 0; i < 9; i++) {
        OUTA &= ~(1 << I2C_SCL);                   // Put out up to 9 clock pulses
        OUTA |= (1 << I2C_SCL);
        if ((INA & (1 << I2C_SDA)) != 0)           // Repeat if SDA not driven high by the EEPROM
            break;
    }
}

int32_t eeprom_read(uint32_t addr, uint8_t * ptr, uint32_t count) {

    while (count > 0) {
        i2c_start();                                    // Select the device & send address
        i2c_write(EEPROM_ADDR | I2C_WRITE | ((addr & 0x10000) >> 15));
        i2c_write(addr >> 8);
        i2c_write(addr & 0xFF);

        i2c_start();                                    // Reselect the device for reading
        i2c_write(EEPROM_ADDR | I2C_READ | ((addr & 0x10000) >> 15));

        addr++;
        while (count > 1 && (addr & 0xFFFF) != 0) {
            *ptr++ = i2c_read(I2C_ACK);
            count--;
            addr++;
        }
        *ptr++ = i2c_read(I2C_NAK);
        count--;

        i2c_stop();
    }

    return 0;
}

int32_t eeprom_write(uint32_t addr, uint8_t * ptr, uint32_t count) {

    while (count > 0) {
        i2c_start();                                    // Select the device & send address
        i2c_write(EEPROM_ADDR | I2C_WRITE | ((addr & 0x10000) >> 15));
        i2c_write(addr >> 8);
        i2c_write(addr & 0xFF);

        do {
            i2c_write(*ptr++);
            addr++;
            count--;
        } while (count > 0 && (addr & 0x3F) != 0);

        i2c_stop();
        waitcnt(CNT + ((_CLKFREQ / 1000) * 5));
    }

    return 0;
}

// SDA goes HIGH to LOW with SCL HIGH
void i2c_start() {
    OUTA |= (1 << I2C_SCL);                        // Initially drive SCL HIGH
    DIRA |= (1 << I2C_SCL);
    OUTA |= (1 << I2C_SDA);                        // Initially drive SDA HIGH
    DIRA |= (1 << I2C_SDA);
    OUTA &= ~(1 << I2C_SDA);                       // Now drive SDA LOW
    OUTA &= ~(1 << I2C_SCL);                       // Leave SCL LOW
}

// SDA goes LOW to HIGH with SCL High
void i2c_stop() {
    OUTA |= (1 << I2C_SCL);                        // Drive SCL HIGH
    OUTA |= (1 << I2C_SDA);                        // then SDA HIGH
    DIRA &= ~(1 << I2C_SCL);                       // Now let them float
    DIRA &= ~(1 << I2C_SDA);                       // If pullups present, they'll stay HIGH
}

// Write i2c data.  Data byte is output MSB first, SDA data line is valid
// only while the SCL line is HIGH.  Data is always 8 bits (+ ACK/NAK).
// SDA is assumed LOW and SCL and SDA are both left in the LOW state.
int32_t i2c_write(uint8_t data) {
    int32_t i, ack = 0;

    for (i = 0x80; i != 0x00; i >>= 1) {    // Output data to SDA
        if ((data & i) != 0)
            OUTA |= (1 << I2C_SDA);
        else
            OUTA &= ~(1 << I2C_SDA);
        OUTA |= (1 << I2C_SCL);                        // Toggle SCL from LOW to HIGH to LOW
        __asm ("nop\r\n");
        __asm ("nop\r\n");
        OUTA &= ~(1 << I2C_SCL);
    }

    DIRA &= ~(1 << I2C_SDA);                           // Set SDA to input for ACK/NAK
    OUTA |= (1 << I2C_SCL);
    ack = INA & (1 << I2C_SDA);                        // Sample SDA when SCL is HIGH
    OUTA &= ~(1 << I2C_SCL);

    OUTA &= ~(1 << I2C_SDA);                           // Leave SDA driven LOW
    DIRA |= (1 << I2C_SDA);

    return ack == 0 ? I2C_ACK : I2C_NAK;
}

// Read in i2c data, Data byte is output MSB first, SDA data line is
// valid only while the SCL line is HIGH.  SCL and SDA left in LOW state.
uint8_t i2c_read(int32_t ack) {
    int32_t i, data = 0;

    DIRA &= ~(1 << I2C_SDA);                       // Make SDA an input
    for (i = 0; i < 8; i++) {           // Receive data from SDA
        OUTA |= (1 << I2C_SCL);                    // Sample SDA when SCL is HIGH
        data <<= 1;
        data |= (INA & (1 << I2C_SDA)) >> I2C_SDA;
        OUTA &= ~(1 << I2C_SCL);
        __asm ("nop\r\n");
        __asm ("nop\r\n");
    }

    if (ack)                            // Output ACK/NAK to SDA
        OUTA |= (1 << I2C_SDA);
    else
        OUTA &= ~(1 << I2C_SDA);
    DIRA |= (1 << I2C_SDA);
    OUTA |= (1 << I2C_SCL);                        // Toggle SCL from LOW to HIGH to LOW
    OUTA &= ~(1 << I2C_SCL);

    OUTA &= ~(1 << I2C_SDA);                       // Leave SDA driven LOW

    return data;
}
