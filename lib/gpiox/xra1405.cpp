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

#ifdef GPIOX_XRA1405

#include "pcf8575.h"

#include <hal/gpio_types.h>

#include "../../include/debug.h"

XRA1405 GPIOX;

XRA1405::XRA1405() :
		_DIN(0), _DIN_LAST(0), _DOUT(0), _DDR(0), _address(0)
{
}

void XRA1405::begin(gpio_num_t sda, gpio_num_t scl, uint8_t address, uint16_t speed) {

	/* Store the I2C address and init the Wire library */
	_address = address;
    myI2C.begin(sda, scl, (speed * 1000)); // 400Khz Default
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 800000); // 800Khz Overclocked
    //myI2C.begin(GPIO_NUM_21, GPIO_NUM_22, 1000000); // 1Mhz Overclocked
    //myI2C.setTimeout(10);
    //myI2C.scanner();
	myI2C.reset();

	readGPIOX();
}

void XRA1405::pinMode(uint8_t pin, pin_mode_t mode) {

	/* Switch according mode */
	if ( mode == GPIOX_MODE_INPUT )
	{
		_DDR |= (1 << pin);	
	}
	else if ( mode == GPIOX_MODE_OUTPUT ) {
		_DDR &= ~(1 << pin);
	} 

	/* Update GPIO values */
	updateGPIOX();
}

void XRA1405::portMode(port_t port, pin_mode_t mode) {

	if ( port == GPIOX_PORT0 )
	{
		// Modify P07-P00 (low byte)
		if ( mode == GPIOX_MODE_INPUT )
		{
			_DDR = (_DDR & 0xFF00) | 0x00FF;    // set low byte to 0xFF
		}
		else if ( mode == GPIOX_MODE_OUTPUT ) 
		{
			_DDR &= 0xFF00;                     // set low byte to 0x00
		}
	}
	else if ( port == GPIOX_PORT1 )
	{
		// Modify P17-P10 (high byte)
		if ( mode == GPIOX_MODE_INPUT )
		{
			_DDR = (_DDR & 0x00FF) | 0xFF00;    // set high byte to 0xFF
		}
		else if ( mode == GPIOX_MODE_OUTPUT ) 
		{
			_DDR &= 0x00FF;                     // set high byte to 0x00
		}
	}
	else if ( port == GPIOX_BOTH )
	{
		if ( mode == GPIOX_MODE_INPUT )
		{
			_DDR = 0xFFFF;  // set high/low byte to 0xFF
		}
		else if ( mode == GPIOX_MODE_OUTPUT ) 
		{
			_DDR = 0x0000;  // set high/low byte to 0x00
		}
	}

	//Debug_printv("port[%.2X] mode[%.2X] _DDR[%.2X] _DOUT[%.2X]", port, mode, _DDR, _DOUT);

	/* Update GPIO values */
	updateGPIOX();
}

void XRA1405::portMode(port_t port, uint16_t mode) {

	if ( port == GPIOX_PORT0 )
	{
		// Modify P07-P00 (low byte)
		_DDR = (_DDR & 0xFF00) | (mode & 0x00FF);    // set low byte to mode
	}
	else if ( port == GPIOX_PORT1 )
	{
		// Modify P17-P10 (high byte)
		_DDR = (_DDR & 0x00FF) | (mode & 0xFF00);    // set high byte to mode
	}
	else if ( port == GPIOX_BOTH )
	{
		_DDR = mode;  // set high/low byte to mode
	}

	//Debug_printv("port[%.2X] mode[%.2X] _DDR[%.2X] _DOUT[%.2X]", port, mode, _DDR, _DOUT);

	/* Update GPIO values */
	updateGPIOX();
}

void XRA1405::digitalWrite(uint8_t pin, uint8_t value) {

	/* Set PORT bit value */
	if (value)
		_DOUT |= (1 << pin);
	else
		_DOUT &= ~(1 << pin);

	writeGPIOX();
}

uint8_t XRA1405::digitalRead(uint8_t pin) {

	/* Read GPIO */
	readGPIOX();

	/* Read and return the pin state */
	return (_DIN & (1 << pin));
}


void XRA1405::write(port_t port, uint16_t value) {
	/* Store pins values and apply */
	if ( port == GPIOX_PORT0)
		// low byte swap
		_DOUT &= 0xFF00 | (value & 0x00FF);
	else if ( port == GPIOX_PORT1 )
		// hight byte swap
		_DOUT &= 0x00FF | (value << 8 & 0xFF00);
	else
		_DOUT = value;

	/* Update GPIOX values */
	writeGPIOX();
}

void XRA1405::write(uint16_t value) {
	_DOUT = value;

	/* Update GPIOX values */
	writeGPIOX();
}

uint16_t XRA1405::read(port_t port) {
	/* Read GPIOX */
	readGPIOX();

	/* Return current pins values */
	if ( port == GPIOX_PORT0)
		return PORT0;
	else if ( port == GPIOX_PORT1)
		return PORT1;
	else
		return _DOUT;
}

void XRA1405::clear(port_t port) {

	if ( port == GPIOX_BOTH )
		write(0x0000);
	else
		write(port, 0x00);
}

void XRA1405::set(port_t port) {

	if ( port == GPIOX_BOTH )
		write(0xFFFF);
	else
		write(port, 0xFF);
}

void XRA1405::toggle(uint8_t pin) {

	/* Toggle pin state */
	_DOUT ^= (1 << pin);

	writeGPIOX();
}


void XRA1405::readGPIOX() {

	_DIN_LAST = _DIN;

	uint8_t buffer[2];

	// Read two bytes
	myI2C.readBytes(_address, 2, buffer);
	_DIN = buffer[0];										// low byte
	_DIN = (_DIN & 0x00FF) | ( (buffer[1] << 8) & 0xFF00);  // high byte
	_DIN &= _DDR;

	this->PORT0 = buffer[0];
	this->PORT1 = buffer[1];
	//Debug_printv("address[%.2X] port0[%.2X] port1[%.2X] din[%.2X] din_last[%.2X] ddr[%.2X]", _address, PORT0, PORT1, _DIN, _DIN_LAST, _DDR);
}


void XRA1405::writeGPIOX() {

	uint8_t buffer[2];

	/* Compute new GPIO states */
	uint16_t value = _DOUT | _DDR;

	// Write two bytes
	buffer[0] = value & 0x00FF;  // low byte
	buffer[1] = value >> 8;      // high byte
	//Debug_printv("low[%.2X] high[%.2X]", buffer[0], buffer[1]);
	myI2C.writeBytes(_address, 2, buffer);
	//Debug_printv("address[%.2X] din[%.2X] dout[%.2X] ddr[%.2X] value[%.2X]", _address, _DIN, _DOUT, _DDR, value);
}

void XRA1405::updateGPIOX() {

	uint8_t buffer[2];

	// Set input bit mask
	buffer[0] = _DDR & 0x00FF;  // low byte
	buffer[1] = _DDR >> 8;      // high byte
	//Debug_printv("low[%.2X] high[%.2X]", buffer[0], buffer[1]);
	myI2C.writeBytes(_address, 2, buffer);
	//Debug_printv("address[%.2X] din[%.2X] dout[%.2X] ddr[%.2X]", _address, _DIN, _DOUT, _DDR);
}

#endif // GPIOX_XRA1405