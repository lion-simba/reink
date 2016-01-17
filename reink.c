/* reink.c file for ReInk version 0.5
 * Copyright (C) 2008-2009 Alexey Osipov lion-simba@pridelands.ru
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */





//////
//		THIS UTILITY MAY DAMAGE YOUR PRINTER !!!
//		USE CAREFULLY
//////














/*
 ############################################################################
 This is EXPERIMENTAL utility to reset ink level in new chiped Epson
 cartridges programmatically through regular printer interface (i.e. without
 any additional hardware controllers).
 This is supposed to be an Open Source analog to Windows program named
 "SSC Service Utility".

 Home page:
 http://reink.lerlan.ru/ (English and Russian)
 ############################################################################

 ############################################################################
	    Basic principle of operation (as I see it).
 ----------------------------------------------------------------------------

    Information about ink levels is stored inside cartridge's chip's EEPROM.
 The information stored in form of USED (not REMAINING) ink - so, zero value
 means full cartridge.
    On power-on, printer reads this info into <Printer's EEPROM> by means of
 internal communication with CSIC. CSIC (Customer Specified Integrated Circuits)
 is another chip located on printer's head and it "talks" to cartridge's chip.

 So we have:
 ----------------------------------------------------------------------------
 <PC>---<Printer's CPU>---<CSIC>---<Cartridge's chip>
             |              |                 |
      <Printer's EEPROM>  <CSIC EEPROM>    <Cartridge EEPROM>

			Fig.1
 ----------------------------------------------------------------------------

    After each print-job, printer calculates approximate ink usage and writes
 this value back to <Printer's EEPROM>.
    On power-off, printer writes last-calculated value to <Cartridge EEPROM>
 by means of CSIC.

    The main idea is to tell printer not to write any values to it's EEPROM or
 cartridge. Or tell it to write zero value to it (which means "full").
    The possibility of this operation is prooved by "SSC Service Utility" program
 for Windows.

    Communication between <PC> and <Printer's CPU> can be done by several
 protocols: ESCP/2, EJL, D4. As we see from "gutenprint"'s "escputil" sources
 getting ink levels on modern printers is done by means of D4.
    As I see from communication logs of "SSC Service Utility" with my printer
 (Epson Stylus Photo 790) at moment of ink level reset, it uses D4 also. So
 let's took d4lib written by Jean-Jacques and use it to reproduce the magic
 sequence. :)

 ############################################################################

 ############################################################################
	    			    Usage
 ----------------------------------------------------------------------------

    Basic usage is:
    - to get current ink levels (as done by escputil):
	./reink -i -r printer_raw_device

    - to dump data from EEPROM
	./reink -d <addr>[-<addr>] -r printer_raw_device
	<addr> - two-byte address of EEPROM to read
	Two addresses recognized as range.
	Example: ./reink -d 0000-0FA0 -r /dev/usb/lp0

    - to write to arbitary EEPROM address (CAUTION: THIS MAY DAMAGE YOUR PRINTER!)
	./reink -w <addr>=<data> -r printer_raw_device
	Writes <data> to <addr>
	Example: ./reink -w 0206=00 -r /dev/usb/lp0

    - to reset ink level for ink type ink_type:
	./reink -z[ink_type] -r printer_raw_device
	<ink_type> - is the number of ink how it print by -i
        if <ink_type> is omitted, then - reset all known inks
	Example: ./reink -z1 -r /dev/usb/lp0

	- to reset waste ink counter:
	./reink -s -r printer_raw_device

    - to make an test report, containing some information about your printer
	./reink -t -r printer_raw_device > testreport.log

    You can set REINK_DEBUG environment variable to enable debug  output to
 stderr.
 REINK_DEBUG=0 - no debug;
 REINK_DEBUG=1 - debug only reink.c;
 REINK_DEBUG=2 - debug reink.c and d4lib.c also.

 ############################################################################
*/

#include <stdlib.h>	//getenv
#include <unistd.h>	//getopt
#include <stdio.h>	//printf, stdin, stderr, stdout
#include <string.h>	//strdup

#include <sys/types.h>	//fileIO
#include <sys/stat.h>	//fileIO
#include <fcntl.h>	//fileIO

#include <errno.h>	//errno

#include <sys/utsname.h> //uname -a

#include "d4lib.h"	//IEEE 1284.4
#include "printers.h" //printers defs

#define REINK_VERSION_MAJOR 0
#define REINK_VERSION_MINOR 5
#define REINK_VERSION_REV	0

#define CMD_NONE			0	//no command
#define CMD_GETINK			1	//command to display current ink levels
#define	CMD_DUMPEEPROM		2	//command to read from printer's EEPROM
#define	CMD_WRITEEEPROM		3	//command to write to printer's EEPROM
#define CMD_ZEROINK			4	//command to reset ink levels
#define CMD_REPORT			5	//command to make test report
#define CMD_ZEROWASTE		6	//command to reset waste ink counter

//EPSON factory commands classes and names
#define EFCMD_EEPROM_READ	0x41
#define EFCLS_EEPROM_READ	0x7c
#define EFCMD_EEPROM_WRITE	0x42
#define EFCLS_EEPROM_WRITE	0x7c

#define INPUT_BUF_LEN	1024

#define D(__c) 	if (ri_debug) {__c;};
#define D_OK 	D(fprintf(stderr, "OK\n"))

int ri_debug = 0;

void print_usage(const char* progname);

/* === protocol, channel initialization === */
/*
    Tries to connect to raw_device and open it for RW.
    Then tries to initialize IEEE 1284.4 packet mode.
    On success returns filedescriptor of opened device.
    On fail prints various error messages to stderr and returns -1.
*/
int printer_connect(const char* raw_device);

/*
    fd - file descriptor for printer raw_device
	 initialized in IEEE 1284.4 mode.
    Tries to carefully exit from IEEE 1284.4 mode
    and close printer raw_device (fd).
    On success returns 0.
    On fail prints various error messages to stderr and returns -1.
*/
int printer_disconnect(int fd);

