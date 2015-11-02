/*
 * wiringPi:
 *	Arduino look-a-like Wiring library for the Raspberry Pi
 *	Copyright (c) 2012-2015 Gordon Henderson
 *	Additional code for pwmSetClock by Chris Hall <chris@kchall.plus.com>
 *
 *	Thanks to code samples from Gert Jan van Loo and the
 *	BCM2835 ARM Peripherals manual, however it's missing
 *	the clock section /grr/mutter/
 ***********************************************************************
 * This file is part of wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as
 *    published by the Free Software Foundation, either version 3 of the
 *    License, or (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with wiringPi.
 *    If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */

// Revisions:
//	19 Jul 2012:
//		Moved to the LGPL
//		Added an abstraction layer to the main routines to save a tiny
//		bit of run-time and make the clode a little cleaner (if a little
//		larger)
//		Added waitForInterrupt code
//		Added piHiPri code
//
//	 9 Jul 2012:
//		Added in support to use the /sys/class/gpio interface.
//	 2 Jul 2012:
//		Fixed a few more bugs to do with range-checking when in GPIO mode.
//	11 Jun 2012:
//		Fixed some typos.
//		Added c++ support for the .h file
//		Added a new function to allow for using my "pin" numbers, or native
//			GPIO pin numbers.
//		Removed my busy-loop delay and replaced it with a call to delayMicroseconds
//
//	02 May 2012:
//		Added in the 2 UART pins
//		Change maxPins to numPins to more accurately reflect purpose


#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#include "softPwm.h"
#include "softTone.h"

#include "wiringPi.h"

#ifndef	TRUE
#define	TRUE	(1==1)
#define	FALSE	(1==2)
#endif

// Environment Variables

#define	ENV_DEBUG	"WIRINGPI_DEBUG"
#define	ENV_CODES	"WIRINGPI_CODES"


// Mask for the bottom 64 pins which belong to the Raspberry Pi
//	The others are available for the other devices

#define	PI_GPIO_MASK	(0xFFFFFFC0)

struct wiringPiNodeStruct *wiringPiNodes = NULL ;

// BCM Magic

#define	BCM_PASSWORD		0x5A000000


// The BCM2835 has 54 GPIO pins.
//	BCM2835 data sheet, Page 90 onwards.
//	There are 6 control registers, each control the functions of a block
//	of 10 pins.
//	Each control register has 10 sets of 3 bits per GPIO pin - the ALT values
//
//	000 = GPIO Pin X is an input
//	001 = GPIO Pin X is an output
//	100 = GPIO Pin X takes alternate function 0
//	101 = GPIO Pin X takes alternate function 1
//	110 = GPIO Pin X takes alternate function 2
//	111 = GPIO Pin X takes alternate function 3
//	011 = GPIO Pin X takes alternate function 4
//	010 = GPIO Pin X takes alternate function 5
//
// So the 3 bits for port X are:
//	X / 10 + ((X % 10) * 3)

// Port function select bits

#define	FSEL_INPT		0b000
#define	FSEL_OUTP		0b001
#define	FSEL_ALT0		0b100
#define	FSEL_ALT1		0b101
#define	FSEL_ALT2		0b110
#define	FSEL_ALT3		0b111
#define	FSEL_ALT4		0b011
#define	FSEL_ALT5		0b010

// Access from ARM Running Linux
//	Taken from Gert/Doms code. Some of this is not in the manual
//	that I can find )-:

#define BCM2708_PERI_BASE	                     0x20000000
#define GPIO_PADS		(BCM2708_PERI_BASE + 0x00100000)
#define CLOCK_BASE		(BCM2708_PERI_BASE + 0x00101000)
#define GPIO_BASE		(BCM2708_PERI_BASE + 0x00200000)
#define GPIO_TIMER		(BCM2708_PERI_BASE + 0x0000B000)
#define GPIO_PWM		(BCM2708_PERI_BASE + 0x0020C000)

#define	PAGE_SIZE		(4*1024)
#define	BLOCK_SIZE		(4*1024)

// PWM
//	Word offsets into the PWM control region

#define	PWM_CONTROL 0
#define	PWM_STATUS  1
#define	PWM0_RANGE  4
#define	PWM0_DATA   5
#define	PWM1_RANGE  8
#define	PWM1_DATA   9

//	Clock regsiter offsets

#define	PWMCLK_CNTL	40
#define	PWMCLK_DIV	41

#define	PWM0_MS_MODE    0x0080  // Run in MS mode
#define	PWM0_USEFIFO    0x0020  // Data from FIFO
#define	PWM0_REVPOLAR   0x0010  // Reverse polarity
#define	PWM0_OFFSTATE   0x0008  // Ouput Off state
#define	PWM0_REPEATFF   0x0004  // Repeat last value if FIFO empty
#define	PWM0_SERIAL     0x0002  // Run in serial mode
#define	PWM0_ENABLE     0x0001  // Channel Enable

#define	PWM1_MS_MODE    0x8000  // Run in MS mode
#define	PWM1_USEFIFO    0x2000  // Data from FIFO
#define	PWM1_REVPOLAR   0x1000  // Reverse polarity
#define	PWM1_OFFSTATE   0x0800  // Ouput Off state
#define	PWM1_REPEATFF   0x0400  // Repeat last value if FIFO empty
#define	PWM1_SERIAL     0x0200  // Run in serial mode
#define	PWM1_ENABLE     0x0100  // Channel Enable

// Timer
//	Word offsets

#define	TIMER_LOAD	(0x400 >> 2)
#define	TIMER_VALUE	(0x404 >> 2)
#define	TIMER_CONTROL	(0x408 >> 2)
#define	TIMER_IRQ_CLR	(0x40C >> 2)
#define	TIMER_IRQ_RAW	(0x410 >> 2)
#define	TIMER_IRQ_MASK	(0x414 >> 2)
#define	TIMER_RELOAD	(0x418 >> 2)
#define	TIMER_PRE_DIV	(0x41C >> 2)
#define	TIMER_COUNTER	(0x420 >> 2)

// Locals to hold pointers to the hardware

//for M2 PL and PM group gpios
static volatile uint32_t *gpio_lm;

static volatile uint32_t *gpio ;
static volatile uint32_t *pwm ;
static volatile uint32_t *clk ;
static volatile uint32_t *pads ;

#ifdef	USE_TIMER
static volatile uint32_t *timer ;
static volatile uint32_t *timerIrqRaw ;
#endif


// Data for use with the boardId functions.
//	The order of entries here to correspond with the PI_MODEL_X
//	and PI_VERSION_X defines in wiringPi.h
//	Only intended for the gpio command - use at your own risk!

static int piModel2 = FALSE ;

const char *piModelNames [8] =
{
  "Unknown",
  "Model A",
  "Model B",
  "Model B+",
  "Compute Module",
  "Model A+",
  "Model 2",	// Quad Core
  "Model BM",
} ;

const char *piRevisionNames [5] =
{
  "Unknown",
  "1",
  "1.1",
  "1.2",
  "2",
} ;

const char *piMakerNames [6] =
{
  "Unknown",
  "Egoman",
  "Sony",
  "Qusda",
  "MBest",
  "BPI",
} ;


// Time for easy calculations

static uint64_t epochMilli, epochMicro ;

// Misc

static int wiringPiMode = WPI_MODE_UNINITIALISED ;
static volatile int    pinPass = -1 ;
static pthread_mutex_t pinMutex ;

// Debugging & Return codes

int wiringPiDebug       = FALSE ;
int wiringPiReturnCodes = FALSE ;

// sysFds:
//	Map a file descriptor from the /sys/class/gpio/gpioX/value

static int sysFds [64] =
{
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
} ;

// ISR Data

static void (*isrFunctions [64])(void) ;


// Doing it the Arduino way with lookup tables...
//	Yes, it's probably more innefficient than all the bit-twidling, but it
//	does tend to make it all a bit clearer. At least to me!

// pinToGpio:
//	Take a Wiring pin (0 through X) and re-map it to the BCM_GPIO pin
//	Cope for 3 different board revisions here.

static int *pinToGpio ;

// Revision 1, 1.1:

static int pinToGpioR1 [64] =
{
  17, 18, 21, 22, 23, 24, 25, 4,	// From the Original Wiki - GPIO 0 through 7:	wpi  0 -  7
   0,  1,				// I2C  - SDA1, SCL1				wpi  8 -  9
   8,  7,				// SPI  - CE1, CE0				wpi 10 - 11
  10,  9, 11, 				// SPI  - MOSI, MISO, SCLK			wpi 12 - 14
  14, 15,				// UART - Tx, Rx				wpi 15 - 16

// Padding:

      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 31
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
} ;

// Revision 2:

static int pinToGpioR2 [64] =
{
  17, 18, 27, 22, 23, 24, 25, 4,	// From the Original Wiki - GPIO 0 through 7:	wpi  0 -  7
   2,  3,				// I2C  - SDA0, SCL0				wpi  8 -  9
   8,  7,				// SPI  - CE1, CE0				wpi 10 - 11
  10,  9, 11, 				// SPI  - MOSI, MISO, SCLK			wpi 12 - 14
  14, 15,				// UART - Tx, Rx				wpi 15 - 16
  28, 29, 30, 31,			// Rev 2: New GPIOs 8 though 11			wpi 17 - 20
   5,  6, 13, 19, 26,			// B+						wpi 21, 22, 23, 24, 25
  12, 16, 20, 21,			// B+						wpi 26, 27, 28, 29
   0,  1,				// B+						wpi 30, 31

// Padding:

  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
} ;


// physToGpio:
//	Take a physical pin (1 through 26) and re-map it to the BCM_GPIO pin
//	Cope for 2 different board revisions here.
//	Also add in the P5 connector, so the P5 pins are 3,4,5,6, so 53,54,55,56

static int *physToGpio ;

static int physToGpioR1 [64] =
{
  -1,		// 0
  -1, -1,	// 1, 2
   0, -1,
   1, -1,
   4, 14,
  -1, 15,
  17, 18,
  21, -1,
  22, 23,
  -1, 24,
  10, -1,
   9, 25,
  11,  8,
  -1,  7,	// 25, 26

                                              -1, -1, -1, -1, -1,	// ... 31
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
} ;

static int physToGpioR2 [64] =
{
  -1,		// 0
  -1, -1,	// 1, 2
   2, -1,
   3, -1,
   4, 14,
  -1, 15,
  17, 18,
  27, -1,
  22, 23,
  -1, 24,
  10, -1,
   9, 25,
  11,  8,
  -1,  7,	// 25, 26

// B+

   0,  1,
   5, -1,
   6, 12,
  13, -1,
  19, 16,
  26, 20,
  -1, 21,

// the P5 connector on the Rev 2 boards:

  -1, -1,
  -1, -1,
  -1, -1,
  -1, -1,
  -1, -1,
  28, 29,
  30, 31,
  -1, -1,
  -1, -1,
  -1, -1,
  -1, -1,
} ;

// gpioToGPFSEL:
//	Map a BCM_GPIO pin to it's Function Selection
//	control port. (GPFSEL 0-5)
//	Groups of 10 - 3 bits per Function - 30 bits per port

static uint8_t gpioToGPFSEL [] =
{
  0,0,0,0,0,0,0,0,0,0,
  1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,
  4,4,4,4,4,4,4,4,4,4,
  5,5,5,5,5,5,5,5,5,5,
} ;


// gpioToShift
//	Define the shift up for the 3 bits per pin in each GPFSEL port

static uint8_t gpioToShift [] =
{
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
  0,3,6,9,12,15,18,21,24,27,
} ;


// gpioToGPSET:
//	(Word) offset to the GPIO Set registers for each GPIO pin

static uint8_t gpioToGPSET [] =
{
   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
   8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
} ;

// gpioToGPCLR:
//	(Word) offset to the GPIO Clear registers for each GPIO pin

static uint8_t gpioToGPCLR [] =
{
  10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
  11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
} ;


// gpioToGPLEV:
//	(Word) offset to the GPIO Input level registers for each GPIO pin

static uint8_t gpioToGPLEV [] =
{
  13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
  14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
} ;


#ifdef notYetReady
// gpioToEDS
//	(Word) offset to the Event Detect Status

static uint8_t gpioToEDS [] =
{
  16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
  17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
} ;

// gpioToREN
//	(Word) offset to the Rising edge ENable register

static uint8_t gpioToREN [] =
{
  19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
  20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
} ;

// gpioToFEN
//	(Word) offset to the Falling edgde ENable register

static uint8_t gpioToFEN [] =
{
  22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
  23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
} ;
#endif


// GPPUD:
//	GPIO Pin pull up/down register

#define	GPPUD	37

// gpioToPUDCLK
//	(Word) offset to the Pull Up Down Clock regsiter

static uint8_t gpioToPUDCLK [] =
{
  38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,38,
  39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,39,
} ;


// gpioToPwmALT
//	the ALT value to put a GPIO pin into PWM mode

static uint8_t gpioToPwmALT [] =
{
          0,         0,         0,         0,         0,         0,         0,         0,	//  0 ->  7
          0,         0,         0,         0, FSEL_ALT0, FSEL_ALT0,         0,         0, 	//  8 -> 15
          0,         0, FSEL_ALT5, FSEL_ALT5,         0,         0,         0,         0, 	// 16 -> 23
          0,         0,         0,         0,         0,         0,         0,         0,	// 24 -> 31
          0,         0,         0,         0,         0,         0,         0,         0,	// 32 -> 39
  FSEL_ALT0, FSEL_ALT0,         0,         0,         0, FSEL_ALT0,         0,         0,	// 40 -> 47
          0,         0,         0,         0,         0,         0,         0,         0,	// 48 -> 55
          0,         0,         0,         0,         0,         0,         0,         0,	// 56 -> 63
} ;


