/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <terry@terrys-home.co.uk>
 */


#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

#include "razermouse.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "0.1"
#define DRIVER_AUTHOR "Terry Cain <terry@terrys-home.co.uk>"
#define DRIVER_DESC "USB HID Razer Mouse"
#define DRIVER_LICENSE "GPL v2"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

/**
 * Send USB control report to the keyboard
 */
int razer_send_report(struct usb_device *usb_dev,void const *data)
{
    uint report_id = 0x300;
    uint value = HID_REQ_SET_REPORT;
    uint index = 0x02;
    uint size = RAZER_REPORT_LEN;
    char *buf;
    int len;

    buf = kmemdup(data, size, GFP_KERNEL);
    if (buf == NULL)
    return -ENOMEM;

    len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
            value,
            USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
            report_id,
            index, buf, size, USB_CTRL_SET_TIMEOUT);
            usleep_range(RAZER_WAIT_MIN_US,RAZER_WAIT_MAX_US);

    kfree(buf);
    return ((len < 0) ? len : ((len != size) ? -EIO : 0));
}

/**
 * Calculate the checksum for the usb message
 *
 * Checksum byte is stored in the 2nd last byte in the messages payload.
 * The checksum is generated by XORing all the bytes in the report starting
 * at byte number 2 (0 based) and ending at byte 88.
 */
unsigned char razer_calculate_crc(struct razer_report *report)
{
    /*second to last byte of report is a simple checksum*/
    /*just xor all bytes up with overflow and you are done*/
    unsigned char crc = 0;
    unsigned char *_report = (unsigned char*)report;

    unsigned int i;
    for(i = 2; i < 88; i++)
    {
        crc ^= _report[i];
    }

    return crc;
}

/**
 * Prepare the USB report
 *
 * The razer_report structure is zero'd and the ID byte is set to 0xFF
 * and the reserved2 byte is set to 0x03
 */
void razer_prepare_report(struct razer_report *report)
{
   /*fill static fields of report*/
   memset(report, 0, sizeof(struct razer_report));
   report->id = 0xFF;
   report->reserved2 = 0x03;
}

/**
 * Get the devices serial number
 *
 * Makes a request like normal, this must change a variable in the mouse as then we
 * tell it give us data (same request for get_battery) and it gives us a report.
 *
 * Supported Devices:
 *   Razer Mamba
 */
void razer_get_serial(struct usb_device *usb_dev, unsigned char* serial_string)
{
	int i = 0;
    uint report_id = 0x300;
    uint value = HID_REQ_GET_REPORT;
    uint index = 0x01;
    uint size = RAZER_REPORT_LEN;
    struct razer_report serial_report;
    int len;
    int retval;
    struct razer_report report;

    memset(&serial_report, 0, sizeof(struct razer_report));
    razer_prepare_report(&report);
    
    report.parameter_bytes_num = 0x16;
    report.reserved2 = 0x00;
    report.command = 0x82;
    report.sub_command = 0x00;
    report.command_parameters[0] = 0x00;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);

    // Now ask for battery level plz
    len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
          value,
          USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
          report_id,
          index, &serial_report, size, USB_CTRL_SET_TIMEOUT);

    usleep_range(RAZER_WAIT_MIN_US,RAZER_WAIT_MAX_US);

    // Error if report is wrong length
    if(len != 90)
    {
        printk(KERN_WARNING "razermouse: Unable to get serial. USB Report length: %d\n", len);
    } else
    {
        // Error if report is wrong type
        if(serial_report.report_start_marker == 0x02 && serial_report.reserved2 == 0x00 &&
           serial_report.command == 0x82)
        {
			unsigned char* pointer = &serial_report.sub_command;
			for(i = 0; i < 20; ++i)
			{
				serial_string[i] = *pointer;
				++pointer;
			}
        } else
        {
            printk(KERN_WARNING "razermouse: Serial Report Incorrect. Num bytes: %d. start: %02x id: %02x num_params: %02x reserved: %02x cmd: %02x subcmd: %02x param1: %02x .\n", len,
               serial_report.report_start_marker,
               serial_report.id,
               serial_report.parameter_bytes_num,
               serial_report.reserved2,
               serial_report.command,
               serial_report.sub_command,
               serial_report.command_parameters[0]);
        }
    }
}

