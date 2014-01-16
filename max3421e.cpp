/*
Copyright 2011 Niels Brouwers

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.#include <string.h>
*/

/**
 *
 * Library for the max3421e USB host controller shield produced by circuitsathome and Sparkfun.
 * This is a low-level interface that provides access to the internal registers and polls the
 * controller for state changes.
 *
 * This library is based on work done by Oleg Masurov, but has been ported to C and heavily
 * restructured. Control over the GPIO pins has been stripped.
 *
 * Note that the current incarnation of this library only supports the Arduino Mega with a
 * hardware mod to rewire the MISO, MOSI, and CLK SPI pins.
 *
 * http://www.circuitsathome.com/
 */

#include <SPI.h>
#include <Arduino.h>
#include "max3421e.h"
#include "HardwareSerial.h"


static uint8_t vbusState;

/*
 * Initialises the max3421e host shield. Initialises the SPI bus and sets the required pin directions.
 * Must be called before powerOn.
 */
void max3421e_init()
{

	SPI.begin();

	pinMode(PIN_MAX_SS, OUTPUT);
/*
	pinMode(PIN_MAX_INT, INPUT);
	pinMode(PIN_MAX_GPX, INPUT);
	pinMode(PIN_MAX_RESET, OUTPUT);
*/


DDRE &= ~ 0x40;
DDRJ &= ~ 0x08;
DDRJ |= 0x04;



/*
#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)

	// Set MAX_INT and MAX_GPX pins to input mode.
	DDRH &= ~(0x40 | 0x20);

	// Set SPI !SS pint to output mode.
	DDRB |= 0x10;

	// Set RESET pin to output
	DDRH |= 0x10;

#elif defined(__AVR_ATmega168__) || defined(__AVR_ATmega328P__)

	// Set MAX_INT and MAX_GPX pins to input mode.
	DDRB &= ~0x3;

	// Set RESET pin to output
	DDRD |= 0x80;

	// Set SS pin to output
	DDRB |= 0x4;

#endif
*/

	// Pull SPI !SS high
	MAX_SS(1);

	// Reset
	MAX_RESET(1);
}

/**
 * Resets the max3412e. Sets the chip reset bit, SPI configuration is not affected.
 * @return true iff success.
 */
boolean max3421e_reset(void)
{
	uint8_t tmp = 0;

	// Chip reset. This stops the oscillator
	max3421e_write(MAX_REG_USBCTL, bmCHIPRES);

	// Remove the reset
	max3421e_write(MAX_REG_USBCTL, 0x00);

	delay(10);

	// Wait until the PLL is stable
	while (!(max3421e_read(MAX_REG_USBIRQ) & bmOSCOKIRQ))
	{
		// Timeout after 256 attempts.
		tmp++;
		if (tmp == 0)
			return (false);
	}

	// Success.
	return (true);
}

/**
 * Initialises the max3421e after power-on.
 */
void max3421e_powerOn(void)
{
	// Configure full-duplex SPI, interrupt pulse.
	max3421e_write(MAX_REG_PINCTL, (bmFDUPSPI + bmINTLEVEL + bmGPXB)); //Full-duplex SPI, level interrupt, GPX

	// Stop/start the oscillator.
	if (max3421e_reset() == false)
		Serial.print("Error: OSCOKIRQ failed to assert\n");

	// Configure host operation.
	max3421e_write(MAX_REG_MODE, bmDPPULLDN | bmDMPULLDN | bmHOST | bmSEPIRQ ); // set pull-downs, Host, Separate GPIN IRQ on GPX
	max3421e_write(MAX_REG_HIEN, bmCONDETIE | bmFRAMEIE ); //connection detection

	// Check if device is connected.
	max3421e_write(MAX_REG_HCTL, bmSAMPLEBUS ); // sample USB bus
	while (!(max3421e_read(MAX_REG_HCTL) & bmSAMPLEBUS)); //wait for sample operation to finish

	max3421e_busprobe(); //check if anything is connected
	max3421e_write(MAX_REG_HIRQ, bmCONDETIRQ ); //clear connection detect interrupt

	// Enable interrupt pin.
	max3421e_write(MAX_REG_CPUCTL, 0x01);
}

/**
 * Writes a single register.
 *
 * @param reg register address.
 * @param value value to write.
 */
void max3421e_write(uint8_t reg, uint8_t value)
{
	// Pull slave select low to indicate start of transfer.
	MAX_SS(0);

	// Transfer command byte, 0x02 indicates write.
	SPDR = (reg | 0x02);
	while (!(SPSR & (1 << SPIF)));

	// Transfer value byte.
	SPDR = value;
	while (!(SPSR & (1 << SPIF)));

	// Pull slave select high to indicate end of transfer.
	MAX_SS(1);

	return;
}

/**
 * Writes multiple bytes to a register.
 * @param reg register address.
 * @param count number of bytes to write.
 * @param vaues input values.
 * @return a pointer to values, incremented by the number of bytes written (values + length).
 */