// gpioToPwmPort
//	The port value to put a GPIO pin into PWM mode

static uint8_t gpioToPwmPort [] =
{
          0,         0,         0,         0,         0,         0,         0,         0,	//  0 ->  7
          0,         0,         0,         0, PWM0_DATA, PWM1_DATA,         0,         0, 	//  8 -> 15
          0,         0, PWM0_DATA, PWM1_DATA,         0,         0,         0,         0, 	// 16 -> 23
          0,         0,         0,         0,         0,         0,         0,         0,	// 24 -> 31
          0,         0,         0,         0,         0,         0,         0,         0,	// 32 -> 39
  PWM0_DATA, PWM1_DATA,         0,         0,         0, PWM1_DATA,         0,         0,	// 40 -> 47
          0,         0,         0,         0,         0,         0,         0,         0,	// 48 -> 55
          0,         0,         0,         0,         0,         0,         0,         0,	// 56 -> 63

} ;

// gpioToGpClkALT:
//	ALT value to put a GPIO pin into GP Clock mode.
//	On the Pi we can really only use BCM_GPIO_4 and BCM_GPIO_21
//	for clocks 0 and 1 respectively, however I'll include the full
//	list for completeness - maybe one day...

#define	GPIO_CLOCK_SOURCE	1

// gpioToGpClkALT0:

static uint8_t gpioToGpClkALT0 [] =
{
          0,         0,         0,         0, FSEL_ALT0, FSEL_ALT0, FSEL_ALT0,         0,	//  0 ->  7
          0,         0,         0,         0,         0,         0,         0,         0, 	//  8 -> 15
          0,         0,         0,         0, FSEL_ALT5, FSEL_ALT5,         0,         0, 	// 16 -> 23
          0,         0,         0,         0,         0,         0,         0,         0,	// 24 -> 31
  FSEL_ALT0,         0, FSEL_ALT0,         0,         0,         0,         0,         0,	// 32 -> 39
          0,         0, FSEL_ALT0, FSEL_ALT0, FSEL_ALT0,         0,         0,         0,	// 40 -> 47
          0,         0,         0,         0,         0,         0,         0,         0,	// 48 -> 55
          0,         0,         0,         0,         0,         0,         0,         0,	// 56 -> 63
} ;

// gpioToClk:
//	(word) Offsets to the clock Control and Divisor register

static uint8_t gpioToClkCon [] =
{
         -1,        -1,        -1,        -1,        28,        30,        32,        -1,	//  0 ->  7
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1, 	//  8 -> 15
         -1,        -1,        -1,        -1,        28,        30,        -1,        -1, 	// 16 -> 23
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1,	// 24 -> 31
         28,        -1,        28,        -1,        -1,        -1,        -1,        -1,	// 32 -> 39
         -1,        -1,        28,        30,        28,        -1,        -1,        -1,	// 40 -> 47
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1,	// 48 -> 55
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1,	// 56 -> 63
} ;

static uint8_t gpioToClkDiv [] =
{
         -1,        -1,        -1,        -1,        29,        31,        33,        -1,	//  0 ->  7
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1, 	//  8 -> 15
         -1,        -1,        -1,        -1,        29,        31,        -1,        -1, 	// 16 -> 23
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1,	// 24 -> 31
         29,        -1,        29,        -1,        -1,        -1,        -1,        -1,	// 32 -> 39
         -1,        -1,        29,        31,        29,        -1,        -1,        -1,	// 40 -> 47
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1,	// 48 -> 55
         -1,        -1,        -1,        -1,        -1,        -1,        -1,        -1,	// 56 -> 63
} ;

// Add for Banana Pi
/* for mmap bananapi */
#define	MAX_PIN_NUM		(0x40)  //64
//sunxi_gpio
#define SUNXI_GPIO_BASE (0x01C20800)
#define SUNXI_GPIO_LM_BASE    (0x01f02c00)
#define MAP_SIZE	(4096*2)
#define MAP_MASK	(MAP_SIZE - 1)
//sunxi_pwm, only use ch1 and ch2
#define SUNXI_PWM_BASE        (0x01c21400)
//#define SUNXI_PWM_CH0_CTRL    (SUNXI_PWM_BASE)
//#define SUNXI_PWM_CH0_PERIOD  (SUNXI_PWM_BASE + 0x04)
#define SUNXI_PWM_CH1_CTRL    (SUNXI_PWM_BASE + 0X10)
#define SUNXI_PWM_CH1_PERIOD  (SUNXI_PWM_BASE + 0x14)
#define SUNXI_PWM_CH2_CTRL    (SUNXI_PWM_BASE + 0X20)
#define SUNXI_PWM_CH2_PERIOD  (SUNXI_PWM_BASE + 0x24)
//#define SUNXI_PWM_CH3_CTRL    (SUNXI_PWM_BASE + 0X30)
//#define SUNXI_PWM_CH3_PERIOD  (SUNXI_PWM_BASE + 0x34)

//each channel use the same offset bit
#define SUNXI_PWM_CH_EN			    (1 << 4)
#define SUNXI_PWM_CH_ACT_STA		(1 << 5)
#define SUNXI_PWM_SCLK_CH_GATING	(1 << 6)
#define SUNXI_PWM_CH_MS_MODE		(1 << 7) //pulse mode
#define SUNXI_PWM_CH_PUL_START		(1 << 8)


#define PWM_CLK_DIV_120 	        0
#define PWM_CLK_DIV_180		1
#define PWM_CLK_DIV_240		2
#define PWM_CLK_DIV_360		3
#define PWM_CLK_DIV_480		4
#define PWM_CLK_DIV_12K		8
#define PWM_CLK_DIV_24K		9
#define PWM_CLK_DIV_36K		10
#define PWM_CLK_DIV_48K		11
#define PWM_CLK_DIV_72K		12

#define GPIO_PADS_BP		(0x00100000)
#define CLOCK_BASE_BP		(0x00101000)
//	addr should 4K*n
//	#define GPIO_BASE_BP		(SUNXI_GPIO_BASE)
#define GPIO_BASE_BP		(0x01C20000)
#define GPIO_TIMER_BP		(0x0000B000)
#define GPIO_BASE_LM_BP		(0x01f02000)  
#define GPIO_PWM_BP		(0x01c20000)  //need 4k*n

static int wiringPinMode = WPI_MODE_UNINITIALISED ;

/*
	map tableb for BP
*/
static int pinToGpio_BP [64] =
{
  199, 234,        //0, 1
  198, 201,        //2, 3
  235, 236,        //4, 5
  200, 233,        //6, 7
  243, 242,        //8, 9
  205, 204,        //10, 11
  207, 208,        //12, 13
  206, 132,        //14, 15
  133,  -1,        //16, 17
   -1,  -1,        //18, 19
   -1,  32,        //20, 21
   33,  34,        //22, 23
   35,  36,        //24, 25
   39, 134,        //26, 27
  135, 290,        //28. 29
   38,  37,        //30, 31
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 63
} ;

//map bcm gpio_num(index) to bp gpio_num(element)
static int pinTobcm_BP [64] =
{
   38,  37,        //0, 1
  243, 242,        //2, 3
  233,  32,        //4, 5
   33, 204,        //6, 7
  205, 208,        //8, 9
  207, 206,        //10, 11
   39,  34,        //12, 13
  132, 133,        //14, 15
  134, 199,        //16, 17
  234,  35,		   //18, 19
  135, 290,        //20, 21
  201, 235,        //22, 23
  236, 200,        //24, 25
   36, 198,        //26, 27
  -1,-1,
  -1,-1,//31
  -1, -1,-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // ... 63
} ;

//map phys_num(index) to bp gpio_num(element)
static int physToGpio_BP [64] =
{
   -1,          // 0
   -1,  -1,     //1, 2
  243,  -1,     //3, 4
  242,  -1,     //5, 6
  233, 132,     //7, 8
   -1, 133,     //9, 10
  199, 234,     //11, 12
  198,  -1,     //13, 14
  201, 235,     //15, 16
   -1, 236,     //17, 18
  207,  -1,     //19, 20
  208, 200,     //21, 22
  206, 205,     //23, 24
   -1, 204,     //25, 26
   38,  37,     //27, 28
   32,  -1,     //29, 30
   33,  39,     //31, 32      
   34,  -1,     //33, 34
   35, 134,     //35, 36
   36, 135,     //37, 38
   -1, 290,     //39, 40
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, //41-> 55
   -1, -1, -1, -1, -1, -1, -1, -1 // 56-> 63
} ;

static int syspin [64] =
{
  -1, -1, 2, 3, 4, 5, 6, 7,   //GPIO0,1 used to I2C
  8, 9, 10, 11, 12,13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23,
  24, 25, 26, 27, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
} ;

