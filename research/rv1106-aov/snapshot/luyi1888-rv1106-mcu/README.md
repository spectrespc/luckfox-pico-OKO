# rv1106-mcu
 Tools and notes for Rockchip RISC-V MCU.

This repository contains the tool which I made for Rockchip RV1106 MCU.

The MCU is based on Syntacore SCR1.
The same RISC-V core exists in RK3568/RV1126/RV1106.

I have tested it on the luckfox pico pro(RV1106).

# JTAG
https://github.com/LuckfoxTECH/luckfox-pico/issues/112


# Demo firmware
This firmware will control the Luckfox Pico onboard led blinking at 1hz and output log to UART2(Default UART).

Step:

1.Disable LED control by linux.

`echo none > /sys/class/leds/work/trigger`

2.Run MCU firmware

`mcuload execute 0xff6c0000 rv1106-mcu-led-blinky.bin`
