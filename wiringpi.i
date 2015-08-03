%module wiringpi2

%{
#include "WiringPi/wiringPi/wiringPi.h"
#include "WiringPi/wiringPi/wiringPiI2C.h"
#include "WiringPi/wiringPi/wiringPiSPI.h"
#include "WiringPi/wiringPi/wiringSerial.h"
#include "WiringPi/wiringPi/wiringShift.h"
#include "WiringPi/wiringPi/drcSerial.h"
#include "WiringPi/wiringPi/max31855.h"
#include "WiringPi/wiringPi/max5322.h"
#include "WiringPi/wiringPi/mcp23008.h"
#include "WiringPi/wiringPi/mcp23016.h"
#include "WiringPi/wiringPi/mcp23016reg.h"
#include "WiringPi/wiringPi/mcp23017.h"
#include "WiringPi/wiringPi/mcp23s08.h"
#include "WiringPi/wiringPi/mcp23s17.h"
#include "WiringPi/wiringPi/mcp23x0817.h"
#include "WiringPi/wiringPi/mcp23x08.h"
#include "WiringPi/wiringPi/mcp3002.h"
#include "WiringPi/wiringPi/mcp3004.h"
#include "WiringPi/wiringPi/mcp3422.h"
#include "WiringPi/wiringPi/mcp4802.h"
#include "WiringPi/wiringPi/pcf8574.h"
#include "WiringPi/wiringPi/pcf8591.h"
#include "WiringPi/wiringPi/sn3218.h"
#include "WiringPi/wiringPi/softPwm.h"
#include "WiringPi/wiringPi/softServo.h"
#include "WiringPi/wiringPi/softTone.h"
#include "WiringPi/wiringPi/sr595.h"
#include "WiringPi/devLib/ds1302.h"
#include "WiringPi/devLib/font.h"
#include "WiringPi/devLib/gertboard.h"
#include "WiringPi/devLib/lcd128x64.h"
#include "WiringPi/devLib/lcd.h"
#include "WiringPi/devLib/maxdetect.h"
#include "WiringPi/devLib/piFace.h"
#include "WiringPi/devLib/piGlow.h"
#include "WiringPi/devLib/piNes.h"
%}

%apply unsigned char { uint8_t };
%typemap(in) (unsigned char *data, int len) {
      $1 = (unsigned char *) PyString_AsString($input);
      $2 = PyString_Size($input);
};

extern int wiringPiFailure (int fatal, const char *message, ...) ;
extern struct wiringPiNodeStruct *wiringPiFindNode (int pin) ;
extern struct wiringPiNodeStruct *wiringPiNewNode  (int pinBase, int numPins) ;

// Core wiringPi functions
extern int  wiringPiSetup       (void) ;
extern int  wiringPiSetupSys    (void) ;
extern int  wiringPiSetupGpio   (void) ;
extern int  wiringPiSetupPhys   (void) ;

extern void pinModeAlt          (int pin, int mode) ;
extern void pinMode             (int pin, int mode) ;
extern void pullUpDnControl     (int pin, int pud) ;
extern int  digitalRead         (int pin) ;
extern void digitalWrite        (int pin, int value) ;
extern void pwmWrite            (int pin, int value) ;
extern int  analogRead          (int pin) ;
extern void analogWrite         (int pin, int value) ;

// On-Board Raspberry Pi hardware specific stuff
extern int  piBoardRev          (void) ;
extern void piBoardId           (int *model, int *rev, int *mem, int *maker, int *overVolted) ;
extern int  wpiPinToGpio        (int wpiPin) ;
extern int  physPinToGpio       (int physPin) ;
extern void setPadDrive         (int group, int value) ;
extern int  getAlt              (int pin) ;
extern void pwmToneWrite        (int pin, int freq) ;
extern void digitalWriteByte    (int value) ;
extern void pwmSetMode          (int mode) ;
extern void pwmSetRange         (unsigned int range) ;
extern void pwmSetClock         (int divisor) ;
extern void gpioClockSet        (int pin, int freq) ;