static int edge [64] =
{
  -1, -1, -1, -1, 4, -1, -1, 7, 
  8, 9, 10, 11, -1,-1, 14, 15,
  -1, 17, -1, -1, -1, -1, 22, 23,
  24, 25, -1, 27, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
} ;
static int physToGpioR3 [64] =//head 2 arm pin and p5
{
  -1,		// 0
  -1, -1,	// 1, 2
   2, -1,
   3, -1,
   4, 14,
  -1, 15,
  17, 18,
  27, -1,
  22, 23,
  -1, 24,
  10, -1,
   9, 25,
  11,  8,
  -1,  7,	// 25, 26

// Padding:
                                              -1, -1, 28, 29, 30,	// ... 31
  31, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 47
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,	// ... 63
} ;
static int BP_PIN_MASK[9][32] =  //[BANK]  [INDEX]
{
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PA
  {0,1,2,3,4,5,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PB
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PC
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PD
  {-1,-1,-1,-1, 4, 5, 6, 7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PE
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PF
  {-1,-1,-1,-1,-1,-1, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PG
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,9,10,11,12,-1,-1,-1,-1,-1,18,19,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PH
  {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PL
  {-1,-1,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,},//PM
};
static int version=0;
static int pwmmode=0;

/**
 *A20 Tools for Banana Pi 
 */

void sunxi_gpio_unexports(void)
{
  int fd ;
  int i, pin;

  if (wiringPiDebug)
	printf("%s\n", __func__);

  wiringPiSetup();
  
  for (i = 0 ; i < 32 ; ++i) 
  {
    if ((i & PI_GPIO_MASK) == 0)    // On-board pin
    {
      if (wiringPiMode == WPI_MODE_PINS)
       	pin = pinToGpio_BP [i] ;
      else if (wiringPiMode == WPI_MODE_PHYS)
      	pin = physToGpio_BP [i] ;
      else if (wiringPiMode == WPI_MODE_GPIO)
      	pin= pinTobcm_BP[i];//need map A20 to bcm
      else 
	  	return;

	  if (wiringPiDebug)
	    printf("%s, i= %d, pin = %d\n", __func__, i, pin);

	  if (-1 == pin)  /*VCC or GND return directly*/
  	  {
  		//printf("%s, the pin:%d is invaild,please check it over!\n", __func__, pin);
  		continue;
  	  }
    }
  
    if ((fd = fopen ("/sys/class/gpio/unexport", "w")) == NULL)
    {
      fprintf (stderr, "Unable to open GPIO export interface\n") ;
      exit (1) ;
    }

	if (wiringPiDebug)
	    printf("%s, i= %d, pin = %d\n", __func__, i, pin);
	
    fprintf (fd, "%d\n", pin) ;
    fclose (fd) ;
  }
}

void sunxi_gpio_exports(void)
{
  int fd ;
  int i, l, first, pin;
  char fName [128] ;
  char buf [16] ;

  if (wiringPiDebug)
	printf("%s\n", __func__);

  wiringPiSetup();

  for(first = 0, i = 0; i < 32; i++)
  {
    if ((i & PI_GPIO_MASK) == 0)    // On-board pin
    {
      if (wiringPiMode == WPI_MODE_PINS)
       	pin = pinToGpio_BP [i] ;
      else if (wiringPiMode == WPI_MODE_PHYS)
      	pin = physToGpio_BP [i] ;
      else if (wiringPiMode == WPI_MODE_GPIO)
      	pin= pinTobcm_BP[i];//need map A20 to bcm
      else 
	  	return;

	  if (wiringPiDebug)
	    printf("%s, i= %d, pin = %d\n", __func__, i, pin);

	  if (-1 == pin)  /*VCC or GND return directly*/
  	  {
  		//printf("%s, the pin:%d is invaild,please check it over!\n", __func__, pin);
  		continue;
  	  }
    }

    // Try to read the direction
    sprintf (fName, "/sys/class/gpio/gpio%d/direction", pin) ;
    if ((fd = open (fName, O_RDONLY)) == -1)
      continue ;

    if (first == 0)
    {
      ++first ;
      printf("GPIO Pins exported:\n") ;
    }

    printf("%d(BP=%d): ", i, pin) ;

    if ((l = read (fd, buf, 16)) == 0)
      sprintf(buf, "%s", "?") ;
 
    buf [l] = 0 ;
    if ((buf [strlen (buf) - 1]) == '\n')
      buf [strlen (buf) - 1] = 0 ;

    printf("direction=%-3s  ", buf) ;

    close (fd) ;

    // Try to Read the value
    sprintf (fName, "/sys/class/gpio/gpio%d/value", pin) ;
    if ((fd = open (fName, O_RDONLY)) == -1)
    {
      printf ("No Value file (huh?)\n") ;
      continue ;
    }

    if ((l = read (fd, buf, 16)) == 0)
      sprintf (buf, "%s", "?") ;

    buf [l] = 0 ;
    if ((buf [strlen (buf) - 1]) == '\n')
      buf [strlen (buf) - 1] = 0 ;

    printf("value=%s  ", buf) ;

    // Read any edge trigger file
    sprintf (fName, "/sys/class/gpio/gpio%d/edge", pin) ;
    if ((fd = open (fName, O_RDONLY)) == -1)
    {
      printf ("\n") ;
      continue ;
    }

    if ((l = read (fd, buf, 16)) == 0)
      sprintf (buf, "%s", "?") ;

    buf [l] = 0 ;
    if ((buf [strlen (buf) - 1]) == '\n')
      buf [strlen (buf) - 1] = 0 ;

    printf("edge=%-8s\n", buf) ;

    close (fd) ;
	
  }
}


#if 0
/**
 * [readl read with an address]
 * @param  addr [address]
 * @return      [value]
 */
uint32_t readl(uint32_t addr)
{
  uint32_t val = 0;
  uint32_t mmap_base = (addr & ~MAP_MASK);
  uint32_t mmap_seek = ((addr - mmap_base) >> 2);

  val = *(gpio + mmap_seek);

  return val;
}

/**
 * [writel write with an address]
 * @param val  [value]
 * @param addr [address]
 */
void writel(uint32_t val, uint32_t addr)
{
  uint32_t mmap_base = (addr & ~MAP_MASK);
  uint32_t mmap_seek = ((addr - mmap_base) >> 2);

  *(gpio + mmap_seek) = val;
}
#endif

uint32_t sunxi_pwm_readl(uint32_t addr)
{
  uint32_t val = 0;
  uint32_t mmap_base = (addr & ~MAP_MASK);
  uint32_t mmap_seek = ((addr - mmap_base) >> 2);

  val = *(pwm + mmap_seek);

  return val;
}

void sunxi_pwm_writel(uint32_t val, uint32_t addr)
{
  uint32_t mmap_base = (addr & ~MAP_MASK);
  uint32_t mmap_seek = ((addr - mmap_base) >> 2);

  *(pwm + mmap_seek) = val;
}

uint32_t sunxi_gpio_readl(uint32_t addr, int bank)
{
  uint32_t val = 0;
  uint32_t mmap_base = (addr & ~MAP_MASK);
  uint32_t mmap_seek = ((addr - mmap_base) >> 2);

  /* DK, for PL and PM */
  if(bank >= 8)
      val = *(gpio_lm+ mmap_seek);
  else
      val = *(gpio + mmap_seek);

  return val;
}

void sunxi_gpio_writel(uint32_t val, uint32_t addr, int bank)
{
  uint32_t mmap_base = (addr & ~MAP_MASK);
  uint32_t mmap_seek = ((addr - mmap_base) >> 2);

  if(bank >= 8)
      *(gpio_lm+ mmap_seek) = val;
  else
      *(gpio + mmap_seek) = val;
}

void sunxi_pwm_set_enable(int en, int pwm_ch)
{
  int val = 0;
  uint32_t pwm_ch_addr=0;

  if(pwm_ch == 1)
  	pwm_ch_addr = SUNXI_PWM_CH1_CTRL;
  else if(pwm_ch == 2)
  	pwm_ch_addr = SUNXI_PWM_CH2_CTRL;
  
  val = sunxi_pwm_readl(pwm_ch_addr);
  if(en)
  {
	val |= (SUNXI_PWM_CH_EN | SUNXI_PWM_SCLK_CH_GATING);
  } 
  else 
  {
	val &= ~(SUNXI_PWM_CH_EN | SUNXI_PWM_SCLK_CH_GATING);
  }
  
  if (wiringPiDebug)
	printf(">>function%s,no:%d,enable? :0x%x\n",__func__, __LINE__, val);
  
  sunxi_pwm_writel(val, pwm_ch_addr);
  delay (1) ;
}

void sunxi_pwm_set_mode(int mode, int pwm_ch)
{
  int val = 0;
  uint32_t pwm_ch_addr=0;

  if(pwm_ch == 1)
  	pwm_ch_addr = SUNXI_PWM_CH1_CTRL;
  else if(pwm_ch == 2)
  	pwm_ch_addr = SUNXI_PWM_CH2_CTRL;
	 
  val = sunxi_pwm_readl(pwm_ch_addr);
  mode &= 1; //cover the mode to 0 or 1
  if(mode)
  { //pulse mode
    val |= ( SUNXI_PWM_CH_MS_MODE|SUNXI_PWM_CH_PUL_START);
    pwmmode=1;
  }
  else 
  {  //cycle mode
    val &= ~( SUNXI_PWM_CH_MS_MODE);
    pwmmode=0;
  }
  
  val |= ( SUNXI_PWM_CH_ACT_STA);
  
  if (wiringPiDebug)
	printf("%s, %d, mode = 0x%x\n",__func__, __LINE__, val);
  
  sunxi_pwm_writel(val, pwm_ch_addr);

  delay (1) ;
	
  val = sunxi_pwm_readl(pwm_ch_addr);//get ch1 period_cys
  if (wiringPiDebug)
    printf("%s after set, channel: %d, mode: %d, phyaddr:0x%x\n",__func__, pwm_ch, val, pwm_ch_addr);
}

void sunxi_pwm_set_clk(int clk, int pwm_ch)
{
  int val = 0;
  uint32_t pwm_ch_addr=0;

  if(pwm_ch == 1)
  	pwm_ch_addr = SUNXI_PWM_CH1_CTRL;
  else if(pwm_ch == 2)
  	pwm_ch_addr = SUNXI_PWM_CH2_CTRL;
  
  val = sunxi_pwm_readl(pwm_ch_addr);

  //clear clk to 0
  val &= 0xfffffff0;
  val |= ((clk & 0xf) << 0);  //todo check wether clk is invalid or not
  sunxi_pwm_writel(val, pwm_ch_addr);
	 
  if (wiringPiDebug)
	printf(">>function%s,no:%d,clk? :0x%x\n",__func__, __LINE__, val);
	 
  delay (1) ;
}

/**
 * ch0 and ch1 set the same,16 bit period and 16 bit act
 */
uint32_t sunxi_pwm_get_period(int pwm_ch)
{
  uint32_t period_cys = 0;
  uint32_t pwm_ch_addr=0;

  if(pwm_ch == 1)
  	pwm_ch_addr = SUNXI_PWM_CH1_PERIOD;
  else if(pwm_ch == 2)
  	pwm_ch_addr = SUNXI_PWM_CH2_PERIOD;

  period_cys = sunxi_pwm_readl(pwm_ch_addr);//get ch1 period_cys
  period_cys &= 0xffff0000;//get period_cys
  period_cys = period_cys >> 16;

  if (wiringPiDebug)
    printf(">>func:%s,no:%d, channel: %d period/range: %d\n",__func__,__LINE__, pwm_ch, period_cys);
  
  delay (1);
  return period_cys;
}

void sunxi_pwm_set_period(int period_cys, int pwm_ch)
{
  uint32_t val = 0;
  uint32_t pwm_ch_addr=0;

  if(pwm_ch == 1)
  	pwm_ch_addr = SUNXI_PWM_CH1_PERIOD;
  else if(pwm_ch == 2)
  	pwm_ch_addr = SUNXI_PWM_CH2_PERIOD;

  if (wiringPiDebug)
    printf("%s before set, channel: %d, period/range: %d, phyaddr:0x%x\n",__func__, pwm_ch, period_cys, pwm_ch_addr);

  period_cys &= 0xffff; //set max period to 2^16
  period_cys = period_cys << 16;
  val = sunxi_pwm_readl(pwm_ch_addr);
  val &=0x0000ffff;
  val |= period_cys;
  sunxi_pwm_writel(val, pwm_ch_addr);

  delay (10) ;
  
  val = sunxi_pwm_readl(pwm_ch_addr);//get ch1 period_cys
  val &= 0xffff0000;//get period_cys
  val = val >> 16;

  if (wiringPiDebug)
    printf("%s after set, channel: %d, period/range: %d, phyaddr:0x%x\n",__func__, pwm_ch, val, pwm_ch_addr);
  
}


uint32_t sunxi_pwm_get_act(void)
{
  uint32_t period_act = 0;

  period_act = sunxi_pwm_readl(SUNXI_PWM_CH1_PERIOD);//get ch1 period_cys
  period_act &= 0xffff;//get period_act

  if (wiringPiDebug)
    printf(">>func:%s,no:%d,period/range:%d",__func__,__LINE__,period_act);
  delay (1) ;

  return period_act;
}

void sunxi_pwm_set_act(int act_cys, int pwm_ch)
{
  uint32_t per0 = 0;
  uint32_t pwm_ch_addr=0;
  
  //keep period the same, clear act_cys to 0 first
  if (wiringPiDebug)
    printf(">>func:%s no:%d\n",__func__,__LINE__);

  if(pwm_ch == 1)
  	pwm_ch_addr = SUNXI_PWM_CH1_PERIOD;
  else if(pwm_ch == 2)
  	pwm_ch_addr = SUNXI_PWM_CH2_PERIOD;

  act_cys &= 0xffff;
  per0 = sunxi_pwm_readl(pwm_ch_addr);
  per0 &= 0xffff0000;
  per0 |= act_cys;
  sunxi_pwm_writel(per0,pwm_ch_addr);
  delay (10) ;

  per0 = sunxi_pwm_readl(pwm_ch_addr);//get ch1 period_cys
  per0 &= 0xffff;//get period_cys

  if (wiringPiDebug)
    printf("%s after set, channel: %d, act: %d, phyaddr:0x%x\n",__func__, pwm_ch, per0, pwm_ch_addr);
  
}

void sunxi_pwm_clear_reg(int pwm_ch)
{
  if(pwm_ch == 1){
    sunxi_pwm_writel(0, SUNXI_PWM_CH1_CTRL); 
    sunxi_pwm_writel(0, SUNXI_PWM_CH1_PERIOD); 
  }
  else if(pwm_ch == 2){
    sunxi_pwm_writel(0, SUNXI_PWM_CH2_CTRL); 
    sunxi_pwm_writel(0, SUNXI_PWM_CH2_PERIOD);
  }
}

int sunxi_pwm_get_channel(int pin)
{	
  if(pin < 233 || pin > 236){
	return wiringPiFailure (WPI_FATAL, "sunxi_pw_channel: gpio %d not support PWM mode\n", pin) ;
  }

  //channel 1
  if(pin == 233 || pin == 234)
	return 1;

  //channel 2
  if(pin == 235 || pin == 236)
	return 2;
}

void sunxi_pwm_set_all(int pin)
{
  int pwm_ch;
  
  pwm_ch = sunxi_pwm_get_channel(pin); 

  //channel select fail
  if(pwm_ch == 1 || pwm_ch == 2)
  {
	if (wiringPiDebug)
        printf("%s, PWM channel select %d, pin = %d\n", __func__, pwm_ch, pin);
  }
  else
  {
    return wiringPiFailure (WPI_FATAL, "%s, PWM channel if wrong, channel: %d, pin: %d\n", pwm_ch, pin) ;
  }
  	
  sunxi_pwm_clear_reg(pwm_ch);
  
  //set default M:S to 1/2
  sunxi_pwm_set_period(1024, pwm_ch);
  sunxi_pwm_set_act(512, pwm_ch);
  sunxi_pwm_set_mode(PWM_MODE_MS, pwm_ch);
  sunxi_pwm_set_clk(PWM_CLK_DIV_64, pwm_ch);//default clk:24M/120
  sunxi_pwm_set_enable(1, pwm_ch);
  delayMicroseconds (200);
}

int sunxi_get_pin_mode(int pin)
{
  uint32_t regval = 0;
  int bank = pin >> 5;
  int index = pin - (bank << 5);
  int offset = ((index - ((index >> 3) << 3)) << 2);
  uint32_t reval=0;
  uint32_t phyaddr=0;

  /* for M2 PM and PL */
  if(bank >= 8)
    phyaddr = SUNXI_GPIO_LM_BASE + ((bank - 8) * 36) + ((index >> 3) << 2);
  else
  	phyaddr = SUNXI_GPIO_BASE + (bank * 36) + ((index >> 3) << 2);

  if (wiringPiDebug)
    printf("func:%s pin:%d,  bank:%d index:%d phyaddr:0x%x\n",__func__, pin , bank,index,phyaddr);

  if(BP_PIN_MASK[bank][index] != -1)
  {
    regval = sunxi_gpio_readl(phyaddr, bank);
	
    if (wiringPiDebug)
      printf("read reg val: 0x%x offset:%d  return: %d\n",regval,offset,reval);

    //reval=regval &(reval+(7 << offset));
    reval=(regval>>offset)&7;

    if (wiringPiDebug)
      printf("read reg val: 0x%x offset:%d  return: %d\n",regval,offset,reval);

    return reval;
  }
  else 
  {
    printf("line:%dpin number error\n",__LINE__);
    return reval;
  } 
}

void sunxi_set_pin_mode(int pin,int mode)
{
  uint32_t regval = 0;
  int bank = pin >> 5;
  int index = pin - (bank << 5);
  int offset = ((index - ((index >> 3) << 3)) << 2);
  uint32_t phyaddr=0;

  /* for M2 PM and PL */
  if(bank >= 8)
    phyaddr = SUNXI_GPIO_LM_BASE + ((bank - 8) * 36) + ((index >> 3) << 2);
  else
    phyaddr = SUNXI_GPIO_BASE + (bank * 36) + ((index >> 3) << 2);

  if (wiringPiDebug)
    printf("func:%s pin:%d, MODE:%d bank:%d index:%d phyaddr:0x%x\n",__func__, pin , mode,bank,index,phyaddr);

  if(BP_PIN_MASK[bank][index] != -1)
  {
    regval = sunxi_gpio_readl(phyaddr, bank);
	
    if (wiringPiDebug)
      printf("read reg val: 0x%x offset:%d\n",regval,offset);

    if(INPUT == mode)
    {
      regval &= ~(7 << offset);
      sunxi_gpio_writel(regval, phyaddr, bank);
      regval = sunxi_gpio_readl(phyaddr, bank);

      if (wiringPiDebug)
        printf("Input mode set over reg val: 0x%x\n",regval);
    }
    else if(OUTPUT == mode)
    {
      regval &= ~(7 << offset);
      regval |=  (1 << offset);
      if (wiringPiDebug)
        printf("Out mode ready set val: 0x%x\n",regval);

      sunxi_gpio_writel(regval, phyaddr, bank);
      regval = sunxi_gpio_readl(phyaddr, bank);
	  
      if (wiringPiDebug)
        printf("Out mode set over reg val: 0x%x\n",regval);
    } 
    else if(PWM_OUTPUT == mode)
    {
      //set pin PWMx to pwm mode
      regval &= ~(7 << offset);
      regval |=  (0x4 << offset);
	  
      if (wiringPiDebug)
        printf(">>>>>line:%d PWM mode ready to set val: 0x%x\n",__LINE__,regval);

      sunxi_gpio_writel(regval, phyaddr, bank);
      delayMicroseconds (200);
      regval = sunxi_gpio_readl(phyaddr, bank);
	  
      if (wiringPiDebug)
        printf("<<<<<PWM mode set over reg val: 0x%x\n",regval); 

	  //register configure
	  sunxi_pwm_set_all(pin);
    }
	else if(I2C_PIN == mode)
    {
      //set pin to i2c mode
      regval &= ~(7 << offset);
      regval |=  (0x2 << offset);

      sunxi_gpio_writel(regval, phyaddr, bank);
      delayMicroseconds (200);
      regval = sunxi_gpio_readl(phyaddr, bank);
	  
      if (wiringPiDebug)
        printf("<<<<<PWM mode set over reg val: 0x%x\n",regval); 
    }
	else if(SPI_PIN == mode)
    {
      //set pin to spi mode
      regval &= ~(7 << offset);
      regval |=  (0x2 << offset);

      sunxi_gpio_writel(regval, phyaddr, bank);
      delayMicroseconds (200);
      regval = sunxi_gpio_readl(phyaddr, bank);
	  
      if (wiringPiDebug)
        printf("<<<<<PWM mode set over reg val: 0x%x\n",regval); 
    }
  }
  else 
  {
    printf("line:%dpin number error\n",__LINE__);
  }

	return ;
}

void sunxi_digitalWrite(int pin, int value)
{ 
  uint32_t regval = 0;
  int bank = pin >> 5;
  int index = pin - (bank << 5);
  uint32_t phyaddr=0;

  /* for M2 PM and PL */
  if(bank >= 8)
    phyaddr = SUNXI_GPIO_LM_BASE + ((bank - 8) * 36) + 0x10;
  else
     phyaddr = SUNXI_GPIO_BASE + (bank * 36) + 0x10;

  if (wiringPiDebug)
    printf("func:%s pin:%d, value:%d bank:%d index:%d phyaddr:0x%x\n",__func__, pin , value,bank,index,phyaddr);

  if(BP_PIN_MASK[bank][index] != -1)
  {
    regval = sunxi_gpio_readl(phyaddr, bank);
	
    if (wiringPiDebug)
      printf("befor write reg val: 0x%x,index:%d\n",regval,index);

    if(0 == value)
    {
      regval &= ~(1 << index);
      sunxi_gpio_writel(regval, phyaddr, bank);
      regval = sunxi_gpio_readl(phyaddr, bank);
	  
      if (wiringPiDebug)
        printf("LOW val set over reg val: 0x%x\n",regval);
    }
    else
    {
      regval |= (1 << index);
      sunxi_gpio_writel(regval, phyaddr, bank);
      regval = sunxi_gpio_readl(phyaddr, bank);
	  
      if (wiringPiDebug)
        printf("HIGH val set over reg val: 0x%x\n",regval);
    }
  }
  else
  {
    printf("pin number error\n");
  }
	 
	 return ;
}

int sunxi_digitalRead(int pin)
{ 
  uint32_t regval = 0;
  int bank = pin >> 5;
  int index = pin - (bank << 5);
  uint32_t phyaddr=0;

  /* for M2 PM and PL */
  if(bank >= 8)
    phyaddr = SUNXI_GPIO_LM_BASE + ((bank - 8) * 36) + 0x10;
  else
 	phyaddr = SUNXI_GPIO_BASE + (bank * 36) + 0x10;

  if (wiringPiDebug)
    printf("func:%s pin:%d,bank:%d index:%d phyaddr:0x%x\n",__func__, pin,bank,index,phyaddr); 
  
  if(BP_PIN_MASK[bank][index] != -1)
  {
    regval = sunxi_gpio_readl(phyaddr, bank);
    regval = regval >> index;
    regval &= 1;
	
    if (wiringPiDebug)
      printf("***** read reg val: 0x%x,bank:%d,index:%d,line:%d\n",regval,bank,index,__LINE__);
	
    return regval;
  }
  else
  {
    printf("pin number error\n");
    return regval;
  } 
}

void sunxi_pullUpDnControl (int pin, int pud)
{
  uint32_t regval = 0;
  int bank = pin >> 5;
  int index = pin - (bank << 5);
  int sub = index >> 4;
  int sub_index = index - 16*sub;
  uint32_t phyaddr=0;

  /* for M2 PM and PL */
  if(bank >= 8)
    phyaddr = SUNXI_GPIO_LM_BASE + ((bank - 8) * 36) + 0x1c + sub*4;
  else
 	phyaddr = SUNXI_GPIO_BASE + (bank * 36) + 0x1c + sub*4;

  if (wiringPiDebug)
	printf("func:%s pin:%d,bank:%d index:%d sub:%d phyaddr:0x%x\n",__func__, pin,bank,index,sub,phyaddr); 
  
  if(BP_PIN_MASK[bank][index] != -1)
  {  //PI13~PI21 need check again
    regval = sunxi_gpio_readl(phyaddr, bank);
	
	if (wiringPiDebug)
	  printf("pullUpDn reg:0x%x, pud:0x%x sub_index:%d\n", regval, pud, sub_index);
	
	regval &= ~(3 << (sub_index << 1));
	regval |= (pud << (sub_index << 1));
	
	if (wiringPiDebug)
	  printf("pullUpDn val ready to set:0x%x\n", regval);
	
	sunxi_gpio_writel(regval, phyaddr, bank);
	regval = sunxi_gpio_readl(phyaddr, bank);
	
	if (wiringPiDebug)
	  printf("pullUpDn reg after set:0x%x  addr:0x%x\n", regval, phyaddr);
  }
  else 
  {
    printf("pin number error\n");
  } 
  
  delay (1) ;	
  
  return ;
}

/*
 * wiringPiFailure:
 *	Fail. Or not.
 *********************************************************************************
 */

int wiringPiFailure (int fatal, const char *message, ...)
{
  va_list argp ;
  char buffer [1024] ;

  if (!fatal && wiringPiReturnCodes)
    return -1 ;

  va_start (argp, message) ;
    vsnprintf (buffer, 1023, message, argp) ;
  va_end (argp) ;

  fprintf (stderr, "%s", buffer) ;
  exit (EXIT_FAILURE) ;

  return 0 ;
}


/*
 * piBoardRev:
 *	Return a number representing the hardware revision of the board.
 *
 *	Revision 1 really means the early Model B's.
 *	Revision 2 is everything else - it covers the B, B+ and CM.
 *		... and the Pi 2 - which is a B+ ++  ...
 *
 *	Seems there are some boards with 0000 in them (mistake in manufacture)
 *	So the distinction between boards that I can see is:
 *	0000 - Error
 *	0001 - Not used 
 *	0002 - Model B,  Rev 1,   256MB, Egoman
 *	0003 - Model B,  Rev 1.1, 256MB, Egoman, Fuses/D14 removed.
 *	0004 - Model B,  Rev 2,   256MB, Sony
 *	0005 - Model B,  Rev 2,   256MB, Qisda
 *	0006 - Model B,  Rev 2,   256MB, Egoman
 *	0007 - Model A,  Rev 2,   256MB, Egoman
 *	0008 - Model A,  Rev 2,   256MB, Sony
 *	0009 - Model A,  Rev 2,   256MB, Qisda
 *	000d - Model B,  Rev 2,   512MB, Egoman
 *	000e - Model B,  Rev 2,   512MB, Sony
 *	000f - Model B,  Rev 2,   512MB, Qisda
 *	0010 - Model B+, Rev 1.2, 512MB, Sony
 *	0011 - Pi CM,    Rev 1.2, 512MB, Sony
 *	0012 - Model A+  Rev 1.2, 256MB, Sony
 *	0014 - Pi CM,    Rev 1.1, 512MB, Sony (Actual Revision might be different)
 *
 *	For the Pi 2:
 *	0010 - Model 2, Rev 1.1, Quad Core, 1GB, Sony
 *
 *	A small thorn is the olde style overvolting - that will add in
 *		1000000
 *
 *	The Pi compute module has an revision of 0011 - since we only check the
 *	last digit, then it's 1, therefore it'll default to not 2 or 3 for a
 *	Rev 1, so will appear as a Rev 2. This is fine for the most part, but
 *	we'll properly detect the Compute Module later and adjust accordingly.
 *	And the next rev of the CN is 0014 ...
 *
 *********************************************************************************
 */

static void piBoardRevOops (const char *why)
{
  fprintf (stderr, "piBoardRev: Unable to determine board revision from /proc/cpuinfo\n") ;
  fprintf (stderr, " -> %s\n", why) ;
  fprintf (stderr, " ->  You may want to check:\n") ;
  exit (EXIT_FAILURE) ;
}

int isA31s(void)
{
  FILE *cpuFd ;
  char line [120] ;
  char *d;

  if ((cpuFd = fopen ("/proc/cpuinfo", "r")) == NULL)
    piBoardRevOops ("Unable to open /proc/cpuinfo") ;

  while (fgets (line, 120, cpuFd) != NULL)
  {
    if (strncmp (line, "Hardware", 8) == 0)
    break ;
  }
		
	fclose (cpuFd) ;
	if (strncmp (line, "Hardware", 8) != 0)
		piBoardRevOops ("No \"Hardware\" line") ;
	
  for (d = &line [strlen (line) - 1] ; (*d == '\n') || (*d == '\r') ; --d)
    *d = 0 ;
  if (wiringPiDebug)
    printf ("piboardRev: Hardware string: %s\n", line) ;
	
	if (strstr(line,"sun6i") != NULL)
	{
		if (wiringPiDebug)
		printf ("Hardware:%s\n",line) ;
		return 1 ;
	}
	else
	{
		if (wiringPiDebug)
		printf ("Hardware:%s\n",line) ;
		return 0 ;
	}
}

int piBoardRev (void)
{
  FILE *cpuFd ;
  char line [120] ;
  char *c ;
  static int  boardRev = -1 ;

  if(isA31s())
  {
	version = BPVER;
		if (wiringPiDebug)
			printf ("piboardRev:  %d\n", version) ;
		return BPVER ;
  }

 
  if (boardRev != -1)	// No point checking twice
    return boardRev ;

  if ((cpuFd = fopen ("/proc/cpuinfo", "r")) == NULL)
    piBoardRevOops ("Unable to open /proc/cpuinfo") ;

// Start by looking for the Architecture, then we can look for a B2 revision....

  while (fgets (line, 120, cpuFd) != NULL)
    if (strncmp (line, "Hardware", 8) == 0)
      break ;

  if (strncmp (line, "Hardware", 8) != 0)
    piBoardRevOops ("No \"Hardware\" line") ;

  if (wiringPiDebug)
    printf ("piboardRev: Hardware: %s\n", line) ;

// See if it's BCM2708 or BCM2709

  if (strstr (line, "BCM2709") != NULL)
    piModel2 = TRUE ;
  else if (strstr (line, "BCM2708") == NULL)
  {
    fprintf (stderr, "Unable to determine hardware version. I see: %s,\n", line) ;
    fprintf (stderr, " - expecting BCM2708 or BCM2709. Please report this to projects@drogon.net\n") ;
    exit (EXIT_FAILURE) ;
  }

// Now do the rest of it as before

  rewind (cpuFd) ;

  while (fgets (line, 120, cpuFd) != NULL)
    if (strncmp (line, "Revision", 8) == 0)
      break ;

  fclose (cpuFd) ;

  if (strncmp (line, "Revision", 8) != 0)
    piBoardRevOops ("No \"Revision\" line") ;

// Chomp trailing CR/NL

  for (c = &line [strlen (line) - 1] ; (*c == '\n') || (*c == '\r') ; --c)
    *c = 0 ;
  
  if (wiringPiDebug)
    printf ("piboardRev: Revision string: %s\n", line) ;

// Scan to first digit

  for (c = line ; *c ; ++c)
    if (isdigit (*c))
      break ;

  if (!isdigit (*c))
    piBoardRevOops ("No numeric revision string") ;

// Make sure its long enough

  if (strlen (c) < 4)
    piBoardRevOops ("Bogus \"Revision\" line (too small)") ;
  
// If you have overvolted the Pi, then it appears that the revision
//	has 100000 added to it!
// The actual condition for it being set is:
//	 (force_turbo || current_limit_override || temp_limit>85) && over_voltage>0

  if (wiringPiDebug)
    if (strlen (c) != 4)
      printf ("piboardRev: This Pi has/is (force_turbo || current_limit_override || temp_limit>85) && over_voltage>0\n") ;

// Isolate  last 4 characters:

  c = c + strlen (c) - 4 ;

  if (wiringPiDebug)
    printf ("piboardRev: last4Chars are: \"%s\"\n", c) ;

  if ( (strcmp (c, "0002") == 0) || (strcmp (c, "0003") == 0))
    boardRev = 1 ;
  else
    boardRev = 2 ;	// Covers everything else from the B revision 2 to the B+, the Pi v2 and CM's.

  if (wiringPiDebug)
    printf ("piBoardRev: Returning revision: %d\n", boardRev) ;

  return boardRev ;
}


/*
 * piBoardId:
 *	Do more digging into the board revision string as above, but return
 *	as much details as we can.
 *	This is undocumented and really only intended for the GPIO command.
 *	Use at your own risk!
 *
 * for Pi v2:
 *   [USER:8] [NEW:1] [MEMSIZE:3] [MANUFACTURER:4] [PROCESSOR:4] [TYPE:8] [REV:4]
 *   NEW          23: will be 1 for the new scheme, 0 for the old scheme
 *   MEMSIZE      20: 0=256M 1=512M 2=1G
 *   MANUFACTURER 16: 0=SONY 1=EGOMAN 2=EMBEST
 *   PROCESSOR    12: 0=2835 1=2836
 *   TYPE         04: 0=MODELA 1=MODELB 2=MODELA+ 3=MODELB+ 4=Pi2 MODEL B 5=ALPHA 6=CM
 *   REV          00: 0=REV0 1=REV1 2=REV2
 *********************************************************************************
 */

void piBoardId (int *model, int *rev, int *mem, int *maker, int *overVolted)
{
  FILE *cpuFd ;
  char line [120] ;
  char *c ;

//	Will deal with the properly later on - for now, lets just get it going...
//  unsigned int modelNum ;

  (void)piBoardRev () ;	// Call this first to make sure all's OK. Don't care about the result.

  if ((cpuFd = fopen ("/proc/cpuinfo", "r")) == NULL)
    piBoardRevOops ("Unable to open /proc/cpuinfo") ;

  while (fgets (line, 120, cpuFd) != NULL)
    if (strncmp (line, "Revision", 8) == 0)
      break ;

  fclose (cpuFd) ;

  if (strncmp (line, "Revision", 8) != 0)
    piBoardRevOops ("No \"Revision\" line") ;

// Chomp trailing CR/NL

  for (c = &line [strlen (line) - 1] ; (*c == '\n') || (*c == '\r') ; --c)
    *c = 0 ;
  
  if (wiringPiDebug)
    printf ("piboardId: Revision string: %s\n", line) ;

  if (piModel2)
  {

// Scan to the colon

    for (c = line ; *c ; ++c)
      if (*c == ':')
	break ;

    if (*c != ':')
      piBoardRevOops ("Bogus \"Revision\" line (no colon)") ;

//    modelNum = (unsigned int)strtol (++c, NULL, 16) ; // Hex number with no leading 0x
    
    *model = PI_MODEL_2  ;
    *rev   = PI_VERSION_1_1 ;
    *mem   = 1024 ;
    *maker = PI_MAKER_SONY   ;
  }
  else
  {

// Scan to first digit

    for (c = line ; *c ; ++c)
      if (isdigit (*c))
	break ;

// Make sure its long enough

    if (strlen (c) < 4)
      piBoardRevOops ("Bogus \"Revision\" line (not long enough)") ;

// If longer than 4, we'll assume it's been overvolted

    *overVolted = strlen (c) > 4 ;
  
// Extract last 4 characters:

    c = c + strlen (c) - 4 ;

// Fill out the replys as appropriate

    /**/ if (strcmp (c, "0002") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_1   ; *mem = 256 ; *maker = PI_MAKER_EGOMAN ; }
    else if (strcmp (c, "0003") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_1_1 ; *mem = 256 ; *maker = PI_MAKER_EGOMAN ; }
    else if (strcmp (c, "0004") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_2   ; *mem = 256 ; *maker = PI_MAKER_SONY   ; }
    else if (strcmp (c, "0005") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_2   ; *mem = 256 ; *maker = PI_MAKER_QISDA  ; }
    else if (strcmp (c, "0006") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_2   ; *mem = 256 ; *maker = PI_MAKER_EGOMAN ; }
    else if (strcmp (c, "0007") == 0) { *model = PI_MODEL_A  ; *rev = PI_VERSION_2   ; *mem = 256 ; *maker = PI_MAKER_EGOMAN ; }
    else if (strcmp (c, "0008") == 0) { *model = PI_MODEL_A  ; *rev = PI_VERSION_2   ; *mem = 256 ; *maker = PI_MAKER_SONY ; ; }
    else if (strcmp (c, "0009") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_2   ; *mem = 256 ; *maker = PI_MAKER_QISDA  ; }
    else if (strcmp (c, "000d") == 0) { *model = PI_MODEL_BM ; *rev = PI_VERSION_1_2 ; *mem = 1024; *maker = PI_MAKER_BPI    ; }
    else if (strcmp (c, "000e") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_2   ; *mem = 512 ; *maker = PI_MAKER_SONY   ; }
    else if (strcmp (c, "000f") == 0) { *model = PI_MODEL_B  ; *rev = PI_VERSION_2   ; *mem = 512 ; *maker = PI_MAKER_EGOMAN ; }
    else if (strcmp (c, "0010") == 0) { *model = PI_MODEL_BP ; *rev = PI_VERSION_1_2 ; *mem = 512 ; *maker = PI_MAKER_SONY   ; }
    else if (strcmp (c, "0011") == 0) { *model = PI_MODEL_CM ; *rev = PI_VERSION_1_2 ; *mem = 512 ; *maker = PI_MAKER_SONY   ; }
    else if (strcmp (c, "0012") == 0) { *model = PI_MODEL_AP ; *rev = PI_VERSION_1_2 ; *mem = 256 ; *maker = PI_MAKER_SONY   ; }
    else if (strcmp (c, "0013") == 0) { *model = PI_MODEL_BP ; *rev = PI_VERSION_1_2 ; *mem = 512 ; *maker = PI_MAKER_MBEST  ; }
    else if (strcmp (c, "0014") == 0) { *model = PI_MODEL_CM ; *rev = PI_VERSION_1_2 ; *mem = 512 ; *maker = PI_MAKER_SONY   ; }
    else if (strcmp (c, "0000") == 0) { *model = PI_MODEL_BM;  *rev = PI_VERSION_1_2;  *mem = 1024;  *maker = PI_MAKER_BPI;}
	else                              { *model = 0           ; *rev = 0              ; *mem =   0 ; *maker = 0 ;               }
  }
}
 


/*
 * wpiPinToGpio:
 *	Translate a wiringPi Pin number to native GPIO pin number.
 *	Provided for external support.
 *********************************************************************************
 */

int wpiPinToGpio (int wpiPin)
{
  return pinToGpio [wpiPin & 63] ;
}


/*
 * physPinToGpio:
 *	Translate a physical Pin number to native GPIO pin number.
 *	Provided for external support.
 *********************************************************************************
 */

int physPinToGpio (int physPin)
{
  return physToGpio [physPin & 63] ;
}


/*
 * setPadDrive:
 *	Set the PAD driver value
 *********************************************************************************
 */

void setPadDrive (int group, int value)
{
  uint32_t wrVal ;
  if(BPVER == version) 
  	return;
  if ((wiringPiMode == WPI_MODE_PINS) || (wiringPiMode == WPI_MODE_PHYS) || (wiringPiMode == WPI_MODE_GPIO))
  {
    if ((group < 0) || (group > 2))
      return ;

    wrVal = BCM_PASSWORD | 0x18 | (value & 7) ;
    *(pads + group + 11) = wrVal ;

    if (wiringPiDebug)
    {
      printf ("setPadDrive: Group: %d, value: %d (%08X)\n", group, value, wrVal) ;
      printf ("Read : %08X\n", *(pads + group + 11)) ;
    }
  }
}


/*
 * getAlt:
 *	Returns the ALT bits for a given port. Only really of-use
 *	for the gpio readall command (I think)
 *********************************************************************************
 */

int getAlt (int pin)
{
  int fSel, shift, alt ;

  if(version == BPVER)
  {
    pin &= 63 ;

    if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio_BP [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio_BP[pin] ;
    else if (wiringPiMode == WPI_MODE_GPIO)
      pin=pinTobcm_BP[pin];//need map A20 to bcm
    else return 0 ;
		
		if(-1 == pin)
		{
			printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
			return -1;
		}
    alt=sunxi_get_pin_mode(pin);

    return alt ;
  }
  else
  {

    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode != WPI_MODE_GPIO)
      return 0 ;

    fSel    = gpioToGPFSEL [pin] ;
    shift   = gpioToShift  [pin] ;

    alt = (*(gpio + fSel) >> shift) & 7 ;

    return alt ;
  }
}

/*
 * pwmSetMode:
 *	Select the native "balanced" mode, or standard mark:space mode
 *********************************************************************************
 */

void pwmSetMode (int mode)
{
/*
    if(version == BPVER)
	{
        sunxi_pwm_set_mode(mode);
		return;
	}
	*/
  if ((wiringPiMode == WPI_MODE_PINS) || (wiringPiMode == WPI_MODE_PHYS) || (wiringPiMode == WPI_MODE_GPIO))
  {
    if (mode == PWM_MODE_MS)
      *(pwm + PWM_CONTROL) = PWM0_ENABLE | PWM1_ENABLE | PWM0_MS_MODE | PWM1_MS_MODE ;
    else
      *(pwm + PWM_CONTROL) = PWM0_ENABLE | PWM1_ENABLE ;

  }
}

/*
 * pwmSetRange:
 *	Set the PWM range register. We set both range registers to the same
 *	value. If you want different in your own code, then write your own.
 *********************************************************************************
 */

void pwmSetRange (unsigned int range)
{
/*
  if(version == BPVER)
  {
    sunxi_pwm_set_period(range);
	return;
  }
  */
  
  if ((wiringPiMode == WPI_MODE_PINS) || (wiringPiMode == WPI_MODE_PHYS) || (wiringPiMode == WPI_MODE_GPIO))
  {
    *(pwm + PWM0_RANGE) = range ; delayMicroseconds (10) ;
    *(pwm + PWM1_RANGE) = range ; delayMicroseconds (10) ;
  }

}


/*
 * pwmSetClock:
 *	Set/Change the PWM clock. Originally my code, but changed
 *	(for the better!) by Chris Hall, <chris@kchall.plus.com>
 *	after further study of the manual and testing with a 'scope
 *********************************************************************************
 */

void pwmSetClock (int divisor)
{
  uint32_t pwm_control ;

  if ((wiringPiMode == WPI_MODE_PINS) || (wiringPiMode == WPI_MODE_PHYS) || (wiringPiMode == WPI_MODE_GPIO))
  {
    if (wiringPiDebug)
      printf ("Setting to: %d. Current: 0x%08X\n", divisor, *(clk + PWMCLK_DIV)) ;
/*
   if(version == BPVER)
   {
      sunxi_pwm_set_clk(divisor);
      sunxi_pwm_set_enable(1);
	  return;
    }
*/
  divisor &= 4095 ;
    pwm_control = *(pwm + PWM_CONTROL) ;		// preserve PWM_CONTROL

// We need to stop PWM prior to stopping PWM clock in MS mode otherwise BUSY
// stays high.

    *(pwm + PWM_CONTROL) = 0 ;				// Stop PWM

// Stop PWM clock before changing divisor. The delay after this does need to
// this big (95uS occasionally fails, 100uS OK), it's almost as though the BUSY
// flag is not working properly in balanced mode. Without the delay when DIV is
// adjusted the clock sometimes switches to very slow, once slow further DIV
// adjustments do nothing and it's difficult to get out of this mode.

      *(clk + PWMCLK_CNTL) = BCM_PASSWORD | 0x01 ;	// Stop PWM Clock
      delayMicroseconds (110) ;			// prevents clock going sloooow

      while ((*(clk + PWMCLK_CNTL) & 0x80) != 0)	// Wait for clock to be !BUSY
      delayMicroseconds (1) ;

      *(clk + PWMCLK_DIV)  = BCM_PASSWORD | (divisor << 12) ;

      *(clk + PWMCLK_CNTL) = BCM_PASSWORD | 0x11 ;	// Start PWM clock
      *(pwm + PWM_CONTROL) = pwm_control ;		// restore PWM_CONTROL

      if (wiringPiDebug)
        printf ("Set     to: %d. Now    : 0x%08X\n", divisor, *(clk + PWMCLK_DIV)) ;
    }
	
}

/*
 * gpioClockSet:
 *	Set the freuency on a GPIO clock pin
 *********************************************************************************
 */

void gpioClockSet (int pin, int freq)
{
  int divi, divr, divf ;

  if(version == BPVER)
  {
    return ;
  }
  else
  {
    pin &= 63 ;

    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode != WPI_MODE_GPIO)
      return ;
  
    divi = 19200000 / freq ;
    divr = 19200000 % freq ;
    divf = (int)((double)divr * 4096.0 / 19200000.0) ;

    if (divi > 4095)
      divi = 4095 ;

    *(clk + gpioToClkCon [pin]) = BCM_PASSWORD | GPIO_CLOCK_SOURCE ;		// Stop GPIO Clock
    while ((*(clk + gpioToClkCon [pin]) & 0x80) != 0)				// ... and wait
      ;

    *(clk + gpioToClkDiv [pin]) = BCM_PASSWORD | (divi << 12) | divf ;		// Set dividers
    *(clk + gpioToClkCon [pin]) = BCM_PASSWORD | 0x10 | GPIO_CLOCK_SOURCE ;	// Start Clock
  }
}

/*
 * wiringPiFindNode:
 *      Locate our device node
 *********************************************************************************
 */

struct wiringPiNodeStruct *wiringPiFindNode (int pin)
{
  struct wiringPiNodeStruct *node = wiringPiNodes ;

  while (node != NULL)
    if ((pin >= node->pinBase) && (pin <= node->pinMax))
      return node ;
    else
      node = node->next ;

  return NULL ;
}

/*
 * wiringPiNewNode:
 *	Create a new GPIO node into the wiringPi handling system
 *********************************************************************************
 */

static void pinModeDummy             (struct wiringPiNodeStruct *node, int pin, int mode)  { return ; }
static void pullUpDnControlDummy     (struct wiringPiNodeStruct *node, int pin, int pud)   { return ; }
static int  digitalReadDummy         (struct wiringPiNodeStruct *node, int pin)            { return LOW ; }
static void digitalWriteDummy        (struct wiringPiNodeStruct *node, int pin, int value) { return ; }
static void pwmWriteDummy            (struct wiringPiNodeStruct *node, int pin, int value) { return ; }
static int  analogReadDummy          (struct wiringPiNodeStruct *node, int pin)            { return 0 ; }
static void analogWriteDummy         (struct wiringPiNodeStruct *node, int pin, int value) { return ; }

struct wiringPiNodeStruct *wiringPiNewNode (int pinBase, int numPins)
{
  int    pin ;
  struct wiringPiNodeStruct *node ;

// Minimum pin base is 64

  if (pinBase < 64)
    (void)wiringPiFailure (WPI_FATAL, "wiringPiNewNode: pinBase of %d is < 64\n", pinBase) ;

// Check all pins in-case there is overlap:

  for (pin = pinBase ; pin < (pinBase + numPins) ; ++pin)
    if (wiringPiFindNode (pin) != NULL)
      (void)wiringPiFailure (WPI_FATAL, "wiringPiNewNode: Pin %d overlaps with existing definition\n", pin) ;

  node = (struct wiringPiNodeStruct *)calloc (sizeof (struct wiringPiNodeStruct), 1) ;	// calloc zeros
  if (node == NULL)
    (void)wiringPiFailure (WPI_FATAL, "wiringPiNewNode: Unable to allocate memory: %s\n", strerror (errno)) ;

  node->pinBase         = pinBase ;
  node->pinMax          = pinBase + numPins - 1 ;
  node->pinMode         = pinModeDummy ;
  node->pullUpDnControl = pullUpDnControlDummy ;
  node->digitalRead     = digitalReadDummy ;
  node->digitalWrite    = digitalWriteDummy ;
  node->pwmWrite        = pwmWriteDummy ;
  node->analogRead      = analogReadDummy ;
  node->analogWrite     = analogWriteDummy ;
  node->next            = wiringPiNodes ;
  wiringPiNodes         = node ;

  return node ;
}


#ifdef notYetReady
/*
 * pinED01:
 * pinED10:
 *	Enables edge-detect mode on a pin - from a 0 to a 1 or 1 to 0
 *	Pin must already be in input mode with appropriate pull up/downs set.
 *********************************************************************************
 */

void pinEnableED01Pi (int pin)
{
  pin = pinToGpio [pin & 63] ;
}
#endif

/*
 *********************************************************************************
 * Core Functions
 *********************************************************************************
 */

/*
 * pinModeAlt:
 *	This is an un-documented special to let you set any pin to any mode
 *********************************************************************************
 */

void pinModeAlt (int pin, int mode)
{
  int fSel, shift ;

  if (BPVER == version)
  {
  		return;
  }
 
  if ((pin & PI_GPIO_MASK) == 0)		// On-board pin
  {
    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode != WPI_MODE_GPIO)
      return ;

    fSel  = gpioToGPFSEL [pin] ;
    shift = gpioToShift  [pin] ;

    *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) | ((mode & 0x7) << shift) ;
  }
}


/*
 * pinMode:
 *	Sets the mode of a pin to be input, output or PWM output
 *********************************************************************************
 */

void pinMode (int pin, int mode)
{
  int    fSel, shift, alt ;
  struct wiringPiNodeStruct *node = wiringPiNodes ;
  int origPin = pin ;
 
if(version == 3)
{

if ((pin & PI_GPIO_MASK) == 0)    // On-board pin
  {
    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio_BP [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio_BP [pin] ;
    else if (wiringPiMode == WPI_MODE_GPIO)
      pin= pinTobcm_BP[pin];//need map A20 to bcm
    else return;
				if (-1 == pin)  /*VCC or GND return directly*/
				{
					//printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
					return;
				}
	
 if (wiringPiDebug)
     printf ("%s,%d,pin:%d,mode:%d\n", __func__, __LINE__,pin,mode) ;

     softPwmStop (origPin) ;
     softToneStop (origPin) ;

      if (mode == INPUT)
      {
        sunxi_set_pin_mode(pin,INPUT);
        wiringPinMode = INPUT;
        return ;
      }
      else if (mode == OUTPUT)
      {
        sunxi_set_pin_mode(pin, OUTPUT);
        wiringPinMode = OUTPUT;
        return ;
      }
      else if (mode == PWM_OUTPUT)
      {
        if(pin < 233 || pin > 236)
           {
            printf("the pin you choose is not surport hardware PWM\n");
          printf("you can select PH9-PH12 for PWM pin\n");
            printf("or you can use it in softPwm mode\n");
              return ;
           }
          else
            {
              printf("you choose the hardware PWM:%d\n", 1);
            }
        sunxi_set_pin_mode(pin,PWM_OUTPUT);
        wiringPinMode = PWM_OUTPUT;
		  
        return ;
      }
	  else if (mode == I2C_PIN)
	  {
	    sunxi_set_pin_mode(pin, I2C_PIN);
		wiringPinMode = I2C_PIN;
	  }
	  else if (mode == SPI_PIN)
	  {
	    sunxi_set_pin_mode(pin, SPI_PIN);
		wiringPinMode = SPI_PIN;
	  }
      else if (mode == PULLUP)
      {
       pullUpDnControl (origPin, 1);
        wiringPinMode = PULLUP;
       return ;
     }
      else if (mode == PULLDOWN)
      {
        pullUpDnControl (origPin, 2);
       wiringPinMode = PULLDOWN;
       return ;
      }
     else if (mode == PULLOFF)
      {
      pullUpDnControl (origPin, 0);
       wiringPinMode = PULLOFF;
        return ;
      }
      else
       return ;
    }
    else
   {
      if ((node = wiringPiFindNode (pin)) != NULL)
        node->pinMode (node, pin, mode) ;
       return ;
    }
}
else
{
  if ((pin & PI_GPIO_MASK) == 0)		// On-board pin
  {
    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode != WPI_MODE_GPIO)
      return ;

    softPwmStop  (origPin) ;
    softToneStop (origPin) ;

    fSel    = gpioToGPFSEL [pin] ;
    shift   = gpioToShift  [pin] ;

    /**/ if (mode == INPUT)
      *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) ; // Sets bits to zero = input
    else if (mode == OUTPUT)
      *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) | (1 << shift) ;
    else if (mode == SOFT_PWM_OUTPUT)
      softPwmCreate (origPin, 0, 100) ;
    else if (mode == SOFT_TONE_OUTPUT)
      softToneCreate (origPin) ;
    else if (mode == PWM_TONE_OUTPUT)
    {
      pinMode (origPin, PWM_OUTPUT) ;	// Call myself to enable PWM mode
      pwmSetMode (PWM_MODE_MS) ;
    }
    else if (mode == PWM_OUTPUT)
    {
      if ((alt = gpioToPwmALT [pin]) == 0)	// Not a hardware capable PWM pin
	return ;

// Set pin to PWM mode

      *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) | (alt << shift) ;
      delayMicroseconds (110) ;		// See comments in pwmSetClockWPi

      pwmSetMode  (PWM_MODE_BAL) ;	// Pi default mode
      pwmSetRange (1024) ;		// Default range of 1024
      pwmSetClock (32) ;		// 19.2 / 32 = 600KHz - Also starts the PWM
    }
    else if (mode == GPIO_CLOCK)
    {
      if ((alt = gpioToGpClkALT0 [pin]) == 0)	// Not a GPIO_CLOCK pin
	return ;

// Set pin to GPIO_CLOCK mode and set the clock frequency to 100KHz

      *(gpio + fSel) = (*(gpio + fSel) & ~(7 << shift)) | (alt << shift) ;
      delayMicroseconds (110) ;
      gpioClockSet      (pin, 100000) ;
    }
  }
  else
  {
    if ((node = wiringPiFindNode (pin)) != NULL)
      node->pinMode (node, pin, mode) ;
    return ;
  }
 }
}


