# Connect all Nokia 5510 pin to BPi IO
import wiringpi2 as wiringpi

PIN_BACKLIGHT = 5 # LED
PIN_SCLK = 4 # Clock SCLK
PIN_SDIN = 3 # DN(MOSI)
PIN_DC = 2 # D/C
PIN_SCE = 1 # SCE
PIN_RESET = 0 # RST Reset


OUTPUT = 1
INPUT = 0
HIGH = 1
LOW = 0

LCD_C = 0
LCD_D = 1

LCD_X = 84
LCD_Y = 48
LCD_SEGS = 504

MSBFIRST = 1
LSBFIRST = 0

SLOW_DOWN = 400
FAST_DOWN = 0

wiringpi.wiringPiSetup()

def slow_shift_out(data_pin, clock_pin, data):
  for bit in bin(data).replace('0b','').rjust(8,'0'):
    wiringpi.digitalWrite(clock_pin,LOW)
    wiringpi.delay(FAST_DOWN)
    wiringpi.digitalWrite(data_pin,int(bit))
    wiringpi.delay(FAST_DOWN)
    wiringpi.digitalWrite(clock_pin,HIGH)
    wiringpi.delay(FAST_DOWN)

def lcd_write(dc, data):
  wiringpi.digitalWrite(PIN_DC, dc)
  wiringpi.digitalWrite(PIN_SCE, LOW)
  wiringpi.delay(FAST_DOWN)
  #wiringpi.shiftOut(PIN_SDIN, PIN_SCLK, MSBFIRST, data)
  slow_shift_out(PIN_SDIN, PIN_SCLK, data)
  wiringpi.digitalWrite(PIN_SCE, HIGH)
  wiringpi.delay(FAST_DOWN)
  #wiringpi.delay(2)

def lcd_initialise():
  wiringpi.pinMode(PIN_BACKLIGHT,OUTPUT)
  wiringpi.digitalWrite(PIN_BACKLIGHT, HIGH)
  wiringpi.pinMode(PIN_SCE, OUTPUT)
  wiringpi.pinMode(PIN_RESET, OUTPUT)
  wiringpi.pinMode(PIN_DC, OUTPUT)
  wiringpi.pinMode(PIN_SDIN, OUTPUT)
  wiringpi.pinMode(PIN_SCLK, OUTPUT)
  wiringpi.digitalWrite(PIN_RESET, LOW)
  wiringpi.delay(SLOW_DOWN)
  wiringpi.digitalWrite(PIN_RESET, HIGH)
  wiringpi.delay(SLOW_DOWN)
  lcd_write(LCD_C, 0x21 )  # LCD Extended Commands.
  lcd_write(LCD_C, 0x14 )  # LCD bias mode 1:48. //0x13
  lcd_write(LCD_C, 0xB2 )  # Set LCD Vop (Contrast = 0x32). 
  lcd_write(LCD_C, 0x20 )  # Normal mode
  lcd_write(LCD_C, 0x0C )  # LCD in normal mode.
  #lcd_write(LCD_C, 0x04 )  # Set Temp coefficent. //0x04
  #lcd_write(LCD_C, 0x0C )

def lcd_clear():
  for time in range(0, LCD_SEGS):
    lcd_write(LCD_D, 0x00)
#    if (time%100) == 0:
#      print "count=%d " %(time)


def lcd_fill():
  for time in range(0, LCD_SEGS):
    lcd_write(LCD_D, 0xFF)
#    if (time%100) == 0:
#      print "count=%d " %(time)

print "@@ LCD initial start @@"
lcd_initialise()

for time in range(0,4):
  print "## LCD clear. ##"
  lcd_clear()
  wiringpi.delay(1000)
  print "## LCD fill. ##"
  lcd_fill()
  wiringpi.delay(1000)
lcd_clear()
wiringpi.digitalWrite(PIN_BACKLIGHT, LOW)