uint8_t * max3421e_writeMultiple(uint8_t reg, uint8_t count, uint8_t * values)
{
	// Pull slave select low to indicate start of transfer.
	MAX_SS(0);

	// Transfer command byte, 0x02 indicates write.
	SPDR = (reg | 0x02);
	while (!(SPSR & (1 << SPIF)));

	// Transfer values.
	while (count--)
	{
		// Send next value byte.
		SPDR = (*values);
		while (!(SPSR & (1 << SPIF)));

		values++;
	}

	// Pull slave select high to indicate end of transfer.
	MAX_SS(1);

	return (values);
}

/**
 * Reads a single register.
 *
 * @param reg register address.
 * @return result value.
 */
uint8_t max3421e_read(uint8_t reg)
{
	// Pull slave-select high to initiate transfer.
	MAX_SS(0);

	// Send a command byte containing the register number.
	SPDR = reg;
	while (!(SPSR & (1 << SPIF)));

	// Send an empty byte while reading.
	SPDR = 0;
	while (!(SPSR & (1 << SPIF)));

	// Pull slave-select low to signal transfer complete.
	MAX_SS(1);

	// Return result byte.
	return (SPDR);
}

/**
 * Reads multiple bytes from a register.
 *
 * @param reg register to read from.
 * @param count number of bytes to read.
 * @param values target buffer.
 * @return pointer to the input buffer + count.
 */
uint8_t * max3421e_readMultiple(uint8_t reg, uint8_t count, uint8_t * values)
{
	// Pull slave-select high to initiate transfer.
	MAX_SS(0);

	// Send a command byte containing the register number.
	SPDR = reg;
	while (!(SPSR & (1 << SPIF))); //wait

	// Read [count] bytes.
	while (count--)
	{
		// Send empty byte while reading.
		SPDR = 0;
		while (!(SPSR & (1 << SPIF)));

		*values = SPDR;
		values++;
	}

	// Pull slave-select low to signal transfer complete.
	MAX_SS(1);

	// Return the byte array + count.
	return (values);
}

/**
 * @return the status of Vbus.
 */
uint8_t max3421e_getVbusState()
{
	return vbusState;
}

/**
 * Probes the bus to determine device presence and speed, and switches host to this speed.
 */
void max3421e_busprobe(void)
{
	uint8_t bus_sample;
	bus_sample = max3421e_read(MAX_REG_HRSL); //Get J,K status
	bus_sample &= (bmJSTATUS | bmKSTATUS); //zero the rest of the uint8_t

	switch (bus_sample)
	{
	//start full-speed or low-speed host
	case (bmJSTATUS):
		if ((max3421e_read(MAX_REG_MODE) & bmLOWSPEED) == 0)
		{
			max3421e_write(MAX_REG_MODE, MODE_FS_HOST ); //start full-speed host
			vbusState = FSHOST;
		} else
		{
			max3421e_write(MAX_REG_MODE, MODE_LS_HOST); //start low-speed host
			vbusState = LSHOST;
		}
		break;
	case (bmKSTATUS):
		if ((max3421e_read(MAX_REG_MODE) & bmLOWSPEED) == 0)
		{
			max3421e_write(MAX_REG_MODE, MODE_LS_HOST ); //start low-speed host
			vbusState = LSHOST;
		} else
		{
			max3421e_write(MAX_REG_MODE, MODE_FS_HOST ); //start full-speed host
			vbusState = FSHOST;
		}
		break;
	case (bmSE1): //illegal state
		vbusState = SE1;
		break;
	case (bmSE0): //disconnected state
		vbusState = SE0;
		break;
	}
}

/**
 * MAX3421 state change task and interrupt handler.
 * @return error code or 0 if successful.
 */
uint8_t max3421e_poll(void)
{
	uint8_t rcode = 0;

	// Check interrupt.
	if (MAX_INT() == 0)
		rcode = max3421e_interruptHandler();

	if (MAX_GPX() == 0)
		max3421e_gpxInterruptHandler();

	return (rcode);
}

/**
 * Interrupt handler.
 */
uint8_t max3421e_interruptHandler(void)
{
	uint8_t interruptStatus;
	uint8_t HIRQ_sendback = 0x00;

	// Determine interrupt source.
	interruptStatus = max3421e_read(MAX_REG_HIRQ);

	if (interruptStatus & bmFRAMEIRQ)
	{
		//->1ms SOF interrupt handler
		HIRQ_sendback |= bmFRAMEIRQ;
	}

	if (interruptStatus & bmCONDETIRQ)
	{
		max3421e_busprobe();

		HIRQ_sendback |= bmCONDETIRQ;
	}

	// End HIRQ interrupts handling, clear serviced IRQs
	max3421e_write(MAX_REG_HIRQ, HIRQ_sendback);

	return (HIRQ_sendback);
}

/**
 * GPX interrupt handler
 */
uint8_t max3421e_gpxInterruptHandler(void)
{
	//read GPIN IRQ register
	uint8_t interruptStatus = max3421e_read(MAX_REG_GPINIRQ);

	//    if( GPINIRQ & bmGPINIRQ7 ) {            //vbus overload
	//        vbusPwr( OFF );                     //attempt powercycle
	//        delay( 1000 );
	//        vbusPwr( ON );
	//        regWr( rGPINIRQ, bmGPINIRQ7 );
	//    }

	return (interruptStatus);
}