/*
 * pullUpDownCtrl:
 *	Control the internal pull-up/down resistors on a GPIO pin
 *	The Arduino only has pull-ups and these are enabled by writing 1
 *	to a port when in input mode - this paradigm doesn't quite apply
 *	here though.
 *********************************************************************************
 */

void pullUpDnControl (int pin, int pud)
{
  struct wiringPiNodeStruct *node = wiringPiNodes ;
if(version == BPVER)
{
  if ((pin & PI_GPIO_MASK) == 0)		// On-Board Pin
  {
    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_GPIO)
     pin=pinTobcm_BP[pin];//need map A20 to bcm
     else 
        return ;

     if (wiringPiDebug)
       printf ("%s,%d,pin:%d\n", __func__, __LINE__,pin) ;
				if (-1 == pin)
				{
					printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
					return;
				}
      pud &= 3 ;
      sunxi_pullUpDnControl(pin, pud);
	  return;
  }
  else						// Extension module
  {
    if ((node = wiringPiFindNode (pin)) != NULL)
      node->pullUpDnControl (node, pin, pud) ;
    return ;
  }
}
else
{
  if ((pin & PI_GPIO_MASK) == 0)    // On-Board Pin
  {
    /**/ if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode != WPI_MODE_GPIO)
      return ;

    *(gpio + GPPUD)              = pud & 3 ;    delayMicroseconds (5) ;
    *(gpio + gpioToPUDCLK [pin]) = 1 << (pin & 31) ;  delayMicroseconds (5) ;
    
    *(gpio + GPPUD)              = 0 ;      delayMicroseconds (5) ;
    *(gpio + gpioToPUDCLK [pin]) = 0 ;      delayMicroseconds (5) ;
  }
  else            // Extension module
  {
    if ((node = wiringPiFindNode (pin)) != NULL)
      node->pullUpDnControl (node, pin, pud) ;
    return ;
  }

}
}


