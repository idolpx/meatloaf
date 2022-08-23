

#include "pcf8575.h"

#include <hal/gpio_types.h>

#include "../../include/debug.h"


PCF8575::PCF8575() :
		_PIN(0), _POUT(0), _DDR(0), _address(0)
{
}

void PCF8575::begin(uint8_t address, gpio_num_t sda, gpio_num_t scl) {

	/* Store the I2C address and init the Wire library */
	_address = address;
    myI2C.begin(sda, scl, 400000); // 400Khz Default
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 800000); // 800Khz Overclocked
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 1000000); // 1Mhz Overclocked
    //myI2C.setTimeout(10);
    //myI2C.scanner();

	readGPIO();
}

void PCF8575::pinMode(uint8_t pin, uint8_t mode) {

	/* Switch according mode */
	if ( mode == GPIO_MODE_INPUT )
	{
		_DDR |= (1 << pin);
		_POUT &= ~(1 << pin);		
	}
	else if ( mode == (GPIO_MODE_INPUT | GPIO_PULLUP_ENABLE) ) 
	{
		_DDR &= (1 << pin);
		_POUT |= ~(1 << pin);		
	}
	else if ( mode == GPIO_MODE_OUTPUT ) {
		_DDR &= ~(1 << pin);
		_POUT &= ~(1 << pin);
	} 

	/* Update GPIO values */
	updateGPIO();
}

void PCF8575::digitalWrite(uint8_t pin, uint8_t value) {

	/* Set PORT bit value */
	if (value)
		_POUT |= (1 << pin);
	else
		_POUT &= ~(1 << pin);

	/* Update GPIO values */
	updateGPIO();
}

uint8_t PCF8575::digitalRead(uint8_t pin) {

	/* Read GPIO */
	readGPIO();

	/* Read and return the pin state */
	return (_PIN & (1 << pin));
}

void PCF8575::write(uint16_t value) {

	/* Store pins values and apply */
	_POUT = value;

	/* Update GPIO values */
	updateGPIO();
}

uint16_t PCF8575::read() {

	/* Read GPIO */
	readGPIO();

	/* Return current pins values */
	return _PIN;
}


void PCF8575::clear() {

	/* User friendly wrapper for write() */
	write(0x0000);
}

void PCF8575::set() {

	/* User friendly wrapper for write() */
	write(0xFFFF);
}

void PCF8575::toggle(uint8_t pin) {

	/* Toggle pin state */
	_POUT ^= (1 << pin);

	/* Update GPIO values */
	updateGPIO();
}


void PCF8575::readGPIO() {

	uint8_t buffer[2];

	// Read two bytes
	myI2C.readBytes(_address, 0x00, 2, buffer);
	_PIN = buffer[0] << 8;  // low byte
	_PIN += buffer[1];        // high byte
	//Debug_printv("_address[%.2x] _pin[%d]", _address, _PIN);
}

void PCF8575::updateGPIO() {

	uint8_t buffer[2];

	/* Read current GPIO states */
	//readGPIO(); // Experimental

	/* Compute new GPIO states */
	//uint8_t value = ((_PIN & ~_DDR) & ~(~_DDR & _POUT)) | _POUT; // Experimental
	uint16_t value = (_PIN & ~_DDR) | _POUT;

	// Write two bytes
	buffer[0] = value & 0x00FF;  // low byte
	buffer[1] = value >> 8;      // high byte
	myI2C.writeBytes(_address, 0x00, 2, buffer);
	//Debug_printv("address[%.2x] in[%d] out[%d] ddr[%d] value[%d]", _address, _PIN, _POUT, _DDR, value);
}