/**
 * Get the battery level
 *
 * Makes a request like normal, this must change a variable in the mouse as then we
 * tell it give us data (same request for is_chaging) and it gives us a report.
 *
 * Supported Devices:
 *   Razer Mamba
 */
int razer_get_battery_level(struct usb_device *usb_dev)
{
    uint report_id = 0x300;
    uint value = HID_REQ_GET_REPORT;
    uint index = 0x01;
    uint size = RAZER_REPORT_LEN;
    struct razer_report battery_report;
    int len;
    int retval;
    struct razer_report report;
    int battery_level = -1;

    memset(&battery_report, 0, sizeof(struct razer_report));
    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x02;
    report.reserved2 = 0x07;
    report.command = 0x80;
    report.sub_command = 0x00;
    report.command_parameters[0] = 0x00;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);

    // Now ask for battery level plz
    len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
          value,
          USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
          report_id,
          index, &battery_report, size, USB_CTRL_SET_TIMEOUT);

    usleep_range(RAZER_WAIT_MIN_US,RAZER_WAIT_MAX_US);

    // Error if report is wrong length
    if(len != 90)
    {
        printk(KERN_WARNING "razermouse: Unable to get battery. USB Report length: %d\n", len);
    } else
    {
        // Error if report is wrong type
        if(battery_report.report_start_marker == 0x02 && battery_report.reserved2 == 0x07 &&
           battery_report.command == 0x80 && battery_report.sub_command == 0x00)
        {
            battery_level = battery_report.command_parameters[0];
        } else
        {
            printk(KERN_WARNING "razermouse: Battery Report Incorrect. Num bytes: %d. start: %02x id: %02x num_params: %02x reserved: %02x cmd: %02x subcmd: %02x param1: %02x .\n", len,
               battery_report.report_start_marker,
               battery_report.id,
               battery_report.parameter_bytes_num,
               battery_report.reserved2,
               battery_report.command,
               battery_report.sub_command,
               battery_report.command_parameters[0]);
        }
    }

    return battery_level;
}

/**
 * Check if the Mambas charging.
 *
 * Makes a request like normal, this must change a variable in the mouse as then we
 * tell it give us data (same request for get_battery) and it gives us a report.
 *
 * Supported Devices:
 *   Razer Mamba
 */
int razer_is_charging(struct usb_device *usb_dev)
{
    uint report_id = 0x300;
    uint value = HID_REQ_GET_REPORT;
    uint index = 0x01;
    uint size = RAZER_REPORT_LEN;
    struct razer_report battery_report;
    int len;
    int retval;
    struct razer_report report;
    int battery_level = -1;

    memset(&battery_report, 0, sizeof(struct razer_report));
    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x02;
    report.reserved2 = 0x07;
    report.command = 0x84;
    report.sub_command = 0x00;
    report.command_parameters[0] = 0x00;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);

    // Now ask for battery level plz
    len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
          value,
          USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
          report_id,
          index, &battery_report, size, USB_CTRL_SET_TIMEOUT);

    usleep_range(RAZER_WAIT_MIN_US,RAZER_WAIT_MAX_US);

    // Error if report is wrong length
    if(len != 90)
    {
        printk(KERN_WARNING "razermouse: Unable to get charging status. USB Report length: %d\n", len);
    } else
    {
        // Error if report is wrong type
        if(battery_report.report_start_marker == 0x02 && battery_report.reserved2 == 0x07 &&
           battery_report.command == 0x84 && battery_report.sub_command == 0x00)
        {
            battery_level = battery_report.command_parameters[0];
        } else
        {
            printk(KERN_WARNING "razermouse: Charging Status Report Incorrect. Num bytes: %d. start: %02x id: %02x num_params: %02x reserved: %02x cmd: %02x subcmd: %02x param1: %02x .\n", len,
               battery_report.report_start_marker,
               battery_report.id,
               battery_report.parameter_bytes_num,
               battery_report.reserved2,
               battery_report.command,
               battery_report.sub_command,
               battery_report.command_parameters[0]);
        }
    }

    return battery_level;
}