/*
 * digitalRead:
 *	Read the value of a given Pin, returning HIGH or LOW
 *********************************************************************************
 */

int digitalRead (int pin)
{
  char c ;
  struct wiringPiNodeStruct *node = wiringPiNodes ;

  if(version== BPVER)
  {
    if ((pin & PI_GPIO_MASK) == 0)		// On-Board Pin
    {
      if (wiringPiMode == WPI_MODE_GPIO_SYS)	// Sys mode
      {
        if(pin==0)
        {
          printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
          return 0;
        }
        if(syspin[pin]==-1)
        {
          printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
          return 0;
        }
        if (sysFds [pin] == -1)
        {
          if (wiringPiDebug)
            printf ("pin %d sysFds -1.%s,%d\n", pin ,__func__, __LINE__) ;
          return LOW ;
        }
        if (wiringPiDebug)
          printf ("pin %d :%d.%s,%d\n", pin ,sysFds [pin],__func__, __LINE__) ;
        lseek  (sysFds [pin], 0L, SEEK_SET) ;
        read   (sysFds [pin], &c, 1) ;
        return (c == '0') ? LOW : HIGH ;
      }
      else if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio_BP [pin] ;
      else if (wiringPiMode == WPI_MODE_PHYS)
        pin = physToGpio_BP[pin] ;
      else if (wiringPiMode == WPI_MODE_GPIO)
        pin=pinTobcm_BP[pin];//need map A20 to bcm
      else 
        return LOW ;

      if(-1 == pin){
					printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
        return LOW;
      }
      return sunxi_digitalRead(pin);
    }
    else
    {
      if ((node = wiringPiFindNode (pin)) == NULL)
        return LOW ;
      return node->digitalRead (node, pin) ;
    }
  }
  else
  {
    if ((pin & PI_GPIO_MASK) == 0)		// On-Board Pin
    {
      /**/ if (wiringPiMode == WPI_MODE_GPIO_SYS)	// Sys mode
      {
        if (sysFds [pin] == -1)
          return LOW ;

        lseek (sysFds [pin], 0L, SEEK_SET) ;
        read  (sysFds [pin], &c, 1) ;
        return (c == '0') ? LOW : HIGH ;
      }
      else if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio [pin] ;
      else if (wiringPiMode == WPI_MODE_PHYS)
        pin = physToGpio [pin] ;
      else if (wiringPiMode != WPI_MODE_GPIO)
        return LOW ;

      if ((*(gpio + gpioToGPLEV [pin]) & (1 << (pin & 31))) != 0)
        return HIGH ;
      else
        return LOW ;
    }
    else
    {
      if ((node = wiringPiFindNode (pin)) == NULL)
        return LOW ;
      return node->digitalRead (node, pin) ;
    }
  }
}

