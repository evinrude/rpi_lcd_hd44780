#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>

/** convenience macros **/
#define NUM_PORTS 14
#define GPFSEL0_OFFSET 0x00000000
#define GPFSEL1_OFFSET 0x00000004
#define GPFSEL2_OFFSET 0x00000008
#define GPFSEL3_OFFSET 0x0000000C
#define GPFSEL4_OFFSET 0x00000010
#define GPFSEL5_OFFSET 0x00000014
#define GPSET0_OFFSET 0x0000001C
#define GPSET1_OFFSET 0x00000020
#define GPCLR0_OFFSET 0x00000028
#define GPCLR1_OFFSET 0x0000002C
#define GPLEV0_OFFSET 0x00000034
#define GPLEV1_OFFSET 0x00000038
#define RS 25
#define EN 24
#define DB4 23
#define DB5 17
#define DB6 27
#define DB7 22

#define IOREAD32(OFFSET) ((unsigned long) ioread32((void *)(GPIO_BASE + OFFSET)))
#define IOADDRESS32(OFFSET) ((void *)(GPIO_BASE + OFFSET))
#define GPIO_ON(GPIO) iowrite32(1 << GPIO, IOADDRESS32(GPSET0_OFFSET))
#define GPIO_OFF(GPIO) iowrite32(1 << GPIO, IOADDRESS32(GPCLR0_OFFSET))

/** prototypes **/
static ssize_t fops_lcdclear(struct inode *inodep, struct file *filep);
static int fops_lcdwrite(struct file *filep, const char __user *buff, size_t len, loff_t *off);
void _toggle_en(void);
void _write_nibble(unsigned char nibble);
void _lcd_write(unsigned char c, int is_command);
void lcd_clear(void);
void lcd_write_message(char *message);
void _lcd_init(void);
int _lcd_setup(void);
static int __init mod_init(void);
static void __exit mod_exit(void);
static unsigned long GPIO = 0x20200000;
static unsigned long GPIO_BASE = 0;
static struct dentry *root_entry;

/** globals **/
#ifdef lcd_20x4
static char lcdbuffer[83];
#else
static char lcdbuffer[33];
#endif
static u32 clear_before_write_message = 0;
static u8 newline_seperator = '+';
struct semaphore sem;
struct file_operations lcdwrite_fops = {
	.owner = THIS_MODULE,
	.write = fops_lcdwrite,
};
struct file_operations lcdclear_fops = {
	.owner = THIS_MODULE,
	.open = fops_lcdclear,
};

static int fops_lcdclear(struct inode *inodep, struct file *filep)
{
	lcd_clear();
	return 0;
}

static ssize_t fops_lcdwrite(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	if(len > 33)
	{
#ifdef lcd_20x4
		continue
#else
		printk(KERN_ERR "Unsupported buffer size\n");
		return -EIO;
#endif
	}

	//grab the data from userspace
#ifdef lcd_20x4
	memset(&lcdbuffer, 0, 83);
#else
	memset(&lcdbuffer, 0, 33);
#endif
	if(copy_from_user(&lcdbuffer, buff, len))
	{
		printk(KERN_ERR "copy_from_user failed\n");
		return -EIO;
	}

	//leave a message
	lcd_write_message(lcdbuffer);
	return len;
}

void _toggle_en(void)
{
	GPIO_ON(EN);
	usleep_range(40, 50);
	GPIO_OFF(EN); 
	usleep_range(40, 50);
}

void _write_nibble(unsigned char nibble)
{
	/** Send the nibble 23=DB4 17=DB5 27=DB6 22=DB7 **/
	if(nibble & 0x01) GPIO_ON(DB4); else GPIO_OFF(DB4);
	if(nibble & 0x02) GPIO_ON(DB5); else GPIO_OFF(DB5);
	if(nibble & 0x04) GPIO_ON(DB6); else GPIO_OFF(DB6);
	if(nibble & 0x08) GPIO_ON(DB7); else GPIO_OFF(DB7);
}

void _lcd_write(unsigned char c, int is_command)
{
	// set rs to low if it is a command
	if(is_command) GPIO_OFF(RS); else GPIO_ON(RS);

	//delay
	usleep_range(40, 50);

	//write the least signifigant nibble
	_write_nibble((c >> 4) & 0x0F);

	//toggle en
	_toggle_en();

	//write the most signifigant nibble
	_write_nibble(c & 0x0F);

	//toggle en
	_toggle_en();
}

void lcd_clear(void)
{
	_lcd_write(0x01, 1);
	msleep(20);
}
EXPORT_SYMBOL(lcd_clear);

void lcd_write_message(char *message)
{
	int i = 0;	
	
	//grab
	down(&sem);

	//do we need to clear the lcd first
	if(clear_before_write_message) lcd_clear();

	for(i = 0; i < strlen(message); i++)
	{
		if(message[i] == newline_seperator) _lcd_write(0xC0, 1); else _lcd_write(message[i], 0);
	}

	//Set cursor to home
	msleep(2);	
	_lcd_write(0x02,1);
	msleep(2);

	//release
	up(&sem);
}
EXPORT_SYMBOL(lcd_write_message);