/**
 * Set the wave effect on the mouse
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_wave_mode(struct usb_device *usb_dev, unsigned char direction)
{
    int retval;
    struct razer_report report;
    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x02;
    report.command = 0x0A;                       /* Change effect command ID */
    report.sub_command = 0x01;                   /* Wave mode ID */
    report.command_parameters[0] = direction;    /* Direction 1=Up / 2=Down */
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set static effect on the mouse
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_static_mode(struct usb_device *usb_dev, struct razer_rgb *colour)
{
    int retval;
    struct razer_report report;
    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x04;
    report.command = 0x0A;                   /*change effect command id*/
    report.sub_command = 0x06;               /*static mode id*/
    report.command_parameters[0] = colour->r; /*rgb color definition*/
    report.command_parameters[1] = colour->g;
    report.command_parameters[2] = colour->b;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the spectrum effect on the mouse
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_spectrum_mode(struct usb_device *usb_dev)
{
    int retval;
    struct razer_report report;
    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x02;
    report.command = 0x0A;                       /* Change effect command ID */
    report.sub_command = 0x04;                   /* Spectrum mode ID */
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set reactive effect on the mouse
 *
 * The speed must be within 01-03
 *
 * 1 Short, 2 Medium, 3 Long
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_reactive_mode(struct usb_device *usb_dev, struct razer_rgb *colour, unsigned char speed)
{
    int retval = 0;
    struct razer_report report;

    if(speed < 0 || speed > 4)
    {
        printk(KERN_WARNING "razerkbd: Reactive mode, Speed must be within 1-3. Got: %d. Defaulting to long\n", speed);
        speed = 3;
    }


    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x05;
    report.command = 0x0A; /*change effect command id*/
    report.sub_command = 0x02;/*reactive mode id*/
    report.command_parameters[0] = speed;/*identified by Oleg Finkelshteyn*/
    report.command_parameters[1] = colour->r; /*rgb color definition*/
    report.command_parameters[2] = colour->g;
    report.command_parameters[3] = colour->b;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);

    return retval;
}

/**
 * Set breath effect on the mouse
 *
 * Breathing types
 * 1: Only 1 Colour
 * 2: 2 Colours
 * 3: Random
 *
 * Supported by:
 *   Razer Mamba
 *
 */
int razer_set_breath_mode(struct usb_device *usb_dev, unsigned char breathing_type, struct razer_rgb *color1, struct razer_rgb *color2)
{
    int retval;
    struct razer_report report;
    razer_prepare_report(&report);
    report.parameter_bytes_num = 0x08;
    report.command = 0x0A;
    report.sub_command = 0x03;

    report.command_parameters[0] = breathing_type;

    if(breathing_type == 1 || breathing_type == 2)
    {
        // Colour 1
        report.command_parameters[1] = color1->r;
        report.command_parameters[2] = color1->g;
        report.command_parameters[3] = color1->b;
    }

    if(breathing_type == 2)
    {
        // Colour 2
        report.command_parameters[4] = color2->r;
        report.command_parameters[5] = color2->g;
        report.command_parameters[6] = color2->b;
    }

    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the mouse brightness when on wireless
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_wireless_brightness(struct usb_device *usb_dev, unsigned char brightness)
{
    int retval;
    struct razer_report report;
    razer_prepare_report(&report);
    report.reserved2 = 0x07;               /* Power management section */
    report.parameter_bytes_num = 0x01;
    report.command = 0x02;                 /* Brightness command*/
    report.sub_command = brightness;       /* Brightness */
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the battery low blink threshold
 *
 * 0x3F = 25%
 * 0x26 = 15%
 * 0x0C =  5%
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_low_battery_threshold(struct usb_device *usb_dev, unsigned char threshold)
{
    int retval;
    struct razer_report report;

    if(threshold >= 0x40)
    {
        printk(KERN_WARNING "razermouse: Setting low battery threshold over 25%% has not been tested so capping it to 25%%.\n");
        threshold = 0x3F;
    }

    razer_prepare_report(&report);
    report.reserved2 = 0x07;               /* Power management section */
    report.parameter_bytes_num = 0x01;
    report.command = 0x01;                 /* Low battery threshold command*/
    report.sub_command = threshold;        /* Threshold */
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the mouse idle time
 *
 * idle_time is in seconds (max 900)
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_idle_time(struct usb_device *usb_dev, unsigned short idle_time)
{
    int retval;
    struct razer_report report;
    unsigned char part1;
    unsigned char part2;

    if(idle_time > 900)
    {
        printk(KERN_WARNING "razermouse: Cannot set an idle time of greater than 15 minutes. Setting to 15.\n");
        idle_time = 900;
    }

    part1 = (idle_time >> 8) & 0x00FF;
    part2 = idle_time & 0x00FF;

    razer_prepare_report(&report);
    report.reserved2 = 0x07;               /* Power management section */
    report.parameter_bytes_num = 0x02;
    report.command = 0x03;                 /* Mouse idle time command*/
    report.sub_command = part1;            /* Idle time */
    report.command_parameters[0] = part2;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the mouse DPI
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_mouse_dpi(struct usb_device *usb_dev, unsigned short dpi_x, unsigned short dpi_y)
{
    int retval;
    struct razer_report report;
    unsigned char dpi_x_part1;
    unsigned char dpi_x_part2;
    unsigned char dpi_y_part1;
    unsigned char dpi_y_part2;


    if(dpi_x > 16000)
    {
        printk(KERN_WARNING "razermouse: Cannot set an X DPI greater than 16000 minutes. Got: %d Setting to 16000.\n", dpi_x);
        dpi_x = 16000;
    }
    if(dpi_y > 16000)
    {
        printk(KERN_WARNING "razermouse: Cannot set an Y DPI greater than 16000 minutes. Got: %d Setting to 16000.\n", dpi_y);
        dpi_y = 16000;
    }

    dpi_x_part1 = (dpi_x >> 8) & 0x00FF;
    dpi_x_part2 = dpi_x & 0x00FF;
    dpi_y_part1 = (dpi_y >> 8) & 0x00FF;
    dpi_y_part2 = dpi_y & 0x00FF;

    razer_prepare_report(&report);
    report.reserved2 = 0x04;               /* DPI section */
    report.parameter_bytes_num = 0x07;
    report.command = 0x05;                 /* Mouse idle time command*/
    report.sub_command = 0x00;            /* Idle time */
    report.command_parameters[0] = dpi_x_part1;
    report.command_parameters[1] = dpi_x_part2;
    report.command_parameters[2] = dpi_y_part1;
    report.command_parameters[3] = dpi_y_part2;
    report.command_parameters[4] = 0x00;
    report.command_parameters[5] = 0x00;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the mouse charge effect
 *
 * 0x00 Charge using currently set mouse effect
 * 0x01 Charge using the charge colour
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_charging_effect(struct usb_device *usb_dev, unsigned char charge_type)
{
    int retval;
    struct razer_report report;

    if(charge_type > 1 || charge_type < 0)
    {
        printk(KERN_WARNING "razermouse: Cannot set an charge_type to anything other than 0 or 1. Got: %d, setting to 1.\n", charge_type);

        charge_type = 0x01;
    }


    razer_prepare_report(&report);
    report.reserved2 = 0x03;               /* Effects section */
    report.parameter_bytes_num = 0x01;
    report.command = 0x10;                 /* Change charging effect command*/
    report.sub_command = charge_type;      /* Charging effect */
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}

/**
 * Set the mouse charge effect colour
 *
 * Calls set_charge_effect(0x01)
 * Sets the charge colour to a given colour
 *
 * Supported by:
 *   Razer Mamba
 */
int razer_set_charging_colour(struct usb_device *usb_dev, struct razer_rgb *colour)
{
    int retval;
    struct razer_report report;

    razer_set_charging_effect(usb_dev, 0x01);

    razer_prepare_report(&report);
    report.reserved2 = 0x03;               /* Effects section */
    report.parameter_bytes_num = 0x05;
    report.command = 0x01;                 /* Change charging effect command*/
    report.sub_command = 00;      /* Charging effect */
    report.command_parameters[0] = 0x03; /* Unknown */
    report.command_parameters[1] = colour->r; /*rgb color definition*/
    report.command_parameters[2] = colour->g;
    report.command_parameters[3] = colour->b;
    report.crc = razer_calculate_crc(&report);
    retval = razer_send_report(usb_dev, &report);
    return retval;
}






/**
 * Read device file "get_battery"
 *
 * Returns an integer which needs to be scaled from 0-255 -> 0-100
 */
static ssize_t razer_attr_read_get_battery(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    int battery_level = razer_get_battery_level(usb_dev);
    //struct usb_interface *intf = to_usb_interface(dev->parent);
    //struct razer_kbd_device *widow = usb_get_intfdata(intf);
    return sprintf(buf, "%d\n", battery_level);
}

/**
 * Write device file "get_battery"
 *
 * Does nothing
 */
static ssize_t razer_attr_write_get_battery(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}

/**
 * Read device file "is_charging"
 *
 * Returns 0 when not charging, 1 when charging
 */
static ssize_t razer_attr_read_is_charging(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    int battery_level = razer_is_charging(usb_dev);

    return sprintf(buf, "%d\n", battery_level);
}

/**
 * Write device file "is_charging"
 *
 * Does nothing
 */
static ssize_t razer_attr_write_is_charging(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}

/**
 * Read device file "mode_wave"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_mode_wave(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "mode_wave"
 *
 * When 1 is written (as a character, 0x31) the wave effect is displayed moving up the mouse
 * if 2 is written (0x32) then the wave effect goes down
 */
static ssize_t razer_attr_write_mode_wave(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    //struct razer_kbd_device *widow = usb_get_intfdata(intf);
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    int temp = simple_strtoul(buf, NULL, 10);

    if(temp == 1 || temp == 2)
    {
        razer_set_wave_mode(usb_dev, temp);
    } {
        return count;
    }
}

/**
 * Read device file "mode_static"
 *
 * Done nothing
 */
static ssize_t razer_attr_read_mode_static(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "mode_static"
 *
 * Set the mouse to static mode when 3 RGB bytes are written
 */
static ssize_t razer_attr_write_mode_static(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    //struct razer_mouse_device *widow = usb_get_intfdata(intf);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    struct razer_rgb color;
    if(count == 3)
    {
        color.r = buf[0];
        color.g = buf[1];
        color.b = buf[2];
        razer_set_static_mode(usb_dev, &color);
    }

    return count;
}

/**
 * Read device file "mode_spectrum"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_mode_spectrum(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "mode_spectrum"
 *
 * Spectrum effect mode is activated whenever the file is written to
 */
static ssize_t razer_attr_write_mode_spectrum(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    razer_set_spectrum_mode(usb_dev);
    return count;
}

/**
 * Read device file "mode_reactive"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_mode_reactive(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "mode_reactive"
 *
 * Sets reactive mode when this file is written to. A speed byte and 3 RGB bytes should be written
 */
static ssize_t razer_attr_write_mode_reactive(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    if(count == 4)
    {
        unsigned char speed = (unsigned char)buf[0];
        razer_set_reactive_mode(usb_dev, (struct razer_rgb*)&buf[1], speed);
    } else
    {
        printk(KERN_WARNING "razermouse: Wrong number of bytes passed in for reactive effect mode. Got %d bytes\n", (int)count);
    }
    return count;
}

/**
 * Read device file "mode_breath"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_mode_breath(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "mode_breath"
 *
 * Sets breathing mode by writing 1, 3 or 6 bytes
 */
static ssize_t razer_attr_write_mode_breath(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    const char *alt_buf[6] = { 0 };

    if(count == 3)
    {
        // Single colour mode
        razer_set_breath_mode(usb_dev, 0x01, (struct razer_rgb*)&buf[0], (struct razer_rgb*)&alt_buf[3]);
    } else if(count == 6)
    {
        // Dual colour mode
        razer_set_breath_mode(usb_dev, 0x02, (struct razer_rgb*)&buf[0], (struct razer_rgb*)&buf[3]);
    } else
    {
        // "Random" colour mode
        razer_set_breath_mode(usb_dev, 0x03, (struct razer_rgb*)&alt_buf[0], (struct razer_rgb*)&alt_buf[3]);
    }
    return count;
}

/**
 * Read device file "set_wireless_brightness"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_set_wireless_brightness(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "set_wireless_brightness"
 *
 * Sets the brightness to the ASCII number written to this file.
 */
static ssize_t razer_attr_write_set_wireless_brightness(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    int brightness = simple_strtoul(buf, NULL, 10);
    razer_set_wireless_brightness(usb_dev, (unsigned char)brightness);
    return count;
}

/**
 * Read device file "set_low_battery_threshold"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_set_low_battery_threshold(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "set_low_battery_threshold"
 *
 * Sets the low battery blink threshold to the ASCII number written to this file.
 */
static ssize_t razer_attr_write_set_low_battery_threshold(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    int threshold = simple_strtoul(buf, NULL, 10);
    razer_set_low_battery_threshold(usb_dev, (unsigned char)threshold);
    return count;
}

/**
 * Read device file "set_idle_time"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_set_idle_time(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "set_idle_time"
 *
 * Sets the idle time to the ASCII number written to this file.
 */
static ssize_t razer_attr_write_set_idle_time(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    unsigned short idle_time = simple_strtoul(buf, NULL, 10);
    razer_set_idle_time(usb_dev, idle_time);
    return count;
}

/**
 * Read device file "set_mouse_dpi"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_set_mouse_dpi(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "set_mouse_dpi"
 *
 * Sets the mouse DPI to the unsigned short integer written to this file.
 */
static ssize_t razer_attr_write_set_mouse_dpi(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    unsigned short dpi_x;
    unsigned short dpi_y;


    // razer_set_breath_mode(usb_dev, 0x01, (struct razer_rgb*)&buf[0], (struct razer_rgb*)&alt_buf[3]);
    if(count == 2)
    {

        dpi_x = (buf[0] << 8) | (buf[1] & 0xFF);

        razer_set_mouse_dpi(usb_dev, dpi_x, dpi_x);
    } else if(count == 4)
    {
        dpi_x = (buf[0] << 8) | (buf[1] & 0xFF); // Apparently the char buffer is rubbish, as buf[1] somehow can equal FFFFFF80????
        dpi_y = (buf[2] << 8) | (buf[3] & 0xFF);

        razer_set_mouse_dpi(usb_dev, dpi_x, dpi_y);
    } else
    {
        printk(KERN_WARNING "razermouse: Unknown DPI setting to X:1500 Y:1500\n");
        razer_set_mouse_dpi(usb_dev, 1500, 1500);
    }

    return count;
}

/**
 * Read device file "set_charging_effect"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_set_charging_effect(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "set_charging_effect"
 *
 * Sets charging effect.
 */
static ssize_t razer_attr_write_set_charging_effect(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    if(count == 1)
    {
        razer_set_charging_effect(usb_dev, buf[0]);
    } else
    {
        printk(KERN_WARNING "razermouse: Incorrect number of bytes for setting the charging effect. Defaulting to 0x01\n");
        razer_set_charging_effect(usb_dev, 0x01);
    }
    return count;
}

/**
 * Read device file "set_charging_colour"
 *
 * Does nothing
 */
static ssize_t razer_attr_read_set_charging_colour(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

/**
 * Write device file "set_charging_colour"
 *
 * Sets charging colour using 3 RGB bytes
 */
static ssize_t razer_attr_write_set_charging_colour(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    const char alt_buf[3] = { 0xFF, 0x00, 0x00 };

    if(count == 3)
    {
        razer_set_charging_colour(usb_dev, (struct razer_rgb*)&buf[0]);
    } else
    {
        printk(KERN_WARNING "razermouse: Wrong number of bytes setting charging colour. Defaulting to red (FF0000)\n");
        razer_set_charging_colour(usb_dev, (struct razer_rgb*)&alt_buf[0]);
    }
    return count;
}

/**
 * Read device file "get_serial"
 *
 * Returns a string
 */
static ssize_t razer_attr_read_get_serial(struct device *dev, struct device_attribute *attr, char *buf)
{
	char serial_string[100] = ""; // Cant be longer than this as report length is 90
	
    struct usb_interface *intf = to_usb_interface(dev->parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);

    razer_get_serial(usb_dev, &serial_string[0]);
    return sprintf(buf, "%s\n", &serial_string[0]);
}

/**
 * Write device file "get_serial"
 *
 * Does nothing
 */
static ssize_t razer_attr_write_get_serial(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}










static DEVICE_ATTR(mode_static,               0664, razer_attr_read_mode_static, razer_attr_write_mode_static);
static DEVICE_ATTR(mode_wave,                 0664, razer_attr_read_mode_wave, razer_attr_write_mode_wave);
static DEVICE_ATTR(mode_spectrum,             0664, razer_attr_read_mode_spectrum, razer_attr_write_mode_spectrum);
static DEVICE_ATTR(mode_reactive,             0664, razer_attr_read_mode_reactive, razer_attr_write_mode_reactive);
static DEVICE_ATTR(mode_breath,               0664, razer_attr_read_mode_breath, razer_attr_write_mode_breath);
static DEVICE_ATTR(get_battery,               0664, razer_attr_read_get_battery, razer_attr_write_get_battery);
static DEVICE_ATTR(get_serial,                0664, razer_attr_read_get_serial, razer_attr_write_get_serial);
static DEVICE_ATTR(is_charging,               0664, razer_attr_read_is_charging, razer_attr_write_is_charging);
static DEVICE_ATTR(set_wireless_brightness,   0664, razer_attr_read_set_wireless_brightness, razer_attr_write_set_wireless_brightness);
static DEVICE_ATTR(set_low_battery_threshold, 0664, razer_attr_read_set_low_battery_threshold, razer_attr_write_set_low_battery_threshold);
static DEVICE_ATTR(set_idle_time,             0664, razer_attr_read_set_idle_time, razer_attr_write_set_idle_time);
static DEVICE_ATTR(set_mouse_dpi,             0664, razer_attr_read_set_mouse_dpi, razer_attr_write_set_mouse_dpi);
static DEVICE_ATTR(set_charging_effect,       0664, razer_attr_read_set_charging_effect, razer_attr_write_set_charging_effect);
static DEVICE_ATTR(set_charging_colour,       0664, razer_attr_read_set_charging_colour, razer_attr_write_set_charging_colour);













/**
 * Get raw event from the keyboard
 *
 * Useful if the keyboard's 2 keyboard devices are binded then keypress's can be
 * monitored and used.
 */
static int razer_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
    //struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    //struct razer_mouse_device *widow = hid_get_drvdata(hdev);

    //if (intf->cur_altsetting->desc.bInterfaceProtocol != USB_INTERFACE_PROTOCOL_MOUSE)
    //    return 0;

    return 0;
}

/**
 * Probe method is ran whenever a device is binded to the driver
 *
 * TODO remove goto's
 */
static int razer_mouse_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
    int retval;
    struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    //struct usb_device *usb_dev = interface_to_usbdev(intf);
    struct razer_mouse_device *dev = NULL;

    dev = kzalloc(sizeof(struct razer_mouse_device), GFP_KERNEL);
    if(dev == NULL) {
        dev_err(&intf->dev, "out of memory\n");
        retval = -ENOMEM;
        goto exit;
    }

    retval = device_create_file(&hdev->dev, &dev_attr_get_battery);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_get_serial);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_is_charging);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_set_wireless_brightness);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_set_low_battery_threshold);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_set_idle_time);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_set_mouse_dpi);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_set_charging_effect);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_set_charging_colour);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_mode_static);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_mode_wave);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_mode_spectrum);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_mode_reactive);
    if (retval)
        goto exit_free;
    retval = device_create_file(&hdev->dev, &dev_attr_mode_breath);
    if (retval)
        goto exit_free;




    hid_set_drvdata(hdev, dev);

    retval = hid_parse(hdev);
    if(retval)    {
        hid_err(hdev, "parse failed\n");
        goto exit_free;
    }
    retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (retval) {
        hid_err(hdev, "hw start failed\n");
       goto exit_free;
    }

    //razer_reset(usb_dev);
    //razer_activate_macro_keys(usb_dev);
    //msleep(3000);
    return 0;