/*
 * digitalWrite:
 *	Set an output bit
 *********************************************************************************
 */

void digitalWrite (int pin, int value)
{
  struct wiringPiNodeStruct *node = wiringPiNodes ;
if(version == BPVER)
{
  if ((pin & PI_GPIO_MASK) == 0)    // On-Board Pin
  {
    /**/ if (wiringPiMode == WPI_MODE_GPIO_SYS) // Sys mode
    {
       if (wiringPiDebug)
       {
          if(pin==0)
          {
            printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
            return;
          }
          if(syspin[pin]==-1)
          {
            printf("%d %s,%d invalid pin,please check it over.\n",pin,__func__, __LINE__);
            return;
          }
      }
        if (sysFds [pin] != -1)
        {
          if (wiringPiDebug)
          {
           printf ("pin %d sysFds -1.%s,%d\n", pin ,__func__, __LINE__) ;
           printf ("pin %d :%d.%s,%d\n", pin ,sysFds [pin],__func__, __LINE__) ;
           }
        if (value == LOW)
          write (sysFds [pin], "0\n", 2) ;
        else
          write (sysFds [pin], "1\n", 2) ;
          }
      return ;
    }
    else if (wiringPiMode == WPI_MODE_PINS)
      pin = pinToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_PHYS)
      pin = physToGpio [pin] ;
    else if (wiringPiMode == WPI_MODE_GPIO)
     pin=pinTobcm_BP[pin];//need map A20 to bcm
      else  return ;
   
         if(-1 == pin){
        printf("%d %s,%d %d invalid pin,please check it over.\n",pin,__func__, __LINE__,wiringPiMode);
        return ;
       }
    sunxi_digitalWrite(pin, value); 
  }
  else
  {
    if ((node = wiringPiFindNode (pin)) != NULL)
      node->digitalWrite (node, pin, value) ;
  }
}
else
  {
    if ((pin & PI_GPIO_MASK) == 0)   // On-Board Pin
    {
      /**/ if (wiringPiMode == WPI_MODE_GPIO_SYS) // Sys mode
      {
        if (sysFds [pin] != -1)
        {
    if (value == LOW)
      write (sysFds [pin], "0\n", 2) ;
    else
      write (sysFds [pin], "1\n", 2) ;
        }
        return ;
      }
      else if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio [pin] ;
      else if (wiringPiMode == WPI_MODE_PHYS)
        pin = physToGpio [pin] ;
      else if (wiringPiMode != WPI_MODE_GPIO)
        return ;

      if (value == LOW)
        *(gpio + gpioToGPCLR [pin]) = 1 << (pin & 31) ;
      else
        *(gpio + gpioToGPSET [pin]) = 1 << (pin & 31) ;
    }
    else
    {
      if ((node = wiringPiFindNode (pin)) != NULL)
        node->digitalWrite (node, pin, value) ;
    }
  }
}