/*
    fd - file descriptor for printer raw_device
	 initialized in IEEE 1284.4 mode.
    Tries to get socket_id for service_name and
    then open it.
    On success returns positive socket_id.
    On fail prints various error messages to stderr and returns -1.
*/
int open_channel(int fd, const char* service_name);

/*
    fd - file descriptor for printer raw_device
	 initialized in IEEE 1284.4 mode.
    socket_id - opened IEEE 1284.4 socket.
    Tries to close socket_id.
    On success returns 0.
    On fail prints various error messages to stderr and returns -1.
*/
int close_channel(int fd, int socket_id);

/*
    fd - file descriptor for printer raw_device
	 initialized in IEEE 1284.4 mode.
    socket_id - opened IEEE 1284.4 socket.
    buf_send - data to send.
    send_len - bytes count to send.
    buf_recv - buffer for recieved data.
    recv_len - IN:  maximum length of buf_recv,
	       OUT: actual bytes read.
    Tries to write to printer channel socket_id data specified by
    buf_send and read it's answer to buf_recv. Handles IEEE 1284.4
    credits internally.
    On success returns *recv_len.
    On fail prints various error messges to stderr and returns -1.
*/
int printer_transact(int fd, int socket_id, const char* buf_send, int send_len, char* buf_recv, int* recv_len);
/* -------------------------------- */

/* === information === */
unsigned int printer_model(const char* raw_device); //return printer model (PM_*) or PM_UNKNOWN
/* ------------------- */

/* === EPSON factory commands === */

//epson factory command header
typedef struct _fcmd_header_t {
	unsigned char cls1;
	unsigned char cls2;
	unsigned char lenL;
	unsigned char lenH;
	unsigned char mcode1;
	unsigned char mcode2;
	unsigned char cmd;
	unsigned char cmd1;
	unsigned char cmd2;
} fcmd_header_t;

/*
    cmd - pointer to uninitialized epson factory command header.
    model - printer model.
    class - factory command class.
    name - factory command name.
    extra_length - length of command arguments.
    Initialize given header based on passed in information.
    If model is pm_unknown, than mcode1 and mcode2 fields is set to zero
    and have to be initialized by caller.
*/
void init_command(fcmd_header_t* cmd, unsigned int model, unsigned char class, unsigned char name, unsigned short int extra_length);

/*
    fd - file descriptor for printer raw_device
	 initialized in IEEE 1284.4 mode.
    socket_id - opened IEEE 1284.4 socket for
	        "EPSON-CTRL" service.
    Tries to read one byte form printer's EEPROM address <addr> to <data>.
    On success returns 0.
    On fail returns -1.
*/
int read_eeprom_address(int fd, int socket_id, unsigned int model, unsigned short int addr, unsigned char* data);

/*
    fd - file descriptor for printer raw_device
	 initialized in IEEE 1284.4 mode.
    socket_id - opened IEEE 1284.4 socket for
	        "EPSON-CTRL" service.
    Tries to write one byte <data> to printer's EEPROM address <addr>.
    On success returns 0.
    On fail returns -1.
*/
int write_eeprom_address(int fd, int socket_id, unsigned int model, unsigned short int addr, unsigned char data);
/* ------------------------- */

/* === helpers === */
/*
   Searches <source> for string: "<tag>*****;"
   Returns ***** as null-terminated string in <value>.
   <tag> must be null-terminated string.

   On success returns 0.
   If not found returns -1.
   If found, but insufficient space in value, returns 1.
*/
int get_tag(const char* source, int source_len, const char* tag, char* value, int max_value_len);

/*
   Parses buf and prints ink levels info to stdout.
   On success returns 0.
   On fail returns -1.
*/
int parse_ink_result(const char* buf, int len);
/* --------------- */

/* === main workers === */
int do_ink_levels(const char* raw_device, unsigned int pm);
int do_ink_reset(const char* raw_device, unsigned int pm, unsigned char ink_type);
int do_eeprom_dump(const char* raw_device, unsigned int pm, unsigned short int start_addr, unsigned short int end_addr);
int do_eeprom_write(const char* raw_device, unsigned int pm, unsigned short int addr, unsigned char data);
int do_make_report(const char* raw_device, unsigned char model_code[]);
int do_waste_reset(const char* raw_device, unsigned int pm);
/* -------------------- */