exit:
    return retval;
exit_free:
    kfree(dev);
    return retval;
}

/**
 * Unbind function
 */
static void razer_mouse_disconnect(struct hid_device *hdev)
{
    struct razer_mouse_device *dev;
    struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    //struct usb_device *usb_dev = interface_to_usbdev(intf);

    dev = hid_get_drvdata(hdev);

    device_remove_file(&hdev->dev, &dev_attr_get_battery);
    device_remove_file(&hdev->dev, &dev_attr_get_serial);
    device_remove_file(&hdev->dev, &dev_attr_is_charging);
    device_remove_file(&hdev->dev, &dev_attr_set_wireless_brightness);
    device_remove_file(&hdev->dev, &dev_attr_set_low_battery_threshold);
    device_remove_file(&hdev->dev, &dev_attr_set_idle_time);
    device_remove_file(&hdev->dev, &dev_attr_set_mouse_dpi);
    device_remove_file(&hdev->dev, &dev_attr_set_charging_effect);
    device_remove_file(&hdev->dev, &dev_attr_set_charging_colour);
    device_remove_file(&hdev->dev, &dev_attr_mode_static);
    device_remove_file(&hdev->dev, &dev_attr_mode_wave);
    device_remove_file(&hdev->dev, &dev_attr_mode_spectrum);
    device_remove_file(&hdev->dev, &dev_attr_mode_reactive);
    device_remove_file(&hdev->dev, &dev_attr_mode_breath);



    hid_hw_stop(hdev);
    kfree(dev);
    dev_info(&intf->dev, "Razer Device disconnected\n");
}

/**
 * Device ID mapping table
 */
static const struct hid_device_id razer_devices[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_RAZER,USB_DEVICE_ID_RAZER_MAMBA) },
    { }
};

MODULE_DEVICE_TABLE(hid, razer_devices);

/**
 * Describes the contents of the driver
 */
static struct hid_driver razer_mouse_driver = {
    .name =        "razermouse",
    .id_table =    razer_devices,
    .probe =    razer_mouse_probe,
    .remove =    razer_mouse_disconnect,
    .raw_event = razer_raw_event
};

module_hid_driver(razer_mouse_driver);

