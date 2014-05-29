#include <linux/module.h> 
#include <linux/kernel.h>

extern void lcd_write_message(char *message);
extern void lcd_clear(void);

int init_module(void)
{
	lcd_clear();
	lcd_write_message("Hello From+Another Module");
	return 0;
}

void cleanup_module(void)
{
	return;
}  

MODULE_LICENSE("GPL");

