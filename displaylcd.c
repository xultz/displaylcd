/*
 * 
 *      Copyright (C) 2017 Christian Schultz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Schultz");
MODULE_DESCRIPTION("A LKM to use a 16x2 Alphanumeric Display with the Raspberry Pi");
MODULE_VERSION("1.0");

// Here, the pin names are defined to his respective pin number.
// To change the circuit connection, just change the defines below according to the schematic
#define RS_pin	10
#define EN_pin	9
#define DB4_pin	6
#define DB5_pin	13
#define DB6_pin	19
#define DB7_pin	26

// This structure defines an gpio array. The GPIO pin number is defined in a series of #define's just above
static struct gpio pins[] = {
	{ RS_pin,  GPIOF_OUT_INIT_LOW, "LCD RS pin"  },
	{ EN_pin,  GPIOF_OUT_INIT_LOW, "LCD EN pin"  },
	{ DB4_pin, GPIOF_OUT_INIT_LOW, "LCD DB4 pin" },
	{ DB5_pin, GPIOF_OUT_INIT_LOW, "LCD DB5 pin" },
	{ DB6_pin, GPIOF_OUT_INIT_LOW, "LCD DB6 pin" },
	{ DB7_pin, GPIOF_OUT_INIT_LOW, "LCD DB7 pin" }
};                                            

// The defines below are used to access the gpio array member, declared above
#define RS 0
#define EN 1
#define DB4 2
#define DB5 3
#define DB6 4
#define DB7 5

// This global variables are used as placeholders, if no parameters are passed to the module when loading it
static char * line1 = " Raspberry Pi 3 ";
static char * line2 = "  LCD  Display  "; 

// Declare the module parameters settings
module_param(line1, charp, 0000);
module_param(line2, charp, 0000);
MODULE_PARM_DESC(line1, "The characters to be displayed in the first (upper) line of the LCD Display (max number of chars: 16)");
MODULE_PARM_DESC(line2, "The characters to be displayed in the second (lower) line of the LCD Display (max number of chars: 16)");

// Function prototypes
void lcd_nibble(unsigned char);		// Writes a single nibble to the LCD
void lcd_byte(unsigned char);		// Writes a byte to LCD, call lcd_nibble twice
void lcd_cls(void);					// Clear the LCD screen and position the cursor in the first position
void lcd_pos(unsigned char);		// Position the cursor in the display, starting from 1 (first position in the first line) to 32 (last position in the second line)
void lcd_print(unsigned char *);	// Prints a string in the display, calling lcd_byte for every character in the array
// Methods prototypes
static int device_open(struct inode *, struct file *);						// This method is called when a program open the device file
static int device_release(struct inode *, struct file *);					// Called when the program closes the device file
static ssize_t device_read(struct file *, char *, size_t, loff_t *);		// Called when the program that opened the device file reads it
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);	// Called when the program that opened the device file writes to it
static char * classmode(struct device *, umode_t *);

// More global variables
static int Device_Open = 0;				// This flag is used to prevent multiple device driver file opening
static struct class * devclass = NULL;	// This structure will hold the device driver class
static struct device * dev = NULL;		// This structur will hole the device driver that will be created
static int major;						// Here I will keep the major number assigned by the kernel
static int minor;						// When the device is opened, I will store the minor number here
static char message[32];				// I will copy messages sent to the driver in this buffer

// Declare the methods to be called when such action happens
static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

// This method is called when the program issues an open command. It is important to have only one program that can open the device file at a time (I think).
// So, a flag is marked whenever the file is opened, so if there's another try to open it, the opening is recused by returning an error code.
static int device_open(struct inode * inode, struct file * file)
{
	if(Device_Open)		// If this flag is marked, I will not do anything and return with an error code
		return -EBUSY;
		
	Device_Open = 1;	// Mark the flag to show that the device is opened by someone
	
	// This command increments the use counter. If it is not zero, rmmod will not allow the module to be removed
	// The counter is decremented on device_release method
	try_module_get(THIS_MODULE);

	minor = MINOR(inode->i_rdev);	// Store the minor number used to open the device

	return 0;
}

// This method is called when the program closes the device driver file. The flag that shows that the file is opened is cleared, so another program 
// can try to open the driver.
static int device_release(struct inode * inode, struct file * file)
{
	Device_Open = 0;	// Clear the flag to allow the device driver file to be opened by another program

	// This command decrements the use counter, so if it is zero, rmmod can remove the module (if needed)
	module_put(THIS_MODULE);

	return 0;
}

static ssize_t device_read(struct file * filp, char * buffer, size_t length, loff_t * offset)
{
	return 0;
}

static ssize_t device_write(struct file * filp, const char * buffer, size_t len, loff_t * offset)
{
	unsigned char pos = 0;

	if(len > 30)	// I will check if the message is bigger than the buffer, if so I will ignore it and pintk an warning message (it only makes sense receiving 17 characters at max)
	{
		printk(KERN_INFO "LCD Display Driver: Message too long (ignored) with %zu chars\n", len);
		return len;
	}

	// opening (and writing) to /dev/displaylcd_cls gives a minor number of 1
	if(minor == 1)
	{
		lcd_cls();
		return len;
	}

	// opening (and writing) to /dev/displaylcd_pos gives a minor number of 2
	if(minor == 2)
	{
		if(len == 0)	// If the user didn't sent any character, there is nothing I can do
			return len;

		if(len == 1)	// If the user sent only 1 character, I will (try) to use it to calculate the value
		{
			if((buffer[0] >= '0') && (buffer[0] <= '9'))	// If the value passed is a number, I will use to calculate the position and execute the command, otherwise I will do nothing
			{
				pos = buffer[0] - '0';
				lcd_pos(pos);
			}
			return len;
		}
		else	// If the message is bigger than 1 character, I will (try) to use the first two characters to calculate the position
		{
			pos = 0;
			if((buffer[0] >= '0') && (buffer[0] <= '9'))
				pos = buffer[0] - '0';

			if((buffer[1] >= '0') && (buffer[1] <= '9'))
			{
				pos = pos * 10;
				pos = pos + buffer[1] - '0';
			}

			if((pos != 0) && (pos <= 32))
				lcd_pos(pos);

			return len;
		}
	}

	// opening (and writing) to /dev/displaylcd gives a minor number of 0
	if(minor == 0)
	{
		// The echo -n do not put an end of string in the buffer, so I will copy the bytes to the message buffer,
		// and put a \0 after it. If the program that is sending the characters puts the \0 on the string, another \0 will be inserted, which is redunctant but harmless
		memcpy(message, buffer, len);
		message[len] = 0;

		lcd_print(message);

		return len;
	}

	return len;
}


// This function writes a single nibble to the display, by looking at the 4 least significant bits of "nibble"
// The least significatn bit is written to the pin DB4, the next to DB5, and so on.
void lcd_nibble(unsigned char nibble)
{
	// Before entering this function, the RS pin must be set or clear from the calling function, signaling
	// if the next write is for a character or a command. This function does no change the RS pin state
	gpio_set_value(pins[EN].gpio, 1);	// Put the EN pin in high logic level, as defined on the HD44780 datasheet (page 58, figure 25)
	
	// Ensures a minumum delay of 150ns before setting the data pins, according to the datasheet. The measured time of GPIO change on a Raspberry Pi 3 is 500ns, so probably this delay is not important
	ndelay(150);
	
	// Check every bit from the nibble, and set or clear the data pin accordingly
	nibble & 0x01 ? gpio_set_value(pins[DB4].gpio, 1) : gpio_set_value(pins[DB4].gpio, 0);
	nibble & 0x02 ? gpio_set_value(pins[DB5].gpio, 1) : gpio_set_value(pins[DB5].gpio, 0);
	nibble & 0x04 ? gpio_set_value(pins[DB6].gpio, 1) : gpio_set_value(pins[DB6].gpio, 0);
	nibble & 0x08 ? gpio_set_value(pins[DB7].gpio, 1) : gpio_set_value(pins[DB7].gpio, 0);
	
	ndelay(80);
	
	gpio_set_value(pins[EN].gpio, 0);	// By changing the EN pin to low state, effectively writes the data present in the data lines to the display
	
	ndelay(10);	
}

// This function writes one byte to the display, by calling lcd_nibble twice
// After the execution of this function, the RS pin will be set to high state, so the next byte written will be a character, not a command
// (unless the RS pin is cleared before calling this function again)
void lcd_byte(unsigned char byte)
{
	// According to the HD44780 datasheet (page 22), the most significant nibble must be written first, and then the least significant nibble next.
	lcd_nibble(byte >> 4);	// I do a 4 bits rotate, so the most significant nibble moves to the 4 least significant bits, which are used by the lcd_nibble function
	lcd_nibble(byte);		// I don't do anything to "byte" because the least significant niblle is in place, and the lcd_nibble ignores the most significant nibble
	
	// According to the HD44780 datasheet (page 24, table 6) all the commands execution time is 37us (with the exception of the Clear Display, which needs 1.52ms)
	// So, I give a 40us delay to give enough time to execute any command. The Clear Display function must ensure the required delay after calling this function
	udelay(40);				
	
	// Here, the RS pin is set, meaning that the next write to the display will be a character. This is done this way because most of the bytes written to the
	// display are characters, not commands. When the program needs to write a command to the LCD, it must clear the RS pin before calling this function
	gpio_set_value(pins[RS].gpio, 1);
}

// This function sends the Clear Display (code 0x01) to the display, which clears the entire display and put the cursor in the first position
// After calling this function, the RS pin will be set high, so the following byte writeen to the display will be threated as characters
void lcd_cls(void)
{
	gpio_set_value(pins[RS].gpio, 0);	// I must put the RS pin low, because it is (probably) in high state, and the next byte is a command
	lcd_byte(0x01);						// Sends the Clear Display command
	mdelay(2);							// I give here 2ms to the command be executed by the display, which is more than enough (the datasheet specifies 1.52ms)
}

// This function position the cursor in the display, according to the table below:
// ---------------------------------------------------------------------------------
// |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  8 |  9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
// ---------------------------------------------------------------------------------
// | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 | 28 | 29 | 30 | 31 | 32 |
// ---------------------------------------------------------------------------------
void lcd_pos(unsigned char pos)
{
	pos--;	// The first position in the display memory is 0, but I decided that the first position is 1 because... because. So I decrement it.
	
	// Check if the position belongs to the second line (if it was passed a value of 17 or greater, after decrementing it, it is 16 or greater)
	// According to the HD44780 datasheet (page 12, figure 6), the first position on the second line is 0x40 (64). So, if pos here is 16 (meaning the first position on the second line)
	// I must set the cursor to memory position 64; if it is 17, to 65, and so on. This way, by adding 48 to the pos value, gives the needed value
	if(pos > 15)
		pos = pos + 48;
		
	// The command to set the cursor position is 1AAA.AAAA where A is the position value (in binary). So, I set the most significant bit to make the command value
	pos = pos | 0x80;
	
	gpio_set_value(pins[RS].gpio, 0);	// Put the RS pin in the command state

	lcd_byte(pos);	// And finally sends the command to the display
}

// This function sends a string of characters to the display. 
// It is expected that no more than 16 characters will be sent to the display in a single write, so the function have a counter to ensure that no more that 16 bytes will be sent.
// That way, if a lot of charaters is passed to this function, there is no chance of messing with the display contents.
// The display hve a memory with 40 bytes per line. If the cursor is in the last position (like, position 16 of the first line) and 16 characters are set, they will be written to
// the display memory, but won't be shown. There is no harm in doing this, the exceeding characters just will not be shown.
void lcd_print(unsigned char * buffer)
{
	int x;
	
	// It is expected that this loop never reaches the maximum, the value is just a guard
	for(x = 0; x < 16; x++)
	{
		if(buffer[x] == 0)	// This means we reached the end of the string
			return;
			
		// The RS line is set high (the last lcd_byte call ensured this), so sending bytes to the display means sending characters
		lcd_byte(buffer[x]);
	}
}


static int __init inicializa(void)
{
	int ret;

	ret = gpio_request_array(pins, ARRAY_SIZE(pins));	// Here, a request is made to the kernel passing the global gpio structure. This command returns 0 if is OK
	
	if(ret)		// If ret is not zero, the request to the GPIOs failed, there's nothing else this module can do to command the display
	{
		printk(KERN_ERR "Unable to request the GPIOs for the LCD display. Errro code:%d\n", ret);
		return ret;
	}
	
	// Perform the LCD initialization. The following commands resets the display circuit, configure it to 4 bits data width, 2 lines and 5x8 caracters
	//  The commands follows the instructions presented in the HD44780 datasheet, page 46
	mdelay(15);
	lcd_nibble(0x03);
	mdelay(5);
	lcd_nibble(0x03);
	udelay(100);
	lcd_nibble(0x03);
	
	// The datasheet does not specify the next commands minimum delay (or at least I didn't find it), so I give 40us, like any other command (except the Clear Display)
	udelay(40);
	lcd_nibble(0x02); 	// I have no idea what this nibbler means, but the datasheet says so... 	
	udelay(40);
	
	// Function Set
	// The fields are 0 0 1 DL , N F * *
	// DL = Data Legth (0 == 4 bits)
	// N = Number of display lines (1 == 2 lines)
	// F = Character font (0 == 5x8 dots)
	// Sending 0010,1000 sets the display to 4 bits communication, 2 lines and 5x8 character format                                       
	lcd_nibble(0x02);
	lcd_nibble(0x08);
	udelay(40);
	
	// Display On/Off control
	// The fields are 0 0 0 0 , 1 D C B
	// D = Display on/off (1 == on)
	// C = Cursor on/off (0 == off
	// B = Blink on/off (0 == off)
	// Sending 0000,1100 sets the display on, no cursor and no blinking
	lcd_nibble(0x00);
	lcd_nibble(0x0C);
	udelay(40);
                                                  
	// Entry mode set
	// The fields are 0 0 0 0 , 0 1 I/D S
	// I/D = Increment/decrement (1 = increment)
	// S = shift (0 = don't shift)
	// Sending 0000,0110 sets the display to increment the cursor and don't shift the display
	lcd_nibble(0x00);
	lcd_nibble(0x06);                                                                                                    
	udelay(40);
	
	// Now I clear the display, it will put the cursor in the first position and set RS to character mode
	lcd_cls();
	
	lcd_print(line1);
	lcd_pos(17);
	lcd_print(line2);
	      
	// Register the device driver as a character device, passing the global fops structure where the methods are defined
	major = register_chrdev(0, "displaylcd", &fops);
	              
	// Check if the kernel registered the driver, otherwise the registering failed, and there's nothing else to do...
	if(major < 0)
	{
		printk(KERN_ALERT "Registering the LCD Display Device Driver failed! Error:%d\n", major);
		return major;
	}
	
	// Register the device driver class, I'm not sure wht this means
	devclass = class_create(THIS_MODULE, "displaylcdclass");
	devclass->devnode = classmode;
	
	if( IS_ERR(devclass) )	// Check if the class registering failed
	{
		unregister_chrdev(major, "displaylcd");	// If there's an error, unregister the character device, because there's nothing else to do
		printk(KERN_ALERT "Failed registering LCD Display Device Driver class\n");
		return PTR_ERR(devclass);
	}
	
	// Create the device driver under the /dev/displaylcd directory with minor number 0
	dev = device_create(devclass, NULL, MKDEV(major, 0), NULL, "displaylcd");
	
	if( IS_ERR(dev) )	// Check if there device creation failed
	{
		class_destroy(devclass);	// Removes the device class created above
		unregister_chrdev(major, "displaylcd");
		printk(KERN_ALERT "Failed creating the LCD Display Device Driver\n");
		return PTR_ERR(dev);
	}

	// Create the device driver under /dev/displaylcd_cls directry with minor number 1
	dev = device_create(devclass, NULL, MKDEV(major, 1), NULL, "displaylcd_cls");
	
	if( IS_ERR(dev) )
	{
		device_destroy(devclass, MKDEV(major, 0));
		class_unregister(devclass);
		class_destroy(devclass);
		unregister_chrdev(major, "displaylcd");
		printk(KERN_ALERT "Failed creating displaylcd_cls\n");
		return PTR_ERR(dev);
	}
	
	// Create the device driver under /dev/displaylcd_pos directry with minor number 2
	dev = device_create(devclass, NULL, MKDEV(major, 2), NULL, "displaylcd_pos");
	
	if( IS_ERR(dev) )
	{
		device_destroy(devclass, MKDEV(major, 0));
		device_destroy(devclass, MKDEV(major, 1));
		class_unregister(devclass);
		class_destroy(devclass);
		unregister_chrdev(major, "displaylcd");
		unregister_chrdev(major, "displaylcd_cls");
		printk(KERN_ALERT "Failed creating displaylcd_pos");	
		return PTR_ERR(dev);
	}
	               	
	return 0;
}

static void __exit finaliza(void)
{
	gpio_free_array(pins, ARRAY_SIZE(pins));
	device_destroy(devclass, MKDEV(major, 0));
	device_destroy(devclass, MKDEV(major, 1));
	device_destroy(devclass, MKDEV(major, 2));
	class_unregister(devclass);
	class_destroy(devclass);
	unregister_chrdev(major, "displaylcd");
	unregister_chrdev(major, "displaylcd_cls");
	unregister_chrdev(major, "displaylcd_pos");
}

static char * classmode(struct device * dev, umode_t * mode)
{
	*mode = 0666;
	return NULL;
}

module_init(inicializa);
module_exit(finaliza);