int main(int argc, char** argv)
{
	int opt; 					//current option
	int command = CMD_NONE; 			//command to do
	unsigned int pmodel = PM_UNKNOWN; 	//printer model

	char* raw_device = NULL;	//-r option argument

	char* addr_range = NULL;	//-d option argument
	unsigned short int addr_s;	//start address for CMD_DUMPEEPROM
	unsigned short int addr_e;	//end address for CMD_DUMPEEPROM

	char* write_data = NULL;	//-w option argument
	unsigned short int write_addr;	//write address for CMD_WRITEEEPROM
	unsigned short int write_byte;	//data to write for CMD_WRITEEEPROM

	char* str_ink_type = NULL;	//-z option argument
	unsigned char ink_type = 0;	//ink_type for CMD_ZEROINK

	char* str_model_code = NULL; //-t option argument
	unsigned char model_code[2]; //model code for CMD_REPORT

	char* inval_pos;		//used in strtol to indicate conversion error
	char onebyte[3];		//holds one-byte hex value ("0A" for example), used in conversion

	char* str_reink_debug = NULL;	//the value of REINK_DEBUG environmental variable

	setDebug(0);
	str_reink_debug = getenv("REINK_DEBUG");
	if (str_reink_debug)
	{
		ri_debug = atoi(str_reink_debug);
		if (ri_debug > 1)
			setDebug(1);
	}

	onebyte[2] = '\0';

	while ((opt = getopt(argc, argv, "sir:d:w:z::t::")) != -1)
	{
		switch (opt)
		{
		case 'i':
			if (command != CMD_NONE)
			{
				print_usage(argv[0]);
				return 1;
			}
			command = CMD_GETINK;
			break;
		case 'r':
			raw_device = optarg;
			break;
		case 'd':
			if (command != CMD_NONE)
			{
				print_usage(argv[0]);
				return 1;
			}
			command = CMD_DUMPEEPROM;
			addr_range = optarg;
			break;
		case 't':
			if (command != CMD_NONE)
			{
				print_usage(argv[0]);
				return 1;
			}
			command = CMD_REPORT;
			str_model_code = optarg;
			break;
		case 'w':
			if (command != CMD_NONE)
			{
				print_usage(argv[0]);
				return 1;
			}
			command = CMD_WRITEEEPROM;
			write_data = optarg;
			break;
		case 'z':
			if (command != CMD_NONE)
			{
				print_usage(argv[0]);
				return 1;
			}
			command = CMD_ZEROINK;
			str_ink_type = optarg;
			break;
		case 's':
			if (command != CMD_NONE)
			{
				print_usage(argv[0]);
				return 1;
			}
			command = CMD_ZEROWASTE;
			break;
		default:
			return 1;
		}
	}

	//parameters checking...

	if (command == CMD_NONE)
	{
		print_usage(argv[0]);
		return 1;
	}

	if (raw_device == NULL)
	{
		print_usage(argv[0]);
		return 1;
	}

	if (command == CMD_DUMPEEPROM)
	{
		//check the range parameter..

		if (strlen(addr_range) == 4)
		{
			addr_s = strtol(addr_range, &inval_pos, 16);
			if (*inval_pos != '\0')
			{
				//conversion failed
				print_usage(argv[0]);
				return 1;
			}
			addr_e = addr_s;
		}
		else if ((strlen(addr_range) == 9) && (addr_range[4] == '-'))
		{
			addr_s = strtol(addr_range, &inval_pos, 16);
			if (*inval_pos != '-')
			{
				//conversion failed
				print_usage(argv[0]);
				return 1;
			}
			addr_e = strtol(addr_range+5, &inval_pos, 16);
			if (*inval_pos != '\0')
			{
				//conversion failed
				print_usage(argv[0]);
				return 1;
			}
		}
		else
		{
			print_usage(argv[0]);
			return 1;
		}

		if (addr_s > addr_e)
		{
			print_usage(argv[0]);
			return 1;
		}
	}
	else if (command == CMD_WRITEEEPROM)
	{
		//check write_data parameter
		if ((strlen(write_data) == 7) && (write_data[4] == '='))
		{
			write_addr = strtol(write_data, &inval_pos, 16); //address
			if (*inval_pos != '=')
			{
				//conversion failed
				print_usage(argv[0]);
				return 1;
			}
			write_byte = strtol(write_data+5, &inval_pos, 16); //data to write
			if (*inval_pos != '\0')
			{
				//conversion failed
				print_usage(argv[0]);
				return 1;
			}
		}
		else
		{
			print_usage(argv[0]);
			return 1;
		}
	}
	else if (command == CMD_ZEROINK)
	{
		//check str_ink_type parameter

		if (!str_ink_type)
		{
			ink_type = 0xFF; //all
		}
		else
		{
			ink_type = 1 << (strtol(str_ink_type, &inval_pos, 10) - 1); //one
			if (*inval_pos != '\0')
			{
				//conversion failed
				print_usage(argv[0]);
				return 1;
			}
		}
	}
	else if (command == CMD_REPORT)
	{
		if (str_model_code)
		{
			if (strlen(str_model_code) != 4)
			{
				print_usage(argv[0]);
				return 1;
			}

			model_code[1] = strtol(str_model_code + 2, &inval_pos, 16);
			if (*inval_pos != '\0')
			{
				print_usage(argv[0]);
				return 1;
			}

			str_model_code[2] = '\0';
			model_code[0] = strtol(str_model_code, &inval_pos, 16);
			if (*inval_pos != '\0')
			{
				print_usage(argv[0]);
				return 1;
			}
		}
		else
		{
			model_code[0] = 0x00;
			model_code[1] = 0x00;
		}
	}
	//end of options parsing

	//CMD_REPORT is a special case
	if (command == CMD_REPORT)
		return do_make_report(raw_device, model_code);

	//identifing printer
	pmodel = printer_model(raw_device);
	if (pmodel == PM_UNKNOWN)
	{
		fprintf(stderr, "Unknown printer. Wrong device file?\n");
		return 1;
	}

	switch (command)
	{
	case CMD_GETINK:
		return do_ink_levels(raw_device, pmodel);
		break;

	case CMD_DUMPEEPROM:
		return do_eeprom_dump(raw_device, pmodel, addr_s, addr_e);
		break;

	case CMD_WRITEEEPROM:
		return do_eeprom_write(raw_device, pmodel, write_addr, write_byte);
		break;

	case CMD_ZEROINK:
		return do_ink_reset(raw_device, pmodel, ink_type);
		break;
		
	case CMD_ZEROWASTE:
		return do_waste_reset(raw_device, pmodel);
		break;

	default:
		fprintf(stderr, "Unknown command.\n");
		return 1;
	}

	return 0;
}

void print_usage(const char* progname)
{
	fprintf(stderr, "ReInk v%d.%d.%d (http://reink.lerlan.ru)\n\
Basic usage is:\n\
    - to get current ink levels (as done by escputil):\n\
	%s -i -r printer_raw_device\n\
\n\
    - to dump data from EEPROM\n\
	%s -d <addr>[-<addr>] -r printer_raw_device\n\
	<addr> - two-byte address of EEPROM to read\n\
	Two addresses recognized as range.\n\
	Example: %s -d 0000-A000 -r /dev/usb/lp0\n\
\n\
    - to write to arbitary EEPROM address (CAUTION: THIS MAY DAMAGE YOUR PRINTER!)\n\
	%s -w <addr>=<data> -r printer_raw_device\n\
	Writes <data> to <addr>\n\
	Example: %s -w 0006=00 -r /dev/usb/lp0\n\
\n\
    - to reset ink level for ink type ink_type:\n\
	%s -z[ink_type] -r printer_raw_device\n\
	<ink_type> - is the number of ink how it print by -i\n\
        if <ink_type> is omitted, then - reset all known inks\n\
	Example: %s -z1 -r /dev/usb/lp0\n\
\n\
    - to reset waste ink counter:\n\
	%s -s -r printer_raw_device\n\
\n\
    - to make an test report, containing some information about your printer\n\
	./reink -t -r printer_raw_device > testreport.log\n\
\n\
    You can set REINK_DEBUG environment variable to enable debug  output to\n\
 stderr.\n\
 REINK_DEBUG=0 - no debug;\n\
 REINK_DEBUG=1 - debug only reink.c;\n\
 REINK_DEBUG=2 - debug reink.c and d4lib.c also.\n",
 REINK_VERSION_MAJOR, REINK_VERSION_MINOR, REINK_VERSION_REV,
 progname, progname, progname, progname, progname, progname, progname, progname);
}

