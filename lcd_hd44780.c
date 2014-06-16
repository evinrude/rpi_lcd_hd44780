#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/time.h>

/** about this module **/
#define MOD_VERSION "v1.1 Baby Sasquatch"
#define MOD_NAME "lcd_hd44780"

/** macros **/
#define MAXBUF 83
#define TMBUF 9
#define DTBUF 11
#define NUM_PORTS 14
#define GPIO_START 0x20200000
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
#define DDRAM_SET 0x80
#define CHAR_DEVICE_NAME "lcd"

#define IOREAD32(OFFSET) ((unsigned long) ioread32((void *)(lcd_hd44780->gpio_base + OFFSET)))
#define IOADDRESS32(OFFSET) ((void *)(lcd_hd44780->gpio_base + OFFSET))
#define GPIO_ON(GPIO) iowrite32(1 << GPIO, IOADDRESS32(GPSET0_OFFSET))
#define GPIO_OFF(GPIO) iowrite32(1 << GPIO, IOADDRESS32(GPCLR0_OFFSET))
#define SET_GPIO(GPIO) if(GPIO/10 > 1) \
iowrite32(((IOREAD32(GPFSEL2_OFFSET) & ~(7 << ((GPIO % 10) * 3))) | (1 << ((GPIO % 10) * 3))), IOADDRESS32(GPFSEL2_OFFSET)); \
else \
iowrite32(((IOREAD32(GPFSEL1_OFFSET) & ~(7 << ((GPIO % 10) * 3))) | (1 << ((GPIO % 10) * 3))), IOADDRESS32(GPFSEL1_OFFSET));

#define LOGGER_INFO(fmt, args ...) printk( KERN_INFO "%s: [info]  %s(%d): " fmt, MOD_NAME,  __FUNCTION__, __LINE__, ## args)
#define LOGGER_ERR(fmt, args ...) printk( KERN_ERR "%s: [err]  %s(%d): " fmt, MOD_NAME, __FUNCTION__, __LINE__, ## args)
#define LOGGER_WARN(fmt, args ...) printk( KERN_ERR "%s: [warn]  %s(%d): " fmt, MOD_NAME, __FUNCTION__, __LINE__, ## args)
#define LOGGER_DEBUG(fmt, args ...) if (debug == 1) { printk( KERN_DEBUG "%s: [debug]  %s(%d): " fmt, MOD_NAME, __FUNCTION__, __LINE__, ## args); }

/** gmt data structure **/
struct gmt {
	char date_s[DTBUF];
	char time_s[TMBUF];
};

/** main data structure **/
struct lcdh {
	struct cdev *lcd_cdev;							/** character device  **/
	struct dentry *root_entry;						/** debugfs root entry **/
	unsigned long gpio_base;						/** virtual address to the io mapped memory at GPIO_START **/
	dev_t lcd_device_major;							/** dynamic major number **/
	dev_t lcd_device_number;						/** major and minor numbers combined **/
	char lcdbuffer[MAXBUF];							/** lcd message buffer **/
	u32 clear_before_write_message;					/** used as a boolean to indicate a clear before write **/
	u8 newline_seperator;							/** newline seperator **/
	struct semaphore sem;							/** semaphore used to guarantee atomic message writing to the lcd **/
};

/** prototypes **/
static ssize_t fops_lcdclear(struct inode *inodep, struct file *filep);
static ssize_t fops_lcdwritemessage(struct file *filep, const char __user *buff, size_t len, loff_t *off);
static ssize_t fops_lcdwritechar(struct file *filep, const char __user *buff, size_t len, loff_t *off);
static ssize_t fops_lcdwritecmd(struct file *filep, const char __user *buff, size_t len, loff_t *off);
void _toggle_en(void);
void _write_nibble(unsigned char nibble);
void _lcd_write(unsigned char c, int is_command);
static void lcd_clear(void);
static void lcd_write_message(char *message);
void _lcd_init(void);
int _lcd_setup(void);
int _cdev_setup(void);
int _lcdh_setup(void);
static void get_gmt(struct gmt *gmt);
int reboot_notify(struct notifier_block *nb, unsigned long action, void *data);
static int __init mod_init(void);
static void __exit mod_exit(void);

