This adapter board connects an esp32 to a pi1541 board to run meatloaf. It should be easy to build and requires just a few components. The board must be powered by a 5V external power supply to power the pi1541. A wall wart  power supply could be used if it outputs 5V DC. Another way is to use a usb charger and split the cable to tap to the 5v and ground wires in it. Make sure it really outputs 5v by measuring it to prevent any damages. 

The esp32 can be powered by the same 5v or by its usb port when connected to a computer but NOT both at the same time. Powering the esp32 via its usb port is useful to flash it and monitor the logs while it is running. If the esp32 is powered via the usb of a computer, the switch must be open. If the esp32 usb is not used for a standalone appliance, you can close the switch to power the esp32 from the 5v rail. CLOSING THE SWITCH WHILE THE USB IS CONNECTED COULD DAMAGE YOUR COMPUTER.

The pi1541 board I used was based on the model github.com/hackup/Pi1541io rev 4 . If you have another model, verify the pin position to validate if you need to modify the schematic. Same thing applies if you use another esp32 chip; verify the GPIO numbers and pins for any customizations.

You need : 
1x esp32-wroom-32d or compatible
1x Pi1541 board
2x 1x19  female connecter for the esp32
1x 2x20 male header for the pi1541
1x switch
1x 5v power supply and connector if needed
1x printed circuit protoboard (double-sided) 

TODO
make use of the switch, oled and buzzer on the pi1541 board

Yannick Heneault
yheneaul@gmail.com
February 9th, 2023

