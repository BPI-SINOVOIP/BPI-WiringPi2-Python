import wiringpi2 as wiringpi
from time import sleep

WIRINGPI_DEBUG = 1

wiringpi.wiringPiSetupGpio()

wiringpi.pinMode(0,1)
wiringpi.digitalWrite(0,0)
sleep(1)
wiringpi.digitalWrite(0,1)
