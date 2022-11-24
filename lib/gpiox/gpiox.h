//
// https://www.nxp.com/docs/en/data-sheet/PCF8575.pdf
// https://www.nxp.com/docs/en/data-sheet/PCA9673.pdf
//

#ifndef GPIOX_H
#define GPIOX_H

#include "../../include/pinmap.h"

#include <I2Cbus.hpp>

#define I2C_SDA      PIN_GPIOX_SDA
#define I2C_SCL      PIN_GPIOX_SCL
#define I2C_ADDRESS  GPIOX_ADDRESS
#define I2C_SPEED    GPIOX_SPEED

/* GPIOX port bits */
#define P00  0
#define P01  1
#define P02  2
#define P03  3
#define P04  4
#define P05  5
#define P06  6
#define P07  7
#define P10  8
#define P11  9
#define P12  10
#define P13  11
#define P14  12
#define P15  13
#define P16  14
#define P17  15

typedef enum {
	GPIOX_MODE_OUTPUT = 0,
	GPIOX_MODE_INPUT = 1
} pin_mode_t;


typedef enum {
	GPIOX_PORT0 = 0,
	GPIOX_PORT1 = 1,
	GPIOX_BOTH = 2
} port_t;

/**
 * @brief GPIOX
 */
class GPIOX {
public:

	/**
	 * Create a new GPIOX instance
	 */
	GPIOX();

	uint8_t PORT0; // PINS 00-07
	uint8_t PORT1; // PINS 10-17

	/**
	 * Start the I2C controller and store the PCF8575 chip address
	 */
	void begin(gpio_num_t sda = I2C_SDA, gpio_num_t scl = I2C_SCL, uint8_t address = I2C_ADDRESS, uint16_t speed = I2C_SPEED);

	/**
	 * Set the direction of a pin (OUTPUT, INPUT)
	 * 
	 * @param pin The pin to set
	 * @param mode The new mode of the pin
	 */
	void pinMode(uint8_t pin, pin_mode_t mode);

	/**
	 * Set the direction of all port pins (INPUT, OUTPUT)
	 * 
	 * @param port The port to set
	 * @param mode The new mode of the pins
	 */
	void portMode(port_t port, pin_mode_t mode);

	/**
	 * Set the state of a pin (HIGH or LOW)
	 * 
	 * @param pin The pin to set
	 * @param value The new state of the pin
	 * @remarks Software pull-up resistors are not available on the PCF8575
	 */
	void digitalWrite(uint8_t pin, uint8_t value);

	/**
	 * Read the state of a pin
	 * 
	 * @param pin The pin to read back
	 * @return
	 */
	uint8_t digitalRead(uint8_t pin);

	/**
	 * Set the state of all pins in one go
	 * 
	 * @param value The new value of all pins (1 bit = 1 pin, '1' = HIGH, '0' = LOW)
	 */
	void write(port_t port, uint16_t value);
	void write(uint16_t value);

	/**
	 * Read the state of all pins in one go
	 * 
	 * @return The current value of all pins (1 bit = 1 pin, '1' = HIGH, '0' = LOW)
	 */
	uint16_t read(port_t port = GPIOX_BOTH);

	/**
	 * Exactly like write(0x00), set all pins to LOW
	 */
	void clear(port_t port = GPIOX_BOTH);


	/**
	 * Exactly like write(0xFF), set all pins to HIGH
	 */
	void set(port_t port = GPIOX_BOTH);

	/**
	 * Toggle the state of a pin
	 */
	void toggle(uint8_t pin);

protected:

	I2C_t& myI2C = i2c0;  // i2c0 and i2c1 are the default objects

	/** Current input pins values */
	volatile uint16_t _DIN;
	volatile uint16_t _DIN_LAST;

	/** Output pins values */
	volatile uint16_t _DOUT;

	/** Pins modes values (OUTPUT or INPUT) */
	volatile uint16_t _DDR;

	/** GPIOX I2C address */
	uint8_t _address;

	/** 
	 * Read GPIO states and store them in _DIN variable
	 *
	 * @remarks Before reading current GPIO states, current _DIN variable value is moved to _DIN_LAST variable
	 */
	void readGPIOX();

	/** 
	 * Write value of _DOUT variable to the GPIOX
	 * 
	 * @remarks Only pin marked as OUTPUT are set, for INPUT pins their value are unchanged
	 * @warning To work properly (and avoid any states conflicts) readGPIO() MUST be called before call this function !
	 */
	void writeGPIOX();

	/** 
	 * Update INPUT/OUTPUT mode of all GPIOX pins
	 * 
	 * @remarks All pins modes are set to be equal to _DDR
	 */
	void updateGPIOX();
};

#endif // GPIOX_H