/////////////////////////////////////////////////////////////////////////////////
//	MAIN WORKERS
/////////////////////////////////////////////////////////////////////////////////
//

int do_ink_levels(const char* raw_device, unsigned int pmodel)
{
	int device; //file descriptor of the printer raw_device
	int ctrl_socket; //IEEE 1284.4 socket identifier for "EPSON-CTRL" channel

	char buf[INPUT_BUF_LEN]; //buffer for input data
	int readed; //number of readed bytes

	D(fprintf(stderr, "=== do_ink_levels ===\n"))

	if ((device = printer_connect(raw_device)) < 0)
		return 1;

	if ((ctrl_socket = open_channel(device, "EPSON-CTRL")) < 0)
		return 1;

	D(fprintf(stderr, "Everything seems to be ready. :) Let's get ink level. Executing \"st\" command... "))
	readed = INPUT_BUF_LEN;
	if (printer_transact(device, ctrl_socket, "st\1\0\1", 5, buf, &readed))
		return 1;
	D_OK

	D(fprintf(stderr, "Parsing result... "))
	if (parse_ink_result(buf, readed))
	{
		fprintf(stderr, "FAIL.\n");
		return 1;
	}
	D_OK

	if (close_channel(device, ctrl_socket) < 0)
		return 1;

	if (printer_disconnect(device) < 0)
		return 1;

	D(fprintf(stderr, "^^^ do_ink_levels ^^^\n"))
	return 0;
}

int do_ink_reset(const char* raw_device, unsigned int pm, unsigned char ink_type)
{
	int i;
	unsigned char cur_ink;
	unsigned char* cur_addr;

	int device; //file descriptor of the printer raw_device
	int ctrl_socket; //IEEE 1284.4 socket identifier for "EPSON-CTRL" channel

	D(fprintf(stderr, "=== do_ink_reset ===\n"))
	
	if (pm == PM_UNKNOWN)
		return 1;

	if ((device = printer_connect(raw_device)) < 0)
		return 1;

	if ((ctrl_socket = open_channel(device, "EPSON-CTRL")) < 0)
		return 1;

	for(cur_ink = 1; cur_ink != 0x80; cur_ink <<= 1)
	{
		if (!(cur_ink & ink_type))
			continue;
		
		if (!(printers[pm].inkmap.mask & cur_ink))
		{
			if (ink_type == 0xFF) //reset all inks
				continue;
				
			fprintf(stderr, "Printer \"%s\" doesn't have ink bit %d.\n", printers[pm].name, cur_ink);
			return 1;
		}
		
		switch(cur_ink)
		{
		case INK_BLACK:
			cur_addr = printers[pm].inkmap.black;
			break;
		case INK_CYAN:
			cur_addr = printers[pm].inkmap.cyan;
			break;
		case INK_MAGENTA:
			cur_addr = printers[pm].inkmap.magenta;
			break;
		case INK_YELLOW:
			cur_addr = printers[pm].inkmap.yellow;
			break;
		case INK_LIGHTCYAN:
			cur_addr = printers[pm].inkmap.lightcyan;
			break;
		case INK_LIGHTMAGENTA:
			cur_addr = printers[pm].inkmap.lightmagenta;
			break;
		default:
			fprintf(stderr, "Unknown ink bit %d.\n", cur_ink);
			return 1;
		}
		
		D(fprintf(stderr, "Resetting ink bit %d... ", cur_ink));
		for (i=0;i<4;i++)
			if (write_eeprom_address(device, ctrl_socket, pm, cur_addr[i], 0x00))
			{
				fprintf(stderr, "Can't write to eeprom.\n");
				return 1;
			}
		D_OK
	}
	
	if (close_channel(device, ctrl_socket) < 0)
		return 1;

	if (printer_disconnect(device) < 0)
		return 1;

	D(fprintf(stderr, "^^^ do_ink_reset ^^^\n"))

	return 0;
}

int do_eeprom_dump(const char* raw_device, unsigned int pm, unsigned short int start_addr, unsigned short int end_addr)
{
	int device; //file descriptor of the printer raw_device
	int ctrl_socket; //IEEE 1284.4 socket identifier for "EPSON-CTRL" channel

	unsigned char data; //eeprom data (one byte)
	unsigned short int cur_addr; //current address

	int i;

	D(fprintf(stderr, "=== do_eeprom_dump ===\n"))

	if (pm == PM_UNKNOWN)
		return 1;

	if ((device = printer_connect(raw_device)) < 0)
		return 1;

	if ((ctrl_socket = open_channel(device, "EPSON-CTRL")) < 0)
		return 1;

	if (!printers[pm].twobyte_addresses && (end_addr & 0xFF00))
	{
		fprintf(stderr, "Printer \"%s\" doesn't support two-byte addresses, I will use lower byte only.\n", printers[pm].name);
		start_addr &= 0xFF;
		end_addr &= 0xFF;
	}

	D(fprintf(stderr, "Let's get the EEPROM dump (%x - %x)...\n", start_addr, end_addr))

	for (cur_addr = start_addr; (cur_addr <= end_addr) && (cur_addr >= start_addr); cur_addr++)
	{
		if (read_eeprom_address(device, ctrl_socket, pm, cur_addr, &data))
		{
			fprintf(stderr, "Fail to read EEPROM data from address %x.\n", cur_addr);
			return 1;
		}
		printf("0x%04X = 0x%02X\n", cur_addr, data);
		if (cur_addr == end_addr)
			break; //or there may be short int overflow and infinite loop
	}

	D_OK

	if (close_channel(device, ctrl_socket) < 0)
		return 1;

	if (printer_disconnect(device) < 0)
		return 1;

	D(fprintf(stderr, "^^^ do_eeprom_dump ^^^\n"))
	return 0;
}