/*
 * pwmWrite:
 *	Set an output PWM value
 *********************************************************************************
 */

void pwmWrite (int pin, int value)
{
  struct wiringPiNodeStruct *node = wiringPiNodes ;

  if(version == BPVER)
  {	
    uint32_t a_val = 0;
	int pwm_ch = 0;
	
    if (pin < MAX_PIN_NUM)  // On-Board Pin needto fix me Jim
    {
      if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio_BP [pin] ;
      else if (wiringPiMode == WPI_MODE_PHYS){
        pin = physToGpio_BP[pin] ;
    } else if (wiringPiMode == WPI_MODE_GPIO)
      pin=pinTobcm_BP[pin];//need map A20 to bcm
    else
      return ;

      if(-1 == pin){
	    printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
        return ;
      }
	
      if(pin < 233 || pin > 236){
        printf("the pin you choose is not surport hardware PWM\n");
        printf("you can select PH9-PH12 for PWM pin\n");
        printf("or you can use it in softPwm mode\n");
        return ;
      }

#if 0
  	  pwm_ch = sunxi_pwm_get_channel(pin);
  	
      if(pwmmode==1)//sycle
      {
        sunxi_pwm_set_mode(PWM_MODE_BAL, pwm_ch);
      }
      else
      {
        sunxi_pwm_set_mode(PWM_MODE_MS, pwm_ch);
      }
  	
      a_val = sunxi_pwm_get_period(pwm_ch);
	
      if (wiringPiDebug)
        printf("==> no:%d period now is :%d,act_val to be set:%d\n",__LINE__,a_val, value);
  	
      if(value > a_val){
        printf("val pwmWrite 0 <= X <= 1024\n");
        printf("Or you can set new range by yourself by pwmSetRange(range\n");
        return;
      }
  	
      //if value changed chang it
      sunxi_pwm_set_enable(0, pwm_ch);
      sunxi_pwm_set_act(value, pwm_ch);
      sunxi_pwm_set_enable(1, pwm_ch);
#else
	  sunxi_pwm_set_all(pin);
#endif
    } 
	else 
	{
	  printf("not on board :%s,%d\n", __func__, __LINE__) ;
      if ((node = wiringPiFindNode (pin)) != NULL)
	  {
        if (wiringPiDebug)
          printf ("Jim find node%s,%d\n", __func__, __LINE__) ;
        node->digitalWrite (node, pin, value) ;
      }
    }
    if (wiringPiDebug)
      printf ("this fun is ok now %s,%d\n", __func__, __LINE__) ;
	return;
  }
  else
  {
    if ((pin & PI_GPIO_MASK) == 0)		// On-Board Pin
    {
      /**/ if (wiringPiMode == WPI_MODE_PINS)
        pin = pinToGpio [pin] ;
      else if (wiringPiMode == WPI_MODE_PHYS)
        pin = physToGpio [pin] ;
      else if (wiringPiMode != WPI_MODE_GPIO)
        return ;

      *(pwm + gpioToPwmPort [pin]) = value ;
    }
    else
    {
      if ((node = wiringPiFindNode (pin)) != NULL)
        node->pwmWrite (node, pin, value) ;
    }
  }
}

/*
 * analogRead:
 *	Read the analog value of a given Pin. 
 *	There is no on-board Pi analog hardware,
 *	so this needs to go to a new node.
 *********************************************************************************
 */

int analogRead (int pin)
{
  struct wiringPiNodeStruct *node = wiringPiNodes ;

  if ((node = wiringPiFindNode (pin)) == NULL)
    return 0 ;
  else
    return node->analogRead (node, pin) ;
}


/*
 * analogWrite:
 *	Write the analog value to the given Pin. 
 *	There is no on-board Pi analog hardware,
 *	so this needs to go to a new node.
 *********************************************************************************
 */

void analogWrite (int pin, int value)
{
  struct wiringPiNodeStruct *node = wiringPiNodes ;

  if ((node = wiringPiFindNode (pin)) == NULL)
    return ;

  node->analogWrite (node, pin, value) ;
}


/*
 * pwmToneWrite:
 *	Pi Specific.
 *      Output the given frequency on the Pi's PWM pin
 *********************************************************************************
 */

void pwmToneWrite (int pin, int freq)
{
  int range ;

  if (freq == 0)
    pwmWrite (pin, 0) ;             // Off
  else
  {
    range = 600000 / freq ;
    pwmSetRange (range) ;
    pwmWrite    (pin, freq / 2) ;
  }
}



/*
 * digitalWriteByte:
 *	Pi Specific
 *	Write an 8-bit byte to the first 8 GPIO pins - try to do it as
 *	fast as possible.
 *	However it still needs 2 operations to set the bits, so any external
 *	hardware must not rely on seeing a change as there will be a change 
 *	to set the outputs bits to zero, then another change to set the 1's
 *********************************************************************************
 */

static int head2win[8]={11,12,13,15,16,18,22,7};
void digitalWriteByte (int value)
{
  uint32_t pinSet = 0 ;
  uint32_t pinClr = 0 ;
  int mask = 1 ;
  int pin ;
if(version == BPVER)
{
  /**/ if (wiringPiMode == WPI_MODE_GPIO_SYS||wiringPiMode == WPI_MODE_GPIO)
  {
    for (pin = 0 ; pin < 8 ; ++pin)
    {
       pinMode(pin,OUTPUT);
       delay(1);
       digitalWrite (pinToGpio [pin], value & mask) ;
       mask <<= 1 ;
    }
    return ;
  }
  else if(wiringPiMode == WPI_MODE_PINS)
  {
     for (pin = 0 ; pin < 8 ; ++pin)
     {      
       pinMode(pin,OUTPUT);
       delay(1);
      digitalWrite (pin, value & mask) ;
       mask <<= 1 ;
     }
   }
   else
     {
       for (pin = 0 ; pin < 8 ; ++pin)
         {
         pinMode(head2win[pin],OUTPUT);
         delay(1);
        digitalWrite (head2win[pin], value & mask) ;
         mask <<= 1 ;
       }
    }
   return ;
}
else
{
  /**/ if (wiringPiMode == WPI_MODE_GPIO_SYS)
  {
    for (pin = 0 ; pin < 8 ; ++pin)
    {
      digitalWrite (pin, value & mask) ;
      mask <<= 1 ;
    }
    return ;
  }
  else
  {
    for (pin = 0 ; pin < 8 ; ++pin)
    {
      if ((value & mask) == 0)
  pinClr |= (1 << pinToGpio [pin]) ;
      else
  pinSet |= (1 << pinToGpio [pin]) ;

      mask <<= 1 ;
    }

    *(gpio + gpioToGPCLR [0]) = pinClr ;
    *(gpio + gpioToGPSET [0]) = pinSet ;
  }
}
}


/*
 * waitForInterrupt:
 *	Pi Specific.
 *	Wait for Interrupt on a GPIO pin.
 *	This is actually done via the /sys/class/gpio interface regardless of
 *	the wiringPi access mode in-use. Maybe sometime it might get a better
 *	way for a bit more efficiency.
 *********************************************************************************
 */

int waitForInterrupt (int pin, int mS)
{
  int fd, x ;
  uint8_t c ;
  struct pollfd polls ;

  /**/ if (wiringPiMode == WPI_MODE_PINS)
    pin = pinToGpio [pin] ;
  else if (wiringPiMode == WPI_MODE_PHYS)
    pin = physToGpio [pin] ;

  if ((fd = sysFds [pin]) == -1)
    return -2 ;

// Setup poll structure

  polls.fd     = fd ;
  polls.events = POLLPRI ;	// Urgent data!

// Wait for it ...

  x = poll (&polls, 1, mS) ;

// Do a dummy read to clear the interrupt
//	A one character read appars to be enough.
//	Followed by a seek to reset it.

  (void)read (fd, &c, 1) ;
  lseek (fd, 0, SEEK_SET) ;

  return x ;
}


/*
 * interruptHandler:
 *	This is a thread and gets started to wait for the interrupt we're
 *	hoping to catch. It will call the user-function when the interrupt
 *	fires.
 *********************************************************************************
 */

static void *interruptHandler (void *arg)
{
  int myPin ;

  (void)piHiPri (55) ;	// Only effective if we run as root

  myPin   = pinPass ;
  pinPass = -1 ;

  for (;;)
    if (waitForInterrupt (myPin, -1) > 0)
      isrFunctions [myPin] () ;

  return NULL ;
}


/*
 * wiringPiISR:
 *	Pi Specific.
 *	Take the details and create an interrupt handler that will do a call-
 *	back to the user supplied function.
 *********************************************************************************
 */

int wiringPiISR (int pin, int mode, void (*function)(void))
{
  pthread_t threadId ;
  const char *modeS ;
  char fName   [64] ;
  char  pinS [8] ;
  pid_t pid ;
  int   count, i ;
  char  c ;
  int   bcmGpioPin ;

  if ((pin < 0) || (pin > 63))
    return wiringPiFailure (WPI_FATAL, "wiringPiISR: pin must be 0-63 (%d)\n", pin) ;

  /**/ if (wiringPiMode == WPI_MODE_UNINITIALISED)
    return wiringPiFailure (WPI_FATAL, "wiringPiISR: wiringPi has not been initialised. Unable to continue.\n") ;
  else if (wiringPiMode == WPI_MODE_PINS)
    bcmGpioPin = pinToGpio [pin] ;
  else if (wiringPiMode == WPI_MODE_PHYS)
    bcmGpioPin = physToGpio [pin] ;
  else
    bcmGpioPin = pin ;

	
	if(BPVER == version)
	{
		if(-1 == bcmGpioPin)  /**/
		{
			printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", __func__,  __LINE__, pin);
			return -1;
		}
		
		if(edge[bcmGpioPin]==-1)
		return wiringPiFailure (WPI_FATAL, "wiringPiISR: pin not sunpprt on bananaPi (%d,%d)\n", pin,bcmGpioPin) ;
	}
	
// Now export the pin and set the right edge
//	We're going to use the gpio program to do this, so it assumes
//	a full installation of wiringPi. It's a bit 'clunky', but it
//	is a way that will work when we're running in "Sys" mode, as
//	a non-root user. (without sudo)

  if (mode != INT_EDGE_SETUP)
  {
    /**/ if (mode == INT_EDGE_FALLING)
      modeS = "falling" ;
    else if (mode == INT_EDGE_RISING)
      modeS = "rising" ;
    else
      modeS = "both" ;

    sprintf (pinS, "%d", bcmGpioPin) ;

    if ((pid = fork ()) < 0)	// Fail
      return wiringPiFailure (WPI_FATAL, "wiringPiISR: fork failed: %s\n", strerror (errno)) ;

    if (pid == 0)	// Child, exec
    {
      /**/ if (access ("/usr/local/bin/gpio", X_OK) == 0)
      {
	execl ("/usr/local/bin/gpio", "gpio", "edge", pinS, modeS, (char *)NULL) ;
	return wiringPiFailure (WPI_FATAL, "wiringPiISR: execl failed: %s\n", strerror (errno)) ;
      }
      else if (access ("/usr/bin/gpio", X_OK) == 0)
      {
	execl ("/usr/bin/gpio", "gpio", "edge", pinS, modeS, (char *)NULL) ;
	return wiringPiFailure (WPI_FATAL, "wiringPiISR: execl failed: %s\n", strerror (errno)) ;
      }
      else
	return wiringPiFailure (WPI_FATAL, "wiringPiISR: Can't find gpio program\n") ;
    }
    else		// Parent, wait
      wait (NULL) ;
  }

// Now pre-open the /sys/class node - but it may already be open if
//	we are in Sys mode...

  if (sysFds [bcmGpioPin] == -1)
  {
    sprintf (fName, "/sys/class/gpio/gpio%d/value", bcmGpioPin) ;
    if ((sysFds [bcmGpioPin] = open (fName, O_RDWR)) < 0)
      return wiringPiFailure (WPI_FATAL, "wiringPiISR: unable to open %s: %s\n", fName, strerror (errno)) ;
  }

// Clear any initial pending interrupt

  ioctl (sysFds [bcmGpioPin], FIONREAD, &count) ;
  for (i = 0 ; i < count ; ++i)
    read (sysFds [bcmGpioPin], &c, 1) ;

  isrFunctions [pin] = function ;

  pthread_mutex_lock (&pinMutex) ;
    pinPass = pin ;
    pthread_create (&threadId, NULL, interruptHandler, NULL) ;
    while (pinPass != -1)
      delay (1) ;
  pthread_mutex_unlock (&pinMutex) ;

  return 0 ;
}


