// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

#ifdef GPIOX_MCP23017

#include "mcp23017.h"

#include <hal/gpio_types.h>

#include "../../include/debug.h"

MCP23017 GPIOX;

MCP23017::MCP23017() :
		_PIN(0), _PORT(0), _DDR(0), _address(0)
{
}

void MCP23017::begin(uint8_t address, gpio_num_t sda, gpio_num_t scl) {

	/* Store the I2C address and init the Wire library */
	_address = address;
    myI2C.begin(sda, scl, 400000); // 400Khz Default
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 800000); // 800Khz Overclocked
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 1000000); // 1Mhz Overclocked
    //myI2C.setTimeout(10);
    //myI2C.scanner();

	readGPIO();
}

void MCP23017::pinMode(uint8_t pin, uint8_t mode) {

	/* Switch according mode */
	if ( mode == GPIO_MODE_INPUT )
	{
		_DDR |= (1 << pin);
		_PORT &= ~(1 << pin);		
	}
	else if ( mode == (GPIO_MODE_INPUT | GPIO_PULLUP_ENABLE) ) 
	{
		_DDR &= (1 << pin);
		_PORT |= ~(1 << pin);		
	}
	else if ( mode == GPIO_MODE_OUTPUT ) {
		_DDR &= ~(1 << pin);
		_PORT &= ~(1 << pin);
	} 

	/* Update GPIO values */
	updateGPIO();
}

void MCP23017::digitalWrite(uint8_t pin, uint8_t value) {

	/* Set PORT bit value */
	if (value)
		_PORT |= (1 << pin);
	else
		_PORT &= ~(1 << pin);

	/* Update GPIO values */
	updateGPIO();
}

uint8_t MCP23017::digitalRead(uint8_t pin) {

	/* Read GPIO */
	readGPIO();

	/* Read and return the pin state */
	return (_PIN & (1 << pin));
}

void MCP23017::write(uint16_t value) {

	/* Store pins values and apply */
	_PORT = value;

	/* Update GPIO values */
	updateGPIO();
}

uint16_t MCP23017::read() {

	/* Read GPIO */
	readGPIO();

	/* Return current pins values */
	return _PIN;
}


void MCP23017::clear() {

	/* User friendly wrapper for write() */
	write(0x0000);
}

void MCP23017::set() {

	/* User friendly wrapper for write() */
	write(0xFFFF);
}

void MCP23017::toggle(uint8_t pin) {

	/* Toggle pin state */
	_PORT ^= (1 << pin);

	/* Update GPIO values */
	updateGPIO();
}


void MCP23017::readGPIO() {

	_oldPIN = _PIN;
	
	uint8_t buffer[2];

	// Read two bytes
	myI2C.readBytes(_address, 0x00, 2, buffer);
	_PIN = buffer[0] << 8;  // low byte
	_PIN += buffer[1];        // high byte
	//Debug_printv("_address[%.2x] _pin[%d]", _address, _PIN);
}

void MCP23017::updateGPIO() {

	uint8_t buffer[2];

	/* Read current GPIO states */
	//readGPIO(); // Experimental

	/* Compute new GPIO states */
	//uint8_t value = ((_PIN & ~_DDR) & ~(~_DDR & _PORT)) | _PORT; // Experimental
	uint16_t value = (_PIN & ~_DDR) | _PORT;

	// Write two bytes
	buffer[0] = value & 0x00FF;  // low byte
	buffer[1] = value >> 8;      // high byte
	myI2C.writeBytes(_address, 0x00, 2, buffer);
	//Debug_printv("address[%.2x] in[%d] out[%d] ddr[%d] value[%d]", _address, _PIN, _PORT, _DDR, value);
}

#endif // GPIOX_MCP23017