int do_eeprom_write(const char* raw_device, unsigned int pm, unsigned short int addr, unsigned char data)
{
	int device; //file descriptor of the printer raw_device
	int ctrl_socket; //IEEE 1284.4 socket identifier for "EPSON-CTRL" channel

	unsigned char readed_data; //verification data

	D(fprintf(stderr, "=== do_eeprom_write ===\n"))

	if (pm == PM_UNKNOWN)
		return 1;

	if ((device = printer_connect(raw_device)) < 0)
		return 1;

	if ((ctrl_socket = open_channel(device, "EPSON-CTRL")) < 0)
		return 1;

	D(fprintf(stderr, "Let's write %#x to EEPROM address %#x...\n", data, addr))
	if (write_eeprom_address(device, ctrl_socket, pm, addr, data))
	{
		fprintf(stderr, "Fail to write EEPROM data to address %#x.\n", addr);
		return 1;
	}
	D_OK

	D(fprintf(stderr, "Verify by reading that byte... "))
	if (read_eeprom_address(device, ctrl_socket, pm, addr, &readed_data))
	{
		fprintf(stderr, "Fail to subsequent read from EEPROM address %#x.\n", addr);
		return 1;
	}

	if (readed_data != data)
	{
		fprintf(stderr, "Verification failed (readed byte = %#x).\n", readed_data);
		return 1;
	}
	D_OK

	if (close_channel(device, ctrl_socket) < 0)
		return 1;

	if (printer_disconnect(device) < 0)
		return 1;

	D(fprintf(stderr, "^^^ do_eeprom_write ^^^\n"))
	return 0;
}

int do_waste_reset(const char* raw_device, unsigned int pm)
{
	int i;

	int device; //file descriptor of the printer raw_device
	int ctrl_socket; //IEEE 1284.4 socket identifier for "EPSON-CTRL" channel

	D(fprintf(stderr, "=== do_waste_reset ===\n"))

	if (pm == PM_UNKNOWN)
		return 1;

	if ((device = printer_connect(raw_device)) < 0)
		return 1;

	if ((ctrl_socket = open_channel(device, "EPSON-CTRL")) < 0)
		return 1;

	D(fprintf(stderr, "Resetting... "));
	for (i=0;i<printers[pm].wastemap.len;i++)
		if (write_eeprom_address(device, ctrl_socket, pm, printers[pm].wastemap.addr[i], 0x00))
		{
			fprintf(stderr, "Can't write to eeprom.\n");
			return 1;
		}
	D_OK

	if (close_channel(device, ctrl_socket) < 0)
		return 1;

	if (printer_disconnect(device) < 0)
		return 1;

	D(fprintf(stderr, "^^^ do_waste_reset ^^^\n"))

	return 0;
}