/*
 * initialiseEpoch:
 *	Initialise our start-of-time variable to be the current unix
 *	time in milliseconds and microseconds.
 *********************************************************************************
 */

static void initialiseEpoch (void)
{
  struct timeval tv ;

  gettimeofday (&tv, NULL) ;
  epochMilli = (uint64_t)tv.tv_sec * (uint64_t)1000    + (uint64_t)(tv.tv_usec / 1000) ;
  epochMicro = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)(tv.tv_usec) ;
}


/*
 * delay:
 *	Wait for some number of milliseconds
 *********************************************************************************
 */

void delay (unsigned int howLong)
{
  struct timespec sleeper, dummy ;

  sleeper.tv_sec  = (time_t)(howLong / 1000) ;
  sleeper.tv_nsec = (long)(howLong % 1000) * 1000000 ;

  nanosleep (&sleeper, &dummy) ;
}


/*
 * delayMicroseconds:
 *	This is somewhat intersting. It seems that on the Pi, a single call
 *	to nanosleep takes some 80 to 130 microseconds anyway, so while
 *	obeying the standards (may take longer), it's not always what we
 *	want!
 *
 *	So what I'll do now is if the delay is less than 100uS we'll do it
 *	in a hard loop, watching a built-in counter on the ARM chip. This is
 *	somewhat sub-optimal in that it uses 100% CPU, something not an issue
 *	in a microcontroller, but under a multi-tasking, multi-user OS, it's
 *	wastefull, however we've no real choice )-:
 *
 *      Plan B: It seems all might not be well with that plan, so changing it
 *      to use gettimeofday () and poll on that instead...
 *********************************************************************************
 */

void delayMicrosecondsHard (unsigned int howLong)
{
  struct timeval tNow, tLong, tEnd ;

  gettimeofday (&tNow, NULL) ;
  tLong.tv_sec  = howLong / 1000000 ;
  tLong.tv_usec = howLong % 1000000 ;
  timeradd (&tNow, &tLong, &tEnd) ;

  while (timercmp (&tNow, &tEnd, <))
    gettimeofday (&tNow, NULL) ;
}

void delayMicroseconds (unsigned int howLong)
{
  struct timespec sleeper ;
  unsigned int uSecs = howLong % 1000000 ;
  unsigned int wSecs = howLong / 1000000 ;

  /**/ if (howLong ==   0)
    return ;
  else if (howLong  < 100)
    delayMicrosecondsHard (howLong) ;
  else
  {
    sleeper.tv_sec  = wSecs ;
    sleeper.tv_nsec = (long)(uSecs * 1000L) ;
    nanosleep (&sleeper, NULL) ;
  }
}


/*
 * millis:
 *	Return a number of milliseconds as an unsigned int.
 *********************************************************************************
 */

unsigned int millis (void)
{
  struct timeval tv ;
  uint64_t now ;

  gettimeofday (&tv, NULL) ;
  now  = (uint64_t)tv.tv_sec * (uint64_t)1000 + (uint64_t)(tv.tv_usec / 1000) ;

  return (uint32_t)(now - epochMilli) ;
}


/*
 * micros:
 *	Return a number of microseconds as an unsigned int.
 *********************************************************************************
 */

unsigned int micros (void)
{
  struct timeval tv ;
  uint64_t now ;

  gettimeofday (&tv, NULL) ;
  now  = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec ;

  return (uint32_t)(now - epochMicro) ;
}

/*
 * wiringPiSetup:
 *	Must be called once at the start of your program execution.
 *
 * Default setup: Initialises the system into wiringPi Pin mode and uses the
 *	memory mapped hardware directly.
 *
 * Changed now to revert to "gpio" mode if we're running on a Compute Module.
 *********************************************************************************
 */

int wiringPiSetup (void)
{
  int   fd ;
  int   boardRev ;
  int   model, rev, mem, maker, overVolted ;

  if (getenv (ENV_DEBUG) != NULL)
    wiringPiDebug = TRUE ;

  if (getenv (ENV_CODES) != NULL)
    wiringPiReturnCodes = TRUE ;

  if (geteuid () != 0)
    (void)wiringPiFailure (WPI_FATAL, "wiringPiSetup: Must be root. (Did you forget sudo?)\n") ;

  if (wiringPiDebug)
    printf ("wiringPi: wiringPiSetup called\n") ;

  boardRev = piBoardRev () ;
 
 if(boardRev == BPVER)
 {
      pinToGpio =  pinToGpio_BP ;
      physToGpio = physToGpio_BP ;
 }
 else
 {
    /**/ if (boardRev == 1)	// A, B, Rev 1, 1.1
    {
       pinToGpio =  pinToGpioR1 ;
      physToGpio = physToGpioR1 ;
    }
    else 				// A, B, Rev 2, B+, CM, Pi2
    {
      if (piModel2)
        BCM2708_PERI_BASE = 0x3F000000 ;
       pinToGpio =  pinToGpioR2 ;
      physToGpio = physToGpioR2 ;
     }
}

// Open the master /dev/memory device

  if ((fd = open ("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC) ) < 0)
    return wiringPiFailure (WPI_ALMOST, "wiringPiSetup: Unable to open /dev/mem: %s\n", strerror (errno)) ;

  if(boardRev == BPVER)
  {
    // GPIO:
    /* for PM and PL */
    if(boardRev == BPVER)
	  gpio_lm = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_BASE_LM_BP);

	gpio = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_BASE_BP);
	if (((int32_t)gpio == -1) || ((int32_t)gpio == -1 ))
	  return wiringPiFailure (WPI_ALMOST,"wiringPiSetup: mmap (GPIO) failed: %s\n", strerror (errno)) ;

    // PWM
    pwm = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_PWM_BP) ;
   if ((int32_t)pwm == -1)
      return wiringPiFailure ("wiringPiSetup: mmap (PWM) failed: %s\n", strerror (errno)) ;
      
    // Clock control (needed for PWM)
    clk = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CLOCK_BASE_BP) ;
    if ((int32_t)clk == -1)      
	return wiringPiFailure ("wiringPiSetup: mmap (CLOCK) failed: %s\n", strerror (errno)) ;
      
    // The drive pads
    pads = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_PADS_BP) ;
    if ((int32_t)pads == -1)
     return wiringPiFailure ("wiringPiSetup: mmap (PADS) failed: %s\n", strerror (errno)) ;

#ifdef	USE_TIMER
// The system timer

    // The system timer
        timer = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_TIMER_BP) ;
        if ((int32_t)timer == -1)
          return wiringPiFailure ("wiringPiSetup: mmap (TIMER) failed: %s\n", strerror (errno)) ;
        // Set the timer to free-running, 1MHz.
        // 0xF9 is 249, the timer divide is base clock / (divide+1)
        // so base clock is 250MHz / 250 = 1MHz.
        *(timer + TIMER_CONTROL) = 0x0000280 ;
        *(timer + TIMER_PRE_DIV) = 0x00000F9 ;
        timerIrqRaw = timer + TIMER_IRQ_RAW ;
#endif
}
  else
  {
    // GPIO:
    gpio = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_BASE) ;
    if ((int32_t)gpio == -1)
      return wiringPiFailure ("wiringPiSetup: mmap (GPIO) failed: %s\n", strerror (errno)) ;

    // PWM
   pwm = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_PWM) ;
    if ((int32_t)pwm == -1)
     return wiringPiFailure ("wiringPiSetup: mmap (PWM) failed: %s\n", strerror (errno)) ;

    // Clock control (needed for PWM)
    clk = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CLOCK_BASE) ;
    if ((int32_t)clk == -1)
      return wiringPiFailure ("wiringPiSetup: mmap (CLOCK) failed: %s\n", strerror (errno)) ;

    // The drive pads
    pads = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_PADS) ;
    if ((int32_t)pads == -1)
      return wiringPiFailure ("wiringPiSetup: mmap (PADS) failed: %s\n", strerror (errno)) ;
 
#ifdef USE_TIMER
    // The system timer
    timer = (uint32_t *)mmap(0, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO_TIMER) ;
    if ((int32_t)timer == -1)
      return wiringPiFailure ("wiringPiSetup: mmap (TIMER) failed: %s\n", strerror (errno)) ;

    // Set the timer to free-running, 1MHz.
   // 0xF9 is 249, the timer divide is base clock / (divide+1)
    // so base clock is 250MHz / 250 = 1MHz.
    *(timer + TIMER_CONTROL) = 0x0000280 ;
    *(timer + TIMER_PRE_DIV) = 0x00000F9 ;
    timerIrqRaw = timer + TIMER_IRQ_RAW ;
#endif
  }
  initialiseEpoch () ;

// If we're running on a compute module, then wiringPi pin numbers don't really many anything...

  piBoardId (&model, &rev, &mem, &maker, &overVolted) ;
  if (model == PI_MODEL_CM)
    wiringPiMode = WPI_MODE_GPIO ;
  else
    wiringPiMode = WPI_MODE_PINS ;

  if (wiringPiDebug)
    printf ("%s: wiringPiMode = %d\n", __func__, wiringPiMode) ;

  return 0 ;
}


/*
 * wiringPiSetupGpio:
 *	Must be called once at the start of your program execution.
 *
 * GPIO setup: Initialises the system into GPIO Pin mode and uses the
 *	memory mapped hardware directly.
 *********************************************************************************
 */

int wiringPiSetupGpio (void)
{
  (void)wiringPiSetup () ;

  if (wiringPiDebug)
    printf ("wiringPi: wiringPiSetupGpio called\n") ;

  wiringPiMode = WPI_MODE_GPIO ;

  return 0 ;
}


/*
 * wiringPiSetupPhys:
 *	Must be called once at the start of your program execution.
 *
 * Phys setup: Initialises the system into Physical Pin mode and uses the
 *	memory mapped hardware directly.
 *********************************************************************************
 */

int wiringPiSetupPhys (void)
{
  (void)wiringPiSetup () ;

  if (wiringPiDebug)
    printf ("wiringPi: wiringPiSetupPhys called\n") ;

  wiringPiMode = WPI_MODE_PHYS ;

  return 0 ;
}


/*
 * wiringPiSetupSys:
 *	Must be called once at the start of your program execution.
 *
 * Initialisation (again), however this time we are using the /sys/class/gpio
 *	interface to the GPIO systems - slightly slower, but always usable as
 *	a non-root user, assuming the devices are already exported and setup correctly.
 */

int wiringPiSetupSys (void)
{
  int boardRev ;
  int pin ;
  char fName [128] ;

  if (getenv (ENV_DEBUG) != NULL)
    wiringPiDebug = TRUE ;

  if (getenv (ENV_CODES) != NULL)
    wiringPiReturnCodes = TRUE ;

  if (wiringPiDebug)
    printf ("wiringPi: wiringPiSetupSys called\n") ;

  boardRev = piBoardRev () ;
  if(boardRev == BPVER)
  {
    pinToGpio =  pinToGpio_BP ;
    physToGpio = physToGpio_BP ;
  }
  else
  {
    if (boardRev == 1)
    {
      pinToGpio =  pinToGpioR1 ;
      physToGpio = physToGpioR1 ;
    }
    else
    {
      pinToGpio =  pinToGpioR2 ;
      physToGpio = physToGpioR2 ;
    }
  }

// Open and scan the directory, looking for exported GPIOs, and pre-open
//	the 'value' interface to speed things up for later

  if(boardRev == BPVER)
  {
    for (pin = 1 ; pin < 32 ; ++pin)
    {
      sprintf (fName, "/sys/class/gpio/gpio%d/value", pin) ;
      sysFds [pin] = open (fName, O_RDWR) ;
    }
  }
  else
  {
    for (pin = 0 ; pin < 64 ; ++pin)
    {
      sprintf (fName, "/sys/class/gpio/gpio%d/value", pin) ;
      sysFds [pin] = open (fName, O_RDWR) ;
    }
  }

  initialiseEpoch () ;
  wiringPiMode = WPI_MODE_GPIO_SYS ;

  return 0 ;
}