/** globals **/
static int debug = 0;
static struct lcdh *lcd_hd44780;
static struct file_operations lcdwritemessage_fops = {
	.owner = THIS_MODULE,
	.write = fops_lcdwritemessage,
};
static struct file_operations lcdclear_fops = {
	.owner = THIS_MODULE,
	.open = fops_lcdclear,
};
static struct file_operations lcdwritechar_fops = {
	.owner = THIS_MODULE,
	.write = fops_lcdwritechar,
};
static struct file_operations lcdwritecmd_fops = {
	.owner = THIS_MODULE,
	.write = fops_lcdwritecmd,
};
static struct notifier_block nb = {
	.notifier_call = reboot_notify,
};

/** module options **/
module_param(debug, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(debug, "Set the log level to debug");

static int fops_lcdclear(struct inode *inodep, struct file *filep)
{
	LOGGER_DEBUG("Received innvocation from user space\n");
	lcd_clear();
	return 0;
}

static ssize_t fops_lcdwritemessage(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	LOGGER_DEBUG("Received innvocation from user space\n");
	if(len > MAXBUF)
	{
		LOGGER_ERR("Unsupported string size [%d]. Must be less than [%d]\n", len, MAXBUF);
		return -EIO;
	}

	//grab the data from userspace
	memset(&lcd_hd44780->lcdbuffer, 0, MAXBUF);
	if(copy_from_user(lcd_hd44780->lcdbuffer, buff, len))
	{
		LOGGER_ERR("copy_from_user failed\n");
		return -EIO;
	}

	LOGGER_DEBUG("Copied char into the buffer [%c]\n", lcd_hd44780->lcdbuffer[0]);

	//leave a message
	lcd_write_message(lcd_hd44780->lcdbuffer);
	return len;
}

static ssize_t fops_lcdwritechar(struct file *filep, const char __user *buff, size_t len, loff_t *off)
{
	LOGGER_DEBUG("Received innvocation from user space\n");

	memset(&lcd_hd44780->lcdbuffer, 0, 1);
	if(copy_from_user(lcd_hd44780->lcdbuffer, buff, len))
	{
		LOGGER_ERR("copy_from_user failed\n");
		return -EIO;
	}

	LOGGER_DEBUG("Copied [%d] bytes into the buffer [%s]\n", len, lcd_hd44780->lcdbuffer);

	_lcd_write(lcd_hd44780->lcdbuffer[0], 0);
	return len;
}

static ssize_t fops_lcdwritecmd(struct file *filep, const char __user *buff, size_t len, loff_t *off)
{
	LOGGER_DEBUG("Received innvocation from user space\n");

	memset(&lcd_hd44780->lcdbuffer, 0, 1);
	if(copy_from_user(lcd_hd44780->lcdbuffer, buff, len))
	{
		LOGGER_ERR("copy_from_user failed\n");
		return -EIO;
	}

	LOGGER_DEBUG("Copied lcd command into the buffer [0x%X]\n", lcd_hd44780->lcdbuffer[0]);

	_lcd_write(lcd_hd44780->lcdbuffer[0], 1);
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

static void lcd_clear(void)
{
	LOGGER_DEBUG("Invocation\n");
	_lcd_write(0x01, 1);
	msleep(20);
}
EXPORT_SYMBOL(lcd_clear);

static void lcd_write_message(char *message)
{
	int i = 0;
	int lines = 0;	
	
	LOGGER_DEBUG("Invocation with message [%s]\n", message);
	
	//grab
	LOGGER_DEBUG("Attempting to down the semaphore\n");
	down(&lcd_hd44780->sem);
	LOGGER_DEBUG("Semaphore down\n");

	//do we need to clear the lcd first
	if(lcd_hd44780->clear_before_write_message) lcd_clear();

	for(i = 0; i < strlen(message); i++)
	{
		if(message[i] == lcd_hd44780->newline_seperator)
		{
			lines++;
			LOGGER_DEBUG("Newline detected and incremented to [%d]\n", lines);
			switch(lines)
			{
				case 1:
					LOGGER_DEBUG("Sending command to lcd [0x40]\n");
					_lcd_write((DDRAM_SET | 0x40), 1);
					break;				
				case 2:
					LOGGER_DEBUG("Sending command to lcd [0x14]\n");
					_lcd_write((DDRAM_SET | 0x14), 1);
					break;				
				case 3:
					LOGGER_DEBUG("Sending command to lcd [0x54]\n");
					_lcd_write((DDRAM_SET | 0x54), 1);
					break;
				default:
					LOGGER_WARN("Only 4 lines are supported. Cannot move to line [%d]. Omitting message from index [%i] on....\n", lines+1, i);
					goto cleanup;		
			}
		}
		else
		{
			LOGGER_DEBUG("Sending char to lcd [%c]\n", message[i]);
			_lcd_write(message[i], 0);
		}
	}

	cleanup:
	LOGGER_DEBUG("Performing cleanup\n");

	//Set cursor to home
	msleep(2);	
	_lcd_write(0x02, 1);
	msleep(2);

	//release
	LOGGER_DEBUG("Attempting to down the semaphore\n");
	up(&lcd_hd44780->sem);
	LOGGER_DEBUG("Semaphore up\n");
}
EXPORT_SYMBOL(lcd_write_message);

void _lcd_init(void)
{
	LOGGER_DEBUG("Invocation\n");

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
	LOGGER_DEBUG("Invocation\n");

	//debugfs dir
	if((lcd_hd44780->root_entry = debugfs_create_dir("hd44780-lcd", 0)) == 0)
	{
		LOGGER_ERR("Unable to create debugfs root directory\n");
		return -ENODEV;
	}

	//debugfs bool entry
	if(debugfs_create_bool("clear_before_write_message", 0660, lcd_hd44780->root_entry, &lcd_hd44780->clear_before_write_message) == 0)
	{
		LOGGER_ERR("Unable to create debugfs bool\n");
		return -ENODEV;
	}

	//debugfs newline seperator
	if(debugfs_create_u8("newline_seperator", 0660, lcd_hd44780->root_entry, &lcd_hd44780->newline_seperator) == 0)
	{
		LOGGER_ERR("Unable to create debugfs u8 newline entry\n");
		return -ENODEV;
	}
	
	//debugfs clear lcd
	if(debugfs_create_file("clear", 0000, lcd_hd44780->root_entry, &lcd_hd44780->lcdbuffer, &lcdclear_fops) == 0)
	{
		LOGGER_ERR("Unable to create debugfs file to clear lcd\n");
		return -ENODEV;
	}

	//debugfs send char
	if(debugfs_create_file("write_char", 0220, lcd_hd44780->root_entry, &lcd_hd44780->lcdbuffer, &lcdwritechar_fops) == 0)
	{
		LOGGER_ERR("Unable to create debugfs file to write char to lcd\n");
		return -ENODEV;
	}

	//debugfs send command
	if(debugfs_create_file("write_command", 0220, lcd_hd44780->root_entry, &lcd_hd44780->lcdbuffer, &lcdwritecmd_fops) == 0)
	{
		LOGGER_ERR("Unable to create debugfs file to write command to lcd\n");
		return -ENODEV;
	}
	//request the region of io mapped memory
	if(request_mem_region(GPIO_START, NUM_PORTS, "hd44780-lcd") == NULL)
	{
		LOGGER_ERR("Not able to map I/0\n");
		return -ENODEV;
	}

	//get the virtual address to the io mapped memory
	lcd_hd44780->gpio_base = (unsigned long) ioremap(GPIO_START, NUM_PORTS);

	// Setup all GPIO's needed for 4bit mode
	SET_GPIO(RS);
	SET_GPIO(EN);
	SET_GPIO(DB4);
	SET_GPIO(DB5);
	SET_GPIO(DB6);
	SET_GPIO(DB7);
	
	//initialize the semaphore for atomic write operations on the lcd
	sema_init(&lcd_hd44780->sem, 1);

	return 0;	
}

int _cdev_setup(void)
{
	int error = 0;

	LOGGER_DEBUG("Setting up the lcd module\n");
	
	//Dynamically assign major number
	if((error = alloc_chrdev_region(&lcd_hd44780->lcd_device_major, 0, 1, CHAR_DEVICE_NAME)) != 0)
	{
		LOGGER_ERR("Dynamic character device creation failed");
		return error;
	}

	//setup cdev
	lcd_hd44780->lcd_device_number = MKDEV(MAJOR(lcd_hd44780->lcd_device_major), MINOR((dev_t)0));	
	lcd_hd44780->lcd_cdev = cdev_alloc();
	lcd_hd44780->lcd_cdev->ops = &lcdwritemessage_fops;
	lcd_hd44780->lcd_cdev->owner = THIS_MODULE;

	if(cdev_add(lcd_hd44780->lcd_cdev, lcd_hd44780->lcd_device_number, 1))
	{
		LOGGER_ERR("Failed to add character device for lcd\n");
		return -ENODEV;
	}
	
	LOGGER_DEBUG("LCD Major [%d] MINOR [%d]\n", MAJOR(lcd_hd44780->lcd_device_number), MINOR(lcd_hd44780->lcd_device_number));		

	return 0;
}

int _lcdh_setup(void)
{
	LOGGER_DEBUG("Invocation\n");	

	if((lcd_hd44780 = kmalloc(sizeof(struct lcdh), GFP_KERNEL)) == 0)
	{
		LOGGER_ERR("Failed to kmalloc lcdh_t\n");
		return -ENOMEM;
	}

	lcd_hd44780->clear_before_write_message = 0;
	lcd_hd44780->newline_seperator = '+';

	return 0;
}

static void get_gmt(struct gmt *gmt)
{
	struct tm tm;
	struct timeval tv;

	LOGGER_DEBUG("Invocation\n");

	//get the time in GMT
	do_gettimeofday(&tv);
	time_to_tm(tv.tv_sec, 0, &tm);

	//create the date string
	sprintf(gmt->date_s,"%lu-%02d-%02d",
			(tm.tm_year + 1900),
			(tm.tm_mon + 1),
			tm.tm_mday);

	//create the time string
	sprintf(gmt->time_s,"%02d:%02d:%02d",
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec);
}
EXPORT_SYMBOL(get_gmt);

int reboot_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	char buf[MAXBUF];
	struct gmt t;
	
	LOGGER_DEBUG("Reboot notifier invoked with action [%lu] data [%p]\n", action, data);
	
	//get the date and time in GMT
	get_gmt(&t);
	
	//create a nice reboot message
	sprintf(buf,"  Powering Down!!++  %s+  %s GMT", t.date_s, t.time_s);

	//clear the screen
	lcd_clear();

	//display the message
	lcd_write_message(buf);

	return NOTIFY_DONE;
}

static int __init mod_init(void)
{
	char buf[MAXBUF];
	int error = 0;	

	LOGGER_INFO("%s\n", MOD_VERSION);

	//main data structure setup
	LOGGER_DEBUG("Setting up the main data structure\n");
	if((error = _lcdh_setup()) != 0) return error;

	//cdev setup
	LOGGER_DEBUG("Setting up cdev\n");
	if((error = _cdev_setup()) != 0) return error;

	//lcd setup
	LOGGER_DEBUG("Setting up lcd\n");
	if((error = _lcd_setup() != 0)) return error;
	
	//initialize the lcd
	LOGGER_DEBUG("Initializing the lcd\n");
	_lcd_init();

	//Put something on the screen
	sprintf(buf, "cdev[%d:%d]+RS:%d EN:%d+DB4:%d DB5:%d+DB6:%d DB7:%d", MAJOR(lcd_hd44780->lcd_device_number), MINOR(lcd_hd44780->lcd_device_number), RS, EN, DB4, DB5, DB6, DB7);
	LOGGER_DEBUG("Displaying the greeting message on the lcd [%s}\n", buf);
	lcd_write_message(buf);

	//notify this module about system state changes
	LOGGER_DEBUG("Registering reboot notifier\n");
	if(register_reboot_notifier(&nb)) LOGGER_ERR("Failed to register reboot notifier\n");

	//done
	LOGGER_DEBUG("Module initialization complete\n");
	return 0;
}

static void __exit mod_exit(void)
{
	LOGGER_INFO("Module Exit");

	//remove the debugfs created dirs and files
	LOGGER_DEBUG("Unregistering debugfs entries\n");
	debugfs_remove_recursive(lcd_hd44780->root_entry);

	//clear the display
	LOGGER_DEBUG("Clear the lcd\n");
	lcd_clear();

	//return the virtual address to the pool
	LOGGER_DEBUG("Retrurning virtual address to the pool\n");
	iounmap((void *)lcd_hd44780->gpio_base);
	
	//return the region of memory
	LOGGER_DEBUG("Releasing GPIO memory\n");
	release_mem_region(GPIO_START, NUM_PORTS);

	//unregister the character device
	LOGGER_DEBUG("Unregistering character device entry\n");
	cdev_del(lcd_hd44780->lcd_cdev);
	unregister_chrdev_region(lcd_hd44780->lcd_device_major, 1);

	//unregister the reboot notifier
	LOGGER_DEBUG("Unregistering reboot notifier\n");
	unregister_reboot_notifier(&nb);

	//free the main data structure
	LOGGER_DEBUG("Freeing the main data structure\n");
	kfree(lcd_hd44780);

	LOGGER_DEBUG("Module cleanup complete\n");
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mickey Malone");
MODULE_DESCRIPTION("Raspberry Pi HD44780 20x4 lcd controller");