/*
What we need to know about unknown printer?
1) name
2) ability to use ieee1284.4
3) secret model code
4) cmds support: one-byte address read/write, two-byte address read/write
5) eeprom map (at least addresses with inks usage)
*/
int do_make_report(const char* raw_device, unsigned char model_code[])
{
	struct utsname linux_info; //uname -a reply
	int fd; //raw_device file descriptor
	int socket2; //socket for EPSON-CTRL
	int socket40; //socket for EPSON-DATA
	int have_model_code = 0; //do we have model code?
	char buf[INPUT_BUF_LEN]; //buffer for input data
	int readed;	//length of data in input buffer
	unsigned short int caddr; //current address
	unsigned char data; //one byte from eeprom
	int original_stderr;
	int original_debug = ri_debug; //original value of ri_debug

	printf("ReInk v%d.%d test report.\n", REINK_VERSION_MAJOR, REINK_VERSION_MINOR);

	fprintf(stderr, "Please, be patient.\nWait at least 10 minutes before force interrupt.\n");

	//uname -a
	if (!uname(&linux_info))
	{
		printf("sysname: %s\nrelease: %s\nvarsion: %s\narch: %s\n\n", linux_info.sysname,
		       linux_info.release,
		       linux_info.version,
		       linux_info.machine);
	}
	fflush(stdout);

	//redirecting stderr to stdout (2>1)
	original_stderr = dup(fileno(stderr));
	dup2(fileno(stdout), fileno(stderr));

	//enabling debug info
	setDebug(1);
	ri_debug=1;

	//raw_device (r/w status)
	//can enter in ieee1284.4 mode?
	if ((fd = printer_connect(raw_device)) < 0)
		return 0;

	//opening EPSON-CTRL
	if ((socket2 = open_channel(fd, "EPSON-CTRL")) < 0)
	{
		printer_disconnect(fd);
		return 0;
	}

	//opening EPSON-DATA (just try to open - then close)
	if ((socket40 = open_channel(fd, "EPSON-DATA")) >= 0)
		close_channel(fd, socket40); //no need anymore

	//reply to "di" command
	readed = INPUT_BUF_LEN;
	if (printer_transact(fd, socket2, "di\1\0\1", 5, buf, &readed) < 0)
	{
		close_channel(fd, socket2);
		printer_disconnect(fd);
		return 0;
	}

	//reply to "st" command
	readed = INPUT_BUF_LEN;
	if (printer_transact(fd, socket2, "st\1\0\1", 5, buf, &readed) < 0)
	{
		close_channel(fd, socket2);
		printer_disconnect(fd);
		return 0;
	}

	//init unknown printer
	printers[PM_UNKNOWN].model_code[0] = model_code[0];
	printers[PM_UNKNOWN].model_code[1] = model_code[1];

	//assume first this is two-byte addresses printer
	printers[PM_UNKNOWN].twobyte_addresses = 1;

	if (model_code[0] != 0 || model_code[1] != 0)
		have_model_code = 1;
	else
	{
		printf("Searching printer secret model code with brute force.\n");
		fflush(stdout);
	}

	//let's find out the model code
	while (printers[PM_UNKNOWN].model_code[0] != 0xFF && !have_model_code)
	{
		//try to read from eeprom (address 00)
		if (read_eeprom_address(fd, socket2, PM_UNKNOWN, 0x00, &data) == 0)
		{
			printf("We found model code: 0x%02X 0x%02X\n", printers[PM_UNKNOWN].model_code[0], printers[PM_UNKNOWN].model_code[1]);
			have_model_code = 1;
			break;
		}

		if (printers[PM_UNKNOWN].model_code[1]++ == 0xFF)
			printers[PM_UNKNOWN].model_code[0]++;

		if (ri_debug && !original_debug)
		{
			//disabling debug to speedup process
			setDebug(0);
			ri_debug = 0;
		}
	}

	if (!have_model_code)
	{
		close_channel(fd, socket2);
		printer_disconnect(fd);
		return 0;
	}

	//on success - dump eeprom 0x00-0xFF

	//reenabling debug to see what we have when read from printer
	setDebug(1);
	ri_debug = 1;

	if (read_eeprom_address(fd, socket2, PM_UNKNOWN, 0x00, &data) != 0)
	{
		close_channel(fd, socket2);
		printer_disconnect(fd);
		return 0;
	}

	//disabling debug before dump
	setDebug(0);
	ri_debug = 0;


	printf("\nEEPROM DUMP:\n");
	for (caddr = 0; caddr <= 0xFF; caddr++)
	{
		if (read_eeprom_address(fd, socket2, PM_UNKNOWN, caddr, &data) < 0)
		{
			close_channel(fd, socket2);
			printer_disconnect(fd);
			return 0;
		}
		printf("0x%02X = 0x%02X\n", caddr, data);
	}

	//redirecting stderr back to console
	dup2(original_stderr, fileno(stderr));

	fprintf(stderr, "Report complete. Thank you.\n");

	if (have_model_code && (model_code[0] == 0 && model_code[1] == 0))
	{
		fprintf(stderr, "You will provide even more help for me if\n\
you run printer head cleaning now and\n\
then make another report with command:\n\
./reink -r %s -t%02X%02X > ./testreport2.log\n", raw_device, printers[PM_UNKNOWN].model_code[0], printers[PM_UNKNOWN].model_code[1]);
	}
	else if (model_code[0] != 0 || model_code[1] != 0)
	{
		fprintf(stderr, "Now send me both reports.\n");
	}

	close_channel(fd, socket2);
	printer_disconnect(fd);
	close(fd);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
//	HELPERS
/////////////////////////////////////////////////////////////////////////////////
//

int parse_ink_result(const char* buf, int len)
{
	char ink_info[20];
	char ink_val[3];
	int i;
	int val;

	D(fprintf(stderr, "=== parse_ink_result ===\n"))

	D(fprintf(stderr, "Getting the \"IQ:\" tag... "));
	if (get_tag(buf, len, "IQ:", ink_info, 20))
	{
		fprintf(stderr, "Can't find ink levels information in printer answer.\n");
		return 1;
	}
	D(fprintf(stderr, "OK, have string \"%s\".\n", ink_info));

	if ((strlen(ink_info)) % 2 != 0)
	{
		fprintf(stderr, "Malformed output in printer answer.\n");
		return 1;
	}

	printf("Ink levels:\n");
	ink_val[2] = '\0';
	for (i=0; i<strlen(ink_info); i += 2)
	{
		strncpy(ink_val, ink_info+i, 2);
		val = strtol(ink_val, NULL, 16);
		printf("Ink type (color) %d remains %d percents.\n", i/2+1, val);
	}

	D(fprintf(stderr, "^^^ parse_ink_result ^^^\n"))

	return 0;
}

int get_tag(const char* source, int source_len, const char* tag, char* value, int max_value_len)
{
	int tag_len;
	int pos;
	int pos_end;
	int val_len;

	D(fprintf(stderr, "=== get_tag ===\n"))

	D(fprintf(stderr, "Searching for \"%s\" substring... ", tag));

	tag_len = strlen(tag);
	pos = 0;
	while ((pos + tag_len < source_len) &&  (0 != strncmp(source+pos, tag, tag_len)))
		pos++;

	if (pos + tag_len == source_len)
	{
		D(fprintf(stderr, "NOT FOUND.\n"));
		return -1;
	}

	D(fprintf(stderr, "FOUND, pos=%d.\n", pos));

	pos += tag_len;

	D(fprintf(stderr, "Searching for \";\" character... "));
	pos_end = pos;
	while ((pos_end < source_len) && (source[pos_end] != ';'))
		pos_end++;
	if (pos_end  == source_len)
	{
		D(fprintf(stderr, "NOT FOUND.\n"));
		return -1;
	}
	D(fprintf(stderr, "FOUND, pos_end=%d.\n", pos_end));

	val_len = pos_end - pos;
	if (val_len+1 > max_value_len)
	{
		D(fprintf(stderr, "Value(+'\\0') too long (%d) for given buffer (%d).\n", val_len+1, max_value_len));
		return 1;
	}

	memcpy(value, source + pos, val_len);
	value[val_len] = '\0';
	D(fprintf(stderr, "Tag value:\"%s\".\n", value));

	D(fprintf(stderr, "^^^ get_tag ^^^\n"))

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
//	INFORMATION
/////////////////////////////////////////////////////////////////////////////////
//

unsigned int printer_model(const char* raw_device)
{
	int device; //file descriptor of the printer raw_device
	int ctrl_socket; //IEEE 1284.4 socket identifier for "EPSON-CTRL" channel

	unsigned int i;

	char buf[INPUT_BUF_LEN]; //buffer for input data
	int readed; //number of readed bytes

	char strModel[MAX_MODEL_LEN];

	unsigned int model = PM_UNKNOWN;

	D(fprintf(stderr, "=== printer_model ===\n"))

	if ((device = printer_connect(raw_device)) < 0)
		return PM_UNKNOWN;

	if ((ctrl_socket = open_channel(device, "EPSON-CTRL")) < 0)
		return PM_UNKNOWN;

	D(fprintf(stderr, "Let's get printer info. Executing \"di\" command... "))
	readed = INPUT_BUF_LEN;
	if (printer_transact(device, ctrl_socket, "di\1\0\1", 5, buf, &readed))
		return PM_UNKNOWN;
	D_OK

	D(fprintf(stderr, "Parsing result... "))
	if (get_tag(buf, readed, "MDL:", strModel, MAX_MODEL_LEN))
	{
		D(fprintf(stderr, "Parse failed.\n"));
		return PM_UNKNOWN;
	}
	D_OK

	for(i = 0; i < printers_count; i++)
	{
		if (!strcmp(strModel, printers[i].model_name))
		{
			D(fprintf(stderr, "Printer \"%s\".\n", printers[i].name));
			model = i;
			break;
		}
	}
	
	if (close_channel(device, ctrl_socket) < 0)
		return model;

	if (printer_disconnect(device) < 0)
		return model;

	D(fprintf(stderr, "^^^ printer_model ^^^\n"))
	return model;
}

/////////////////////////////////////////////////////////////////////////////////
//	PROTOCOL, CHANNEL INITIALIZATION, FINILIZING
/////////////////////////////////////////////////////////////////////////////////
//

int printer_connect(const char* raw_device)
{
	int device;

	D(fprintf(stderr, "=== printer_connect ===\n"));

	D(fprintf(stderr, "Opening raw device... "))
	device = open(raw_device, O_RDWR | O_SYNC);
	if (device == -1)
	{
		fprintf(stderr, "Error opening device file '%s': %s\n", raw_device, strerror(errno));
		return -1;
	}
	D_OK

	clearSndBuf(device); //if there are some data from previous incoreectly terminated session

	D(fprintf(stderr, "Entering IEEE 1284.4 mode... "))
	if (!EnterIEEE(device))
	{
		fprintf(stderr, "Can't enter in IEEE 1284.4 mode. Wrong printer device file?\n");
		return -1;
	}
	D_OK

	D(fprintf(stderr, "Perfoming IEEE 1284.4 Init transaction... "))
	if (!Init(device))
	{
		fprintf(stderr, "IEEE 1284.4: \"Init\" transaction failed.\n");
		return -1;
	}
	D_OK

	D(fprintf(stderr, "^^^ printer_connect ^^^\n"));

	return device;
}

int printer_disconnect(int fd)
{
	D(fprintf(stderr, "=== printer_disconnect ===\n"));

	D(fprintf(stderr, "Perfoming IEEE 1284.4 Exit transaction... "))
	if (!Exit(fd))
	{
		fprintf(stderr, "IEEE 1284.4: \"Exit\" transaction failed.\n");
		return -1;
	}
	D_OK

	D(fprintf(stderr, "Closing raw device... "))
	fd = close(fd);
	if (fd == -1)
	{
		fprintf(stderr, "Error closing printer device file: %s\n", strerror(errno));
		return -1;
	}
	D_OK

	D(fprintf(stderr, "^^^ printer_disconnect ^^^\n"));

	return 0;
}


int open_channel(int fd, const char* service_name)
{
	int socket;
	int max_send_packet = 0x0200; //maximum size of PC to printer packet (this value may be changed by the printer while opening a channel)
	int max_recv_packet = 0x0200; //maximum size of printer to PC packet (this value may be changed by the printer while opening a channel)

	D(fprintf(stderr, "=== open_channel ===\n"));

	D(fprintf(stderr, "Obtaining IEEE 1284.4 socket for \"%s\" service... ", service_name))
	if (!(socket = GetSocketID(fd, service_name)))
	{
		fprintf(stderr, "IEEE 1284.4: \"GetSocketID\" transaction failed.\n");
		return -1;
	}
	D(fprintf(stderr, "OK, socket=%d.\n", socket));

	D(fprintf(stderr, "Opening IEEE 1284.4 channel %d-%d... ", socket, socket))
	if (1 != OpenChannel(fd, socket, &max_send_packet, &max_recv_packet))
	{
		fprintf(stderr, "IEEE 1284.4: \"OpenChannel\" transaction failed.\n");
		return -1;
	}
	D_OK

	D(fprintf(stderr, "^^^ open_channel ^^^\n"));

	return socket;
}

int close_channel(int fd, int socket_id)
{
	D(fprintf(stderr, "=== close_channel ===\n"));

	D(fprintf(stderr, "Closing IEEE 1284.4 channel %d-%d... ", socket_id, socket_id))
	if (1 != CloseChannel(fd, socket_id))
	{
		fprintf(stderr, "IEEE 1284.4: \"CloseChannel\" transaction failed.\n");
		return -1;
	}
	D_OK

	D(fprintf(stderr, "^^^ close_channel ^^^\n"));

	return 0;
}

int printer_transact(int fd, int socket_id, const char* buf_send, int send_len, char* buf_recv, int* recv_len)
{
	int credits = 0; //count of ieee1284.4 credits I have left
	int buf_len;	//the length of recieve buffer

	D(fprintf(stderr, "=== printer_transact ===\n"));

	buf_len = *recv_len;

	D(fprintf(stderr, "Requesting some IEEE 1284.4 credits on channel %d-%d... ", socket_id, socket_id))
	credits = CreditRequest(fd, socket_id);
	if (credits < 1)
	{
		fprintf(stderr, "IEEE 1284.4: \"CreditRequest\" transaction failed.\n");
		return -1;
	}
	D(fprintf(stderr, "OK, got %d credits.\n", credits))

	/* // made automatically by readData later
	D(fprintf(stderr, "Giving one IEEE 1284.4 credit to printer on channel %d-%d... ", ctrl_socket, ctrl_socket))
	if (1 != Credit(device, ctrl_socket, 1))
	{
	fprintf(stderr, "IEEE 1284.4: \"Credit\" transaction failed.\n");
	return -1;
	}
	D_OK
	*/

	D(fprintf(stderr, "Writing data to printer... "))
	if (writeData(fd, socket_id, buf_send, send_len, 0) < send_len)
	{
		fprintf(stderr, "IEEE 1284.4: Error sending data to channel %d-%d.\n", socket_id, socket_id);
		return -1;
	}
	D_OK

	D(fprintf(stderr, "Get the answer... "))
	if ((*recv_len = readData(fd, socket_id, buf_recv, buf_len)) < 0)
	{
		fprintf(stderr, "IEEE 1284.4: Error recieving data from channel %d-%d.\n", socket_id, socket_id);
		return -1;
	}
	D_OK

	D(fprintf(stderr, "^^^ printer_transact ^^^\n"));

	return 0;
}



/////////////////////////////////////////////////////////////////////////////////
//	EPSON FACTORY COMMANDS
/////////////////////////////////////////////////////////////////////////////////

void init_command(fcmd_header_t* cmd, unsigned int pm, unsigned char class, unsigned char name, unsigned short int extra_length)
{
	unsigned short int full_length;

	full_length = 5 + extra_length;

	cmd->cls1 = class;
	cmd->cls2 = class;

	cmd->lenL = full_length & 0xFF;
	cmd->lenH = (full_length >> 8) & 0xFF;

	cmd->cmd = name;
	cmd->cmd1 = ~name;
	cmd->cmd2 = ((name >> 1) & 0x7F) | ((name << 7) & 0x80); //round shift by one bit to the right

	cmd->mcode1 = printers[pm].model_code[0];
	cmd->mcode2 = printers[pm].model_code[1];
}

int read_eeprom_address(int fd, int socket_id, unsigned int pm, unsigned short int addr, unsigned char* data)
{
	char cmd[11]; // full command with address
	int cmd_len = 10; //length of the command
	int cmd_args_count = 1; //command arguments count

	char reply[INPUT_BUF_LEN]; // buffer for printer reply
	int actual; // actual reply length

	char reply_data[7]; // buffer for "EE" tag (contains readed byte)
	int reply_data_len = 4; //expected reply_data length

	char onebyte[5]; //contains one or two HEX byte string ("B2\0" for example)
	unsigned short int replyaddr; //reply address (for confirmation)

	D(fprintf(stderr, "=== read_eeprom_address ===\n"))

	cmd[9] = addr & 0xFF;
	if (printers[pm].twobyte_addresses)
	{
		cmd[10] = (addr >> 8) & 0xFF;
		cmd_len = 11;
		cmd_args_count = 2;
		reply_data_len = 6;
	}
	else
	{
		if ((addr >> 8) != 0)
		{
			D(fprintf(stderr, "Printer \"%s\" don't support two-byte addresses. Continuing using low byte only.\n", printers[pm].name));
			addr = addr & 0xFF;
		}
	}

	init_command((fcmd_header_t*)cmd, pm, EFCLS_EEPROM_READ, EFCMD_EEPROM_READ, cmd_args_count);

	D(fprintf(stderr, "Reading eeprom address %#x... ", addr))
	actual = INPUT_BUF_LEN;
	if (printer_transact(fd, socket_id, cmd, cmd_len, reply, &actual))
	{
		D(fprintf(stderr, "Transact failed.\n"))
		return -1;
	}

	if (get_tag(reply, actual, "EE:", reply_data, 7))
	{
		D(fprintf(stderr, "Can't get reply data.\n"))
		return -1;
	}
	D_OK

	if (strlen(reply_data) != reply_data_len)
	{
		D(fprintf(stderr, "ReplyData length != %d\n", reply_data_len))
		reply_data_len -= 2; //assuming this is one-byte addresses printer
		if (strlen(reply_data) == reply_data_len)
		{
			D(fprintf(stderr, "Seems like printer with one-byte addresses EEPROM.\n"))
		}
		else
			return -1;
	}

	strncpy(onebyte, reply_data, reply_data_len - 2);
	onebyte[reply_data_len - 2] = '\0';

	replyaddr = strtol(onebyte, NULL, 16);
	if (replyaddr != addr)
	{
		D(fprintf(stderr, "Reply address (%x) don't match requested (%x).\n", replyaddr, addr))
		return -1;
	}

	strncpy(onebyte, reply_data + reply_data_len - 2, 2); //the data itself
	onebyte[2] = '\0';
	*data = strtol(onebyte, NULL, 16);

	D(fprintf(stderr, "EEPROM addr %#x = %#x.\n", addr, *data))

	D(fprintf(stderr, "^^^ read_eeprom_address ^^^\n"))

	return 0;
}

int write_eeprom_address(int fd, int socket_id, unsigned int pm, unsigned short int addr, unsigned char data)
{
	char cmd[12]; // full command with address
	int cmd_len = 11; // full length of the command
	int cmd_args_len = 2; // command arguments count

	char reply[INPUT_BUF_LEN]; // buffer for printer reply
	int actual; // actual reply length
	char reply_data[6]; // buffer for "OK" tag

	D(fprintf(stderr, "=== write_eeprom_address ===\n"))

	cmd[9] = addr & 0xFF;
	if (printers[pm].twobyte_addresses)
	{
		cmd[10] = (addr >> 8) & 0xFF;
		cmd[11] = (char)data;
		cmd_len = 12;
		cmd_args_len = 3;
	}
	else
	{
		cmd[10] = (char)data;

		if ((addr >> 8) != 0)
			D(fprintf(stderr, "Printer \"%s\" don't support two-byte addresses. Continuing using low byte only.\n", printers[pm].name));
	}

	init_command((fcmd_header_t*)cmd, pm, EFCLS_EEPROM_WRITE, EFCMD_EEPROM_WRITE, cmd_args_len);

	D(fprintf(stderr, "Writing %#x to eeprom address %#x... ", data, addr))
	actual = INPUT_BUF_LEN;
	if (printer_transact(fd, socket_id, cmd, cmd_len, reply, &actual))
	{
		D(fprintf(stderr, "Transact failed.\n"))
		return -1;
	}

	if (get_tag(reply, actual, "OK", reply_data, 6))
	{
		D(fprintf(stderr, "Can't get reply data.\n"))
		return -1;
	}
	D_OK

	D(fprintf(stderr, "^^^ write_eeprom_address ^^^\n"))

	return 0;
}
