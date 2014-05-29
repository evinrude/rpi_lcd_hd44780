#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>


static volatile uint32_t *GPMAP;

static volatile uint32_t *GPFSEL0;
static volatile uint32_t *GPFSEL1;
static volatile uint32_t *GPFSEL2;
static volatile uint32_t *GPFSEL3;
static volatile uint32_t *GPFSEL4;
static volatile uint32_t *GPFSEL5;

static volatile uint32_t *GPSET0;
static volatile uint32_t *GPSET1;

static volatile uint32_t *GPCLR0;
static volatile uint32_t *GPCLR1;

static volatile uint32_t *GPLEV0;
static volatile uint32_t *GPLEV1;

void write_nibble(unsigned char nibble)
{

	/** Send the nibble 23=DB4 17=DB5 27=DB6 22=DB7 **/
	if (nibble & 0x01) *GPSET0 = *GPLEV0 | 1<<23; else *GPCLR0 = 1<<23;
	if (nibble & 0x02) *GPSET0 = *GPLEV0 | 1<<17; else *GPCLR0 = 1<<17;
	if (nibble & 0x04) *GPSET0 = *GPLEV0 | 1<<27; else *GPCLR0 = 1<<27;
	if (nibble & 0x08) *GPSET0 = *GPLEV0 | 1<<22; else *GPCLR0 = 1<<22;
}

void lcd_write(unsigned char c, int is_command)
{
	// set rs to low if is is a command
	if(is_command) *GPCLR0 = 1<<25; else *GPSET0 = *GPLEV0 | 1<<25;
	
	//delay
	usleep(40);

	//write the least signifigant nibble
	write_nibble((c >> 4) & 0x0F);
	
	//toggle en with delay
        *GPSET0 = *GPLEV0 | 1<<24;
        usleep(40);
        *GPCLR0 = 1<<24;
	usleep(40);

	//write the most signifigant nibble
	write_nibble(c & 0x0F);

	//toggle en with delay
	*GPSET0 = *GPLEV0 | 1<<24;
        usleep(40);
        *GPCLR0 = 1<<24;
        usleep(40);
}

void lcd_write_message(char *message)
{
	int i;
	for(i=0; i < strlen(message); i++)
	{
		if(message[i] == '\n') lcd_write(0xC0, 1); else lcd_write(message[i], 0);
	}

	//Set cursor to home	
	usleep(2000);
	lcd_write(0x02,1);
	usleep(2000);
}

void lcd_init()
{
	//delay
	usleep(250000);

	//set en low
	*GPCLR0 = 1<<24;

	//set rs low
	*GPCLR0 = 1<<25;

	//wake the interface
	write_nibble(0x03);

	//toggle en
	*GPSET0 = *GPLEV0 | 1<<24;
	*GPCLR0 = 1<<24;

	//delay
	usleep(150000);

	//toggle en
	*GPSET0 = *GPLEV0 | 1<<24;
        *GPCLR0 = 1<<24;

	//delay
	usleep(200);

	//toggle en
	*GPSET0 = *GPLEV0 | 1<<24;
        *GPCLR0 = 1<<24;
	
	//delay
	usleep(200);

	//set the interface to 4bits
	write_nibble(0x02);

	//delay
	usleep(40);

	//toggle en with delay
	*GPSET0 = *GPLEV0 | 1<<24;
	usleep(40);
        *GPCLR0 = 1<<24;

	//2 line 5x7 matrix
	lcd_write(0x28, 1);

	//enable display hide cursor
	//lcd_write(0x08, 1);

	//set cursor movement to right
	lcd_write(0x06, 1);

	//turn cursor off
	lcd_write(0x0C, 1);

	//clear the display
	lcd_write(0x01, 1);
	usleep(20000);
}

void lcd_setup()
{
	int fd ;
	if ((fd = open ("/dev/mem", O_RDWR | O_SYNC) ) < 0) {
		printf("Unable to open /dev/mem: %s\n", strerror(errno));
		exit(-1); 
	}

	GPMAP = (uint32_t *)mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x20200000);
	if (! GPMAP){
		printf("Mmap failed: %s\n", strerror(errno));
		exit(-1);
	}

	
	GPFSEL0 = GPMAP;
	GPFSEL1 = GPMAP + (0x00000004/4);
	GPFSEL2 = GPMAP + (0x00000008/4);
	GPFSEL3 = GPMAP + (0x0000000C/4);
	GPFSEL4 = GPMAP + (0x00000010/4);
	GPFSEL5 = GPMAP + (0x00000014/4);
	
	GPSET0 = GPMAP + (0x0000001C/4);
	GPSET1 = GPMAP + (0x00000020/4);

	GPCLR0 = GPMAP + (0x00000028/4);
	GPCLR1 = GPMAP + (0x0000002C/4);
	
	GPLEV0 = GPMAP + (0x00000034/4);
	GPLEV1 = GPMAP + (0x00000038/4);

	/** Set 25 as an OUTPUT**/
	*GPFSEL2 = ((*GPFSEL2 & ~(7 << 15)) | 1 << 15);

	/** Set 24 as an OUTPUT **/
	*GPFSEL2 = ((*GPFSEL2 & ~(7 << 12)) | 1 << 12);

	/** set 17 as an OUTPUT **/
	*GPFSEL1 = ((*GPFSEL1 & ~(7 << 21)) | 1 << 21);

	/** Set 22 as an OUTPUT **/
	*GPFSEL2 = ((*GPFSEL2 & ~(7 << 6)) | 1 << 6);

	/** Set 23 as and OUTPUT **/
	*GPFSEL2 = ((*GPFSEL2 & ~(7 << 9)) | 1 << 9);

	/** Set 27 as an OUTPUT **/
	*GPFSEL2 = ((*GPFSEL2 & ~(7 << 21)) | 1 << 21);	
}


int main(int argc, char **argv)
{
	time_t t;
	struct tm tm;
	char message[32];

	lcd_setup();
	lcd_init();
	while(1)
	{
		bzero(message, 32);
		t = time(NULL);
		tm = *localtime(&t);
		strftime(message, 32,"%m-%d-%Y\n%r", &tm);
		lcd_write_message(message);
		usleep(10000);
		//usleep(20000);
	}
}