// Interrupts
extern int  waitForInterrupt    (int pin, int mS) ;
extern int  wiringPiISR         (int pin, int mode, void (*function)(void)) ;

// Threads
extern int  piThreadCreate      (void *(*fn)(void *)) ;
extern void piLock              (int key) ;
extern void piUnlock            (int key) ;

// Schedulling priority

extern int piHiPri (const int pri) ;

// Delays and Timing
extern void         delay             (unsigned int howLong) ;
extern void         delayMicroseconds (unsigned int howLong) ;
extern unsigned int millis            (void) ;
extern unsigned int micros            (void) ;

// ds1302
extern unsigned int ds1302rtcRead       (const int reg) ;
extern void         ds1302rtcWrite      (const int reg, const unsigned int data) ;
extern unsigned int ds1302ramRead       (const int addr) ;
extern void         ds1302ramWrite      (const int addr, const unsigned int data) ;
extern void         ds1302clockRead     (int clockData [8]) ;
extern void         ds1302clockWrite    (const int clockData [8]) ;
extern void         ds1302trickleCharge (const int diodes, const int resistors) ;
extern void         ds1302setup         (const int clockPin, const int dataPin, const int csPin) ;

// Gertboard
extern void gertboardAnalogWrite (const int chan, const int value) ;
extern int  gertboardAnalogRead  (const int chan) ;
extern int  gertboardSPISetup    (void) ;
extern int  gertboardAnalogSetup (const int pinBase) ;

// LCD 128x64
extern void lcd128x64setOrigin         (int x, int y) ;
extern void lcd128x64setOrientation    (int orientation) ;
extern void lcd128x64orientCoordinates (int *x, int *y) ;
extern void lcd128x64getScreenSize     (int *x, int *y) ;
extern void lcd128x64point             (int  x, int  y, int colour) ;
extern void lcd128x64line              (int x0, int y0, int x1, int y1, int colour) ;
extern void lcd128x64lineTo            (int  x, int  y, int colour) ;
extern void lcd128x64rectangle         (int x1, int y1, int x2, int y2, int colour, int filled) ;
extern void lcd128x64circle            (int  x, int  y, int  r, int colour, int filled) ;
extern void lcd128x64ellipse           (int cx, int cy, int xRadius, int yRadius, int colour, int filled) ;
extern void lcd128x64putchar           (int  x, int  y, int c, int bgCol, int fgCol) ;
extern void lcd128x64puts              (int  x, int  y, const char *str, int bgCol, int fgCol) ;
extern void lcd128x64update            (void) ;
extern void lcd128x64clear             (int colour) ;
extern int  lcd128x64setup             (void) ;

// LCD
extern void lcdHome        (const int fd) ;
extern void lcdClear       (const int fd) ;
extern void lcdDisplay     (const int fd, int state) ;
extern void lcdCursor      (const int fd, int state) ;
extern void lcdCursorBlink (const int fd, int state) ;
extern void lcdSendCommand (const int fd, unsigned char command) ;
extern void lcdPosition    (const int fd, int x, int y) ;
extern void lcdCharDef     (const int fd, int index, unsigned char data [8]) ;
extern void lcdPutchar     (const int fd, unsigned char data) ;
extern void lcdPuts        (const int fd, const char *string) ;
extern void lcdPrintf      (const int fd, const char *message, ...) ;
extern int  lcdInit (const int rows, const int cols, const int bits,
	const int rs, const int strb,
	const int d0, const int d1, const int d2, const int d3, const int d4,
	const int d5, const int d6, const int d7) ;

// PiFace
extern int  piFaceSetup (const int pinBase) ;

// PiGlow
extern void piGlow1     (const int leg,  const int ring, const int intensity) ;
extern void piGlowLeg   (const int leg,  const int intensity) ;
extern void piGlowRing  (const int ring, const int intensity) ;
extern void piGlowSetup (int clear) ;