void _lcd_init(void)
{
	//delay
	msleep(250);

	//set en low
	GPIO_OFF(24);

	//set rs low
	GPIO_OFF(25);

	//wake the interface
	_write_nibble(0x03);

	//toggle en
	_toggle_en();

	//delay
	usleep_range(200, 250);

	//toggle en
	_toggle_en();
	
	//delay
	usleep_range(200, 250);

	//set the interface to 4bits
	_write_nibble(0x02);

	//delay
	usleep_range(40, 50);

	//toggle en
	_toggle_en();

	//2 line 5x7 matrix
	_lcd_write(0x28, 1);

	//set cursor movement to right
	_lcd_write(0x06, 1);

	//turn cursor off
	_lcd_write(0x0C, 1);

	//clear the display
	lcd_clear();

	//allow the device to catch up
	msleep(20);
}

int _lcd_setup(void)
{
	struct dentry *file_entry;
	struct dentry *bool_entry;
	struct dentry *newline_entry;
	struct dentry *clear_entry;
	
	//debugfs dir
	root_entry = debugfs_create_dir("hd44780-lcd", 0);
	if(root_entry == 0)
	{
		printk(KERN_ERR "Unable to create debugfs root directory\n");
		return -1;
	}

	//debugfs file write buffer
	file_entry = debugfs_create_file("write_buffer", 0220, root_entry, &lcdbuffer, &lcdwrite_fops);
	if(file_entry == 0)
	{
		printk(KERN_ERR "Unable to create debugfs file\n");
		return -1;
	}

	//debugfs bool entry
	bool_entry = debugfs_create_bool("clear_before_write_message", 0660, root_entry, &clear_before_write_message);
	if(bool_entry == 0)
	{
		printk(KERN_ERR "Unable to create debugfs bool\n");
		return -1;
	}

	//debugfs newline seperator
	newline_entry = debugfs_create_u8("newline_seperator", 0660, root_entry, &newline_seperator);
    if(newline_entry == 0)
    {
        printk(KERN_ERR "Unable to create debugfs u8 newline entry\n");
        return -1;
    }
	
	//debugfs clear lcd
	clear_entry = debugfs_create_file("clear", 0000, root_entry, &lcdbuffer, &lcdclear_fops);
	if(clear_entry == 0)
	{
		printk(KERN_ERR "Unable to create debugfs file to clear lcd\n");
		return -1;
	}

	//request the region of io mapped memory
	if(request_mem_region(GPIO, NUM_PORTS, "hd44780-lcd") == NULL)
	{
		printk("Not able to map I/0\n");
		return -1;
	}

	//get the virtual address to the io mapped memory
	GPIO_BASE = (unsigned long) ioremap(GPIO, NUM_PORTS);

	// make gpio #25 an output
	iowrite32((IOREAD32(GPFSEL2_OFFSET) & ~(7 << 15)) | (1 << 15), IOADDRESS32(GPFSEL2_OFFSET));
	
	// make gpio #24 an output
	iowrite32((IOREAD32(GPFSEL2_OFFSET) & ~(7 << 12)) | (1 << 12), IOADDRESS32(GPFSEL2_OFFSET));
	
	// make gpio #17 an output
	iowrite32((IOREAD32(GPFSEL1_OFFSET) & ~(7 << 21)) | (1 << 21), IOADDRESS32(GPFSEL1_OFFSET));

	// make gpio #22 an output
	iowrite32((IOREAD32(GPFSEL2_OFFSET) & ~(7 << 6)) | (1 << 6), IOADDRESS32(GPFSEL2_OFFSET));

	// make gpio #23 an output
	iowrite32((IOREAD32(GPFSEL2_OFFSET) & ~(7 << 9)) | (1 << 9), IOADDRESS32(GPFSEL2_OFFSET));

	// make gpio #23 an output
	iowrite32((IOREAD32(GPFSEL2_OFFSET) & ~(7 << 21)) | (1 << 21), IOADDRESS32(GPFSEL2_OFFSET));

	//initialize the semaphore for atomic write operations on the lcd
	sema_init(&sem, 1);

	return 0;	
}

static int __init mod_init(void)
{
	//setup
	if(_lcd_setup() != 0) return -1;
	
	//initialize the lcd
	_lcd_init();

	//Put something on the screen
	lcd_write_message("  Hello From+  Kernel Space!");	

	//done
	return 0;
}

static void __exit mod_exit(void)
{
	//remove the debugfs created dirs and files
	debugfs_remove_recursive(root_entry);

	//clear the display
	lcd_clear();

	//return the virtual address to the pool
	iounmap((void *)GPIO_BASE);
	
	//return the region of memory
	release_mem_region(GPIO, NUM_PORTS);
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mickey Malone");
