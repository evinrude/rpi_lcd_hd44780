KERNEL_SOURCE=~/Development/raspberry-pi/raspberrypi_linux

obj-m := lcd_hd44780.o lcd_extern.o

all:
	@$(MAKE) -C $(KERNEL_SOURCE) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-none-eabi- modules
	@gcc -Wall lcd_clock.c -o lcd_clock

clean:      
	@$(MAKE) -C $(KERNEL_SOURCE) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-none-eabi- clean
	@rm -vf lcd_clock