// NES
extern int          setupNesJoystick (int dPin, int cPin, int lPin) ;
extern unsigned int  readNesJoystick (int joystick) ;

// Various IC setup functions
extern int sr595Setup (const int pinBase, const int numPins, const int dataPin, const int clockPin, const int latchPin) ;
extern int drcSetupSerial (const int pinBase, const int numPins, const char *device, const int baud) ;
extern int max31855Setup (int pinBase, int spiChannel) ;
extern int max5322Setup (int pinBase, int spiChannel) ;
extern int mcp23008Setup (const int pinBase, const int i2cAddress) ;
extern int mcp23016Setup (const int pinBase, const int i2cAddress) ;
extern int mcp23017Setup (const int pinBase, const int i2cAddress) ;
extern int mcp23s08Setup (const int pinBase, const int spiPort, const int devId) ;
extern int mcp23s17Setup (int pinBase, int spiPort, int devId) ;
extern int mcp3002Setup (int pinBase, int spiChannel) ;
extern int mcp3004Setup (int pinBase, int spiChannel) ;
extern int mcp3422Setup (int pinBase, int i2cAddress, int sampleRate, int gain) ;
extern int mcp4802Setup (int pinBase, int spiChannel) ;
extern int pcf8574Setup (const int pinBase, const int i2cAddress) ;
extern int pcf8591Setup (const int pinBase, const int i2cAddress) ;
extern int sn3218Setup (int pinBase) ;

// Soft PWM
extern int  softPwmCreate (int pin, int value, int range) ;
extern void softPwmWrite  (int pin, int value) ;
extern void softPwmStop   (int pin) ;

// Soft Servo
extern void softServoWrite  (int pin, int value) ;
extern int softServoSetup   (int p0, int p1, int p2, int p3, int p4, int p5, int p6, int p7) ;

// Soft Tone
extern int  softToneCreate (int pin) ;
extern void softToneStop   (int pin) ;
extern void softToneWrite  (int pin, int freq) ;

// SPI

%typemap(in) (unsigned char *data, int len) {
      $1 = (unsigned char *) PyString_AsString($input);
      $2 = PyString_Size($input);
};

%typemap(argout) (unsigned char *data) {
      $result = SWIG_Python_AppendOutput($result, PyString_FromStringAndSize((char *) $1, result));
};

int wiringPiSPIGetFd  (int channel) ;
int wiringPiSPIDataRW (int channel, unsigned char *data, int len) ;
int wiringPiSPISetup  (int channel, int speed) ;

// I2C
extern int wiringPiI2CRead           (int fd) ;
extern int wiringPiI2CReadReg8       (int fd, int reg) ;
extern int wiringPiI2CReadReg16      (int fd, int reg) ;
extern int wiringPiI2CWrite          (int fd, int data) ;
extern int wiringPiI2CWriteReg8      (int fd, int reg, int data) ;
extern int wiringPiI2CWriteReg16     (int fd, int reg, int data) ;
extern int wiringPiI2CSetupInterface (const char *device, int devId) ;
extern int wiringPiI2CSetup          (const int devId) ;

// WiringSerial
extern int   serialOpen      (const char *device, const int baud) ;
extern void  serialClose     (const int fd) ;
extern void  serialFlush     (const int fd) ;
extern void  serialPutchar   (const int fd, const unsigned char c) ;
extern void  serialPuts      (const int fd, const char *s) ;
extern void  serialPrintf    (const int fd, const char *message, ...) ;
extern int   serialDataAvail (const int fd) ;
extern int   serialGetchar   (const int fd) ;

// Shift Register
extern uint8_t shiftIn      (uint8_t dPin, uint8_t cPin, uint8_t order) ;
extern void    shiftOut     (uint8_t dPin, uint8_t cPin, uint8_t order, uint8_t val) ;

%include "wiringpi2-class.py"
