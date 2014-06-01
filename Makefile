KERNEL_SOURCE=~/Development/raspberry-pi/raspberrypi_linux

obj-m := lcd_hd44780.o

default: all

all:
	@$(MAKE) -C $(KERNEL_SOURCE) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-none-eabi- modules

clean:      
	@$(MAKE) -C $(KERNEL_SOURCE) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-none-eabi- clean
