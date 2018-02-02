/* This file is part of ReInk.
 * Copyright (C) 2008-2016 Alexey Osipov public@alexey.osipov.name
 *
 * ReInk is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * ReInk is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ReInk. If not, see <http://www.gnu.org/licenses/>.
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
#include <stdint.h>
#include <string.h>	//strdup

#include <sys/types.h>	//fileIO
#include <sys/stat.h>	//fileIO
#include <fcntl.h>	//fileIO

#include <errno.h>	//errno

#include <sys/utsname.h> //uname -a

#include "d4lib.h"	//IEEE 1284.4
#include "printers.h" //printers defs
#include "util.h"

#define REINK_VERSION_MAJOR 0
#define REINK_VERSION_MINOR 6
#define REINK_VERSION_REV   0

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

#define reink_dbg(...)				\
	do {					\
		if (ri_debug)			\
			reink_log(__VA_ARGS__);	\
	} while (0)

#define DBG_OK()	reink_dbg("OK\n")

int ri_debug = 0;

void print_usage(const char* progname);

/*
 * A printer consists of an immutable descriptor, as well as dynamic data, such
 * as how it is connected to the system. Communication with the printer happens
 * over IEEE1284.4 sockets.
 * Initialization: printer_connect()
 *     will fill in the dynamic part of the 'printer' struct.
 * Connection: open_channel()
 *     In order to communicate, a channel must be establishedm and the
 *     appropriate ieee1284_socket structure initialized. This is done by
 *     open_channel(), which handles any backend magic.
 * Communication: printer_transact() and derivatives
 *     Once a ieee1284_socket is available, it can be used to send data to and
 *     from the printer. printer_transact() is the main interface for this,
 *     although helpers built on top of printer_transact() do exist.
 * Disconnect: close_channel()
 *     Sockets must first be closed with close_channel(). Once all sockets
 *     previously created from a printer are closed, the printer itself may be
 *     released.
 * Cleanup: printer_disconnect()
 *     The printer does not keep track of the active sockets, so
 *     printer_disconnect() will not be able to close any remaining sockets.
 *     However, each socket does keep track of its parent printer.
 */
struct printer_connection {
	int fd;			/* File descriptor to printer port. */
};

struct ieee1284_socket {
	struct printer *parent;	/* Whom do I belong to? */
	int fd;			/* File descriptor to printer port. */
	int d4_socket;		/* IEEE-1284.4 socket number. */
};

struct printer {
	struct printer_connection conn;
	const struct printer_info *info;
};

/* Communication primitives. */
int printer_connect(struct printer *printer, const char *raw_device);
int printer_disconnect(struct printer *printer);
int open_channel(struct ieee1284_socket *conn, struct printer *printer,
		 const char* service_name);
int close_channel(struct ieee1284_socket *conn);
int printer_transact(struct ieee1284_socket *d4_sock,
		     const char* buf_send, int send_len,
		     char* buf_recv, int* recv_len);
int printer_request_status(struct ieee1284_socket *socket, char *buf, int *len);
/* -------------------------------- */

/* === information === */
const struct printer_info *printer_model(const char* raw_device);
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
void init_command(fcmd_header_t* cmd, const struct printer_info *info,
		  unsigned char class, unsigned char name,
		  unsigned short int extra_length);

/*
    socket_id - opened IEEE 1284.4 socket for
	        "EPSON-CTRL" service.
    Tries to read one byte form printer's EEPROM address <addr> to <data>.
    On success returns 0.
    On fail returns -1.
*/
int read_eeprom_address(struct ieee1284_socket *d4_sock,
			unsigned short int addr, unsigned char* data);

/*
    socket_id - opened IEEE 1284.4 socket for
	        "EPSON-CTRL" service.
    Tries to write one byte <data> to printer's EEPROM address <addr>.
    On success returns 0.
    On fail returns -1.
*/
int write_eeprom_address(struct ieee1284_socket *d4_sock,
			 unsigned short int addr, unsigned char data);
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
int parse_v1_status_report(const uint8_t *buf, size_t len);
int parse_v2_status_report(const uint8_t *buf, size_t len);
/* --------------- */

/* === main workers === */
int do_ink_levels(const char *raw_device, struct printer *printer);
int do_ink_reset(const char *raw_device, struct printer *printer,
		 unsigned char ink_type);
int do_eeprom_dump(const char *raw_device, struct printer *printer,
		   unsigned short int start_addr, unsigned short int end_addr);
int do_eeprom_write(const char *raw_device, struct printer *printer,
		    unsigned short int addr, unsigned char data);
int do_make_report(const char *raw_device, unsigned char model_code[]);
int do_waste_reset(const char *raw_device, struct printer *printer);
/* -------------------- */

int main(int argc, char** argv)
{
	int opt; 					//current option
	int command = CMD_NONE; 			//command to do

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
	struct printer printer = {
		.conn.fd = -1,
	};

	setDebug(0);
	d4lib_set_debug_fn(reink_do_log);
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
	printer.info = printer_model(raw_device);
	if (is_unknown_printer(printer.info)) {
		reink_log("Unknown printer. Wrong device file?\n");
		return 1;
	}

	switch (command)
	{
	case CMD_GETINK:
		return do_ink_levels(raw_device, &printer);
		break;

	case CMD_DUMPEEPROM:
		return do_eeprom_dump(raw_device, &printer, addr_s, addr_e);
		break;

	case CMD_WRITEEEPROM:
		return do_eeprom_write(raw_device, &printer, write_addr, write_byte);
		break;

	case CMD_ZEROINK:
		return do_ink_reset(raw_device, &printer, ink_type);
		break;
		
	case CMD_ZEROWASTE:
		return do_waste_reset(raw_device, &printer);
		break;

	default:
		reink_log("Unknown command.\n");
		return 1;
	}

	return 0;
}

void print_usage(const char* progname)
{
	reink_log("ReInk v%d.%d.%d (http://reink.lerlan.ru)\n\
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

int do_ink_levels(const char* raw_device, struct printer *printer)
{
	char buf[INPUT_BUF_LEN]; //buffer for input data
	int readed; //number of readed bytes
	int (*do_parse_status_report)(const uint8_t* buf, size_t len);
	struct ieee1284_socket d4_sock;

	reink_dbg("=== do_ink_levels ===\n");

	if (printer_connect(printer, raw_device) < 0)
		return 1;

	if (open_channel(&d4_sock, printer, "EPSON-CTRL") < 0)
		return 1;

	reink_dbg("Everything seems to be ready. :) Let's get ink level. Executing \"st\" command... ");
	readed = INPUT_BUF_LEN;
	if (printer_request_status(&d4_sock, buf, &readed))
		return 1;
	DBG_OK();

	reink_dbg("Parsing result... ");
	if (!strncmp(buf, "@BDC ST2", 8))
		do_parse_status_report = parse_v2_status_report;
	else
		do_parse_status_report = parse_v1_status_report;

	if (do_parse_status_report(buf, readed))
	{
		reink_log("FAIL.\n");
		return 1;
	}
	DBG_OK();

	if (close_channel(&d4_sock) < 0)
		return 1;

	if (printer_disconnect(printer) < 0)
		return 1;

	reink_dbg("^^^ do_ink_levels ^^^\n");
	return 0;
}

int do_ink_reset(const char *raw_device, struct printer *printer,
		 unsigned char ink_type)
{
	int i;
	unsigned char cur_ink;
	const unsigned char *cur_addr;
	struct ieee1284_socket d4_sock;

	reink_dbg("=== do_ink_reset ===\n");
	
	if (is_unknown_printer(printer->info))
		return 1;

	if (printer_connect(printer, raw_device) < 0)
		return 1;

	if (open_channel(&d4_sock, printer, "EPSON-CTRL") < 0)
		return 1;

	for(cur_ink = 1; cur_ink != 0x80; cur_ink <<= 1)
	{
		if (!(cur_ink & ink_type))
			continue;
		
		if (!(printer->info->inkmap.mask & cur_ink))
		{
			if (ink_type == 0xFF) //reset all inks
				continue;
				
			reink_log("Printer \"%s\" doesn't have ink bit %d.\n",
				  printer->info->name, cur_ink);
			return 1;
		}
		
		switch(cur_ink)
		{
		case INK_BLACK:
			cur_addr = printer->info->inkmap.black;
			break;
		case INK_CYAN:
			cur_addr = printer->info->inkmap.cyan;
			break;
		case INK_MAGENTA:
			cur_addr = printer->info->inkmap.magenta;
			break;
		case INK_YELLOW:
			cur_addr = printer->info->inkmap.yellow;
			break;
		case INK_LIGHTCYAN:
			cur_addr = printer->info->inkmap.lightcyan;
			break;
		case INK_LIGHTMAGENTA:
			cur_addr = printer->info->inkmap.lightmagenta;
			break;
		default:
			reink_log("Unknown ink bit %d.\n", cur_ink);
			return 1;
		}
		
		reink_dbg("Resetting ink bit %d... ", cur_ink);
		for (i=0;i<4;i++)
			if (write_eeprom_address(&d4_sock, cur_addr[i], 0x00)) {
				reink_log("Can't write to eeprom.\n");
				return 1;
			}
		DBG_OK();
	}
	
	if (close_channel(&d4_sock) < 0)
		return 1;

	if (printer_disconnect(printer) < 0)
		return 1;

	reink_dbg("^^^ do_ink_reset ^^^\n");

	return 0;
}

int do_eeprom_dump(const char *raw_device, struct printer *printer,
		   unsigned short int start_addr, unsigned short int end_addr)
{
	unsigned char data; //eeprom data (one byte)
	unsigned short int cur_addr; //current address
	int i;
	struct ieee1284_socket d4_sock;

	reink_dbg("=== do_eeprom_dump ===\n");

	if (is_unknown_printer(printer->info))
		return 1;

	if (printer_connect(printer, raw_device) < 0)
		return 1;

	if (open_channel(&d4_sock, printer, "EPSON-CTRL") < 0)
		return 1;

	if (!printer->info->twobyte_addresses && (end_addr & 0xFF00))
	{
		reink_log("Printer \"%s\" doesn't support two-byte addresses, I will use lower byte only.\n",
			  printer->info->name);
		start_addr &= 0xFF;
		end_addr &= 0xFF;
	}

	reink_dbg("Let's get the EEPROM dump (%x - %x)...\n", start_addr, end_addr);

	for (cur_addr = start_addr; (cur_addr <= end_addr) && (cur_addr >= start_addr); cur_addr++)
	{
		if (read_eeprom_address(&d4_sock, cur_addr, &data)) {
			reink_log("Fail to read EEPROM data from address %x.\n", cur_addr);
			return 1;
		}
		printf("0x%04X = 0x%02X\n", cur_addr, data);
		if (cur_addr == end_addr)
			break; //or there may be short int overflow and infinite loop
	}

	DBG_OK();

	if (close_channel(&d4_sock) < 0)
		return 1;

	if (printer_disconnect(printer) < 0)
		return 1;

	reink_dbg("^^^ do_eeprom_dump ^^^\n");
	return 0;
}

int do_eeprom_write(const char *raw_device, struct printer *printer,
		    unsigned short int addr, unsigned char data)
{
	unsigned char readed_data; //verification data
	struct ieee1284_socket d4_sock;

	reink_dbg("=== do_eeprom_write ===\n");

	if (is_unknown_printer(printer->info))
		return 1;

	if (printer_connect(printer, raw_device) < 0)
		return 1;

	if (open_channel(&d4_sock, printer, "EPSON-CTRL") < 0)
		return 1;

	reink_dbg("Let's write %#x to EEPROM address %#x...\n", data, addr);
	if (write_eeprom_address(&d4_sock, addr, data))
	{
		reink_log("Fail to write EEPROM data to address %#x.\n", addr);
		return 1;
	}
	DBG_OK();

	reink_dbg("Verify by reading that byte... ");
	if (read_eeprom_address(&d4_sock, addr, &readed_data))
	{
		reink_log("Fail to subsequent read from EEPROM address %#x.\n", addr);
		return 1;
	}

	if (readed_data != data)
	{
		reink_log("Verification failed (readed byte = %#x).\n", readed_data);
		return 1;
	}
	DBG_OK();

	if (close_channel(&d4_sock) < 0)
		return 1;

	if (printer_disconnect(printer) < 0)
		return 1;

	reink_dbg("^^^ do_eeprom_write ^^^\n");
	return 0;
}

int do_waste_reset(const char *raw_device, struct printer *printer)
{
	int i;
	struct ieee1284_socket d4_sock;

	reink_dbg("=== do_waste_reset ===\n");

	if (is_unknown_printer(printer->info))
		return 1;

	if (printer_connect(printer, raw_device) < 0)
		return 1;

	if (open_channel(&d4_sock, printer, "EPSON-CTRL") < 0)
		return 1;

	reink_dbg("Resetting... ");;
	for (i=0; i < printer->info->wastemap.len; i++)
		if (write_eeprom_address(&d4_sock,
					 printer->info->wastemap.addr[i], 0x00)) {
			reink_log("Can't write to eeprom.\n");
			return 1;
		}
	DBG_OK();

	if (close_channel(&d4_sock) < 0)
		return 1;

	if (printer_disconnect(printer) < 0)
		return 1;

	reink_dbg("^^^ do_waste_reset ^^^\n");

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
	int have_model_code = 0; //do we have model code?
	char buf[INPUT_BUF_LEN]; //buffer for input data
	int readed;	//length of data in input buffer
	unsigned short int caddr; //current address
	unsigned char data; //one byte from eeprom
	int original_stderr;
	int original_debug = ri_debug; //original value of ri_debug
	struct printer_info unknown_printer;
	struct printer prt;
	struct printer *printer = &prt;
	struct ieee1284_socket d4_sock, data_sock;

	printf("ReInk v%d.%d test report.\n", REINK_VERSION_MAJOR, REINK_VERSION_MINOR);

	reink_log("Please, be patient.\nWait at least 10 minutes before force interrupt.\n");

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
	unknown_printer = printers[PM_UNKNOWN];
	printer->info = &unknown_printer;

	//raw_device (r/w status)
	//can enter in ieee1284.4 mode?
	if (printer_connect(printer, raw_device) < 0)
		return 0;

	//opening EPSON-CTRL
	if (open_channel(&d4_sock, printer, "EPSON-CTRL") < 0)
	{
		printer_disconnect(printer);
		return 0;
	}

	//opening EPSON-DATA (just try to open - then close)
	if (open_channel(&data_sock, printer, "EPSON-DATA") >= 0)
		close_channel(&data_sock); //no need anymore

	//reply to "di" command
	readed = INPUT_BUF_LEN;
	if (printer_transact(&d4_sock, "di\1\0\1", 5, buf, &readed) < 0)
	{
		close_channel(&d4_sock);
		printer_disconnect(printer);
		return 0;
	}

	//reply to "st" command
	readed = INPUT_BUF_LEN;
	if (printer_request_status(&d4_sock, buf, &readed) < 0)
	{
		close_channel(&d4_sock);
		printer_disconnect(printer);
		return 0;
	}

	//init unknown printer
	unknown_printer.model_code[0] = model_code[0];
	unknown_printer.model_code[1] = model_code[1];

	//assume first this is two-byte addresses printer
	unknown_printer.twobyte_addresses = 1;

	if (model_code[0] != 0 || model_code[1] != 0)
		have_model_code = 1;
	else
	{
		printf("Searching printer secret model code with brute force.\n");
		fflush(stdout);
	}

	//let's find out the model code
	while (unknown_printer.model_code[0] != 0xFF && !have_model_code)
	{
		//try to read from eeprom (address 00)
		if (read_eeprom_address(&d4_sock, 0x00, &data) == 0)
		{
			printf("We found model code: 0x%02X 0x%02X\n", unknown_printer.model_code[0], unknown_printer.model_code[1]);
			have_model_code = 1;
			break;
		}

		if (unknown_printer.model_code[1]++ == 0xFF)
			unknown_printer.model_code[0]++;

		if (ri_debug && !original_debug)
		{
			//disabling debug to speedup process
			setDebug(0);
			ri_debug = 0;
		}
	}

	if (!have_model_code)
	{
		close_channel(&d4_sock);
		printer_disconnect(printer);
		return 0;
	}

	//on success - dump eeprom 0x00-0xFF

	//reenabling debug to see what we have when read from printer
	setDebug(1);
	ri_debug = 1;

	if (read_eeprom_address(&d4_sock, 0x00, &data) != 0)
	{
		close_channel(&d4_sock);
		printer_disconnect(printer);
		return 0;
	}

	//disabling debug before dump
	setDebug(0);
	ri_debug = 0;


	printf("\nEEPROM DUMP:\n");
	for (caddr = 0; caddr <= 0xFF; caddr++)
	{
		if (read_eeprom_address(&d4_sock, caddr, &data) < 0)
		{
			close_channel(&d4_sock);
			printer_disconnect(printer);
			return 0;
		}
		printf("0x%02X = 0x%02X\n", caddr, data);
	}

	//redirecting stderr back to console
	dup2(original_stderr, fileno(stderr));

	reink_log("Report complete. Thank you.\n");

	if (have_model_code && (model_code[0] == 0 && model_code[1] == 0))
	{
		reink_log("You will provide even more help for me if\n"
			  "you run printer head cleaning now and\n"
			  "then make another report with command:\n"
			  "./reink -r %s -t%02X%02X > ./testreport2.log\n",
			  raw_device, unknown_printer.model_code[0],
			  unknown_printer.model_code[1]);
	}
	else if (model_code[0] != 0 || model_code[1] != 0)
	{
		reink_log("Now send me both reports.\n");
	}

	close_channel(&d4_sock);
	printer_disconnect(printer);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
//	HELPERS
/////////////////////////////////////////////////////////////////////////////////
//

int parse_v1_status_report(const uint8_t *buf, size_t len)
{
	char ink_info[20];
	char ink_val[3];
	int i;
	int val;

	reink_dbg("=== parse_ink_result ===\n");

	reink_dbg("Getting the \"IQ:\" tag... ");;
	if (get_tag(buf, len, "IQ:", ink_info, 20))
	{
		reink_log("Can't find ink levels information in printer answer.\n");
		return 1;
	}
	reink_dbg("OK, have string \"%s\".\n", ink_info);;

	if ((strlen(ink_info)) % 2 != 0)
	{
		reink_log("Malformed output in printer answer.\n");
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

	reink_dbg("^^^ parse_ink_result ^^^\n");

	return 0;
}

static const char *ink_color_name(size_t idx)
{
	static const char *color_names[] = {
		[0x00] = "Black",
		[0x01] = "Photo Black",
		"Unknown",
		[0x03] = "Cyan",
		[0x04] = "Magenta",
		[0x05] = "Yellow",
		[0x06] = "Light Cyan",
		[0x07] = "Light Magenta",
		"Unknown",
		"Unknown",
		[0x0a] = "Light Black",
		[0x0b] = "Matte Black",
		[0x0c] = "Red",
		[0x0d] = "Blue",
		[0x0e] = "Gloss Optimizer",
		[0x0f] = "Light Light Black",
		[0x10] = "Orange",
	};

	return (idx > ARRAY_SIZE(color_names)) ? "ERROR" : color_names[idx];
}

static const char *ink_aux_color_name(size_t idx)
{
	static const char *aux_color_names[] = {
		[0x00] = "Black",
		[0x01] = "Cyan",
		[0x02] = "Magenta",
		[0x03] = "Yellow",
		[0x04] = "Light Cyan",
		[0x05] = "Light Magenta",
		"Unknown",
		"Unknown",
		[0x09] = "Red",
		[0x0a] = "Blue",
		"Unknown",
		"Unknown",
		[0x0d] = "Orange",
		"Unknown",
		"Unknown",
	};

	return (idx > ARRAY_SIZE(aux_color_names)) ? "ERROR"
		: aux_color_names[idx];
}

static int report_inks_levels_v2(const uint8_t* buf, size_t len)
{
	uint8_t entry_size, num_colors, i, idx_name, idx_aux, level;

	entry_size = buf[0];
	if (entry_size != 3) {
		reink_log("Expected 3-byte ink level entry, got %d instead\n",
			  entry_size);
		return -1;
	}

	num_colors = (len - 1) / entry_size;
	if ((len - 1) % entry_size)
		reink_log("Ink level entries not multiple of message size\n");

	buf ++;
	reink_log("Reported ink levels:\n");
	for (i = 0; i < num_colors; i++) {
		idx_name = buf[0];
		idx_aux = buf[1];
		level = buf[2];

		/* The funky format string is just for aligning the output. */
		reink_log("\t%-16saux - %-16s%4d\%\n", ink_color_name(idx_name),
			  ink_aux_color_name(idx_aux), level);

		buf += entry_size;
	}
}

int parse_v2_status_report(const uint8_t* buf, size_t len)
{
	uint8_t msg_type, msg_size;
	const uint8_t *msg;
	const uint8_t num_pork_bytes = 12;

	if (len < num_pork_bytes)
		return -1;

	len -= num_pork_bytes;
	buf += num_pork_bytes;

	while (len) {
		msg_type = buf[0];
		msg_size = buf[1];
		msg = buf + 2;

		if ((msg_size + 2) > len) {
			reink_log("Malformed status report\n");
			return -1;
		}

		switch (msg_type) {
		case 0x01:
			reink_dbg("Printer status code: %x\n", msg[0]);
			break;
		case 0x0f:
			report_inks_levels_v2(msg, msg_size);
			break;
		default:
			reink_dbg("Unknown report type %x in status message\n",
				  msg_type);
		}

		buf += msg_size + 2;
		len -= msg_size + 2;
	}

	return 0;
}

int get_tag(const char* source, int source_len, const char* tag, char* value, int max_value_len)
{
	int tag_len;
	int val_len;
	const char *semicol;

	reink_dbg("=== get_tag ===\n");

	reink_dbg("Searching for \"%s\" substring... ", tag);

	tag_len = strlen(tag);
	/* Can't use strstr(), since source is not NULL-terminated. */
	while ((tag_len < source_len) && (strncmp(source, tag, tag_len))) {
		source++;
		source_len--;
	}

	if (tag_len == source_len) {
		reink_dbg("NOT FOUND.\n");
		return -1;
	}

	reink_dbg("FOUND\n");

	source += tag_len;

	reink_dbg("Searching for \";\" character... ");

	semicol = memchr(source, ';', source_len);
	if (!semicol) {
		reink_dbg("NOT FOUND.\n");
		return -1;
	}

	val_len = (intptr_t)(semicol - source);
	reink_dbg("FOUND, len=%d.\n", val_len);

	if (val_len > max_value_len - 1) {
		reink_dbg("Value(+'\\0') too long (%d) for given buffer (%d).\n", val_len+1, max_value_len);
		return 1;
	}

	strncpy(value, source, val_len);
	value[val_len] = '\0';
	reink_dbg("Tag value:\"%s\".\n", value);

	reink_dbg("^^^ get_tag ^^^\n");

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////
//	INFORMATION
/////////////////////////////////////////////////////////////////////////////////
//

const struct printer_info *printer_model(const char* raw_device)
{
	unsigned int i;
	struct printer printer;
	const struct printer_info *unknown = &printers[PM_UNKNOWN];

	char buf[INPUT_BUF_LEN]; //buffer for input data
	int readed; //number of readed bytes

	char strModel[MAX_MODEL_LEN];
	struct ieee1284_socket d4_sock;

	reink_dbg("=== printer_model ===\n");

	if (printer_connect(&printer, raw_device) < 0)
		return unknown;

	if (open_channel(&d4_sock, &printer, "EPSON-CTRL") < 0)
		return unknown;

	reink_dbg("Let's get printer info. Executing \"di\" command... ");
	readed = INPUT_BUF_LEN;
	if (printer_transact(&d4_sock, "di\1\0\1", 5, buf, &readed))
		return unknown;
	DBG_OK();

	reink_dbg("Parsing result... ");
	if (get_tag(buf, readed, "MDL:", strModel, MAX_MODEL_LEN))
	{
		reink_dbg("Parse failed.\n");
		return unknown;
	}
	DBG_OK();

	for(i = 0; i < printers_count; i++)
	{
		if (!strcmp(strModel, printers[i].model_name))
		{
			reink_dbg("Printer \"%s\".\n", printers[i].name);
			printer.info = &printers[i];
			break;
		}
	}
	
	if (close_channel(&d4_sock) < 0)
		return printer.info;

	if (printer_disconnect(&printer) < 0)
		return printer.info;

	reink_dbg("^^^ printer_model ^^^\n");
	return printer.info;
}

/////////////////////////////////////////////////////////////////////////////////
//	PROTOCOL, CHANNEL INITIALIZATION, FINILIZING
/////////////////////////////////////////////////////////////////////////////////
//

int printer_connect(struct printer *printer, const char *raw_device)
{
	reink_dbg("=== printer_connect ===\n");

	reink_dbg("Opening raw device... ");
	printer->conn.fd = open(raw_device, O_RDWR | O_SYNC);
	if (printer->conn.fd < 0) {
		reink_log("Error opening device file '%s': %s\n", raw_device, strerror(errno));
		return -1;
	}
	DBG_OK();

	clearSndBuf(printer->conn.fd); //if there are some data from previous incoreectly terminated session

	reink_dbg("Entering IEEE 1284.4 mode... ");
	if (!EnterIEEE(printer->conn.fd))
	{
		reink_log("Can't enter in IEEE 1284.4 mode. Wrong printer device file?\n");
		return -1;
	}
	DBG_OK();

	reink_dbg("Perfoming IEEE 1284.4 Init transaction... ");
	if (!Init(printer->conn.fd))
	{
		reink_log("IEEE 1284.4: \"Init\" transaction failed.\n");
		return -1;
	}
	DBG_OK();

	reink_dbg("^^^ printer_connect ^^^\n");

	return 0;
}

int printer_disconnect(struct printer *printer)
{
	int ret;

	reink_dbg("=== printer_disconnect ===\n");

	reink_dbg("Perfoming IEEE 1284.4 Exit transaction... ");
	if (!Exit(printer->conn.fd))
	{
		reink_log("IEEE 1284.4: \"Exit\" transaction failed.\n");
		return -1;
	}
	DBG_OK();

	reink_dbg("Closing raw device... ");
	if (close(printer->conn.fd) < 0) {
		reink_log("Error closing printer device file: %s\n", strerror(errno));
		return -1;
	}
	DBG_OK();

	reink_dbg("^^^ printer_disconnect ^^^\n");

	return 0;
}


int open_channel(struct ieee1284_socket *d4_sock, struct printer *printer,
		 const char* service_name)
{
	int ret;
	int max_send_packet = 0x0200; //maximum size of PC to printer packet (this value may be changed by the printer while opening a channel)
	int max_recv_packet = 0x0200; //maximum size of printer to PC packet (this value may be changed by the printer while opening a channel)

	reink_dbg("=== open_channel ===\n");

	d4_sock->fd = printer->conn.fd;
	d4_sock->parent = printer;

	reink_dbg("Obtaining IEEE 1284.4 socket for \"%s\" service... ", service_name);
	d4_sock->d4_socket = GetSocketID(d4_sock->fd, service_name);
	if (!d4_sock->d4_socket) {
		reink_log("IEEE 1284.4: \"GetSocketID\" transaction failed.\n");
		return -1;
	}
	reink_dbg("OK, socket=%d.\n", d4_sock->d4_socket);

	reink_dbg("Opening IEEE 1284.4 channel %d... ", d4_sock->d4_socket);
	ret = OpenChannel(d4_sock->fd, d4_sock->d4_socket,
			  &max_send_packet, &max_recv_packet);
	if (ret != 1) {
		reink_log("IEEE 1284.4: \"OpenChannel\" transaction failed.\n");
		return -1;
	}
	DBG_OK();

	reink_dbg("^^^ open_channel ^^^\n");

	return 0;
}

int close_channel(struct ieee1284_socket *d4_sock)
{
	reink_dbg("=== close_channel ===\n");

	reink_dbg("Closing IEEE 1284.4 channel %d... ", d4_sock->d4_socket);
	if (1 != CloseChannel(d4_sock->fd, d4_sock->d4_socket))
	{
		reink_log("IEEE 1284.4: \"CloseChannel\" transaction failed.\n");
		return -1;
	}
	DBG_OK();

	reink_dbg("^^^ close_channel ^^^\n");

	return 0;
}

int printer_transact(struct ieee1284_socket *d4,
		     const char* buf_send, int send_len,
		     char* buf_recv, int* recv_len)
{
	int credits = 0; //count of ieee1284.4 credits I have left
	int buf_len;	//the length of recieve buffer

	reink_dbg("=== printer_transact ===\n");

	buf_len = *recv_len;

	reink_dbg("Requesting some IEEE 1284.4 credits on channel %d... ",
		  d4->d4_socket);
	credits = CreditRequest(d4->fd, d4->d4_socket);
	if (credits < 1)
	{
		reink_log("IEEE 1284.4: \"CreditRequest\" transaction failed.\n");
		return -1;
	}
	reink_dbg("OK, got %d credits.\n", credits);

	/* // made automatically by readData later
	reink_dbg("Giving one IEEE 1284.4 credit to printer on channel %d-%d... ", ctrl_socket, ctrl_socket))
	if (1 != Credit(printer, 1))
	{
	reink_log("IEEE 1284.4: \"Credit\" transaction failed.\n");
	return -1;
	}
	DBG_OK();
	*/

	reink_dbg("Writing data to printer... ");
	if (writeData(d4->fd, d4->d4_socket, buf_send, send_len, 0) < send_len)
	{
		reink_log("IEEE 1284.4: Error sending data to channel %d.\n",
			  d4->d4_socket);
		return -1;
	}
	DBG_OK();

	reink_dbg("Get the answer... ");
	*recv_len = readData(d4->fd, d4->d4_socket, buf_recv, buf_len);
	if (*recv_len < 0) {
		reink_log("IEEE 1284.4: Error recieving data from channel %d.\n",
			  d4->d4_socket);
		return -1;
	}
	DBG_OK();

	reink_dbg("^^^ printer_transact ^^^\n");

	return 0;
}

int printer_request_status(struct ieee1284_socket *d4_sock, char *buf, int *len)
{
	return printer_transact(d4_sock, "st\1\0\1", 5, buf, len);
}

/////////////////////////////////////////////////////////////////////////////////
//	EPSON FACTORY COMMANDS
/////////////////////////////////////////////////////////////////////////////////

void init_command(fcmd_header_t* cmd, const struct printer_info *info,
		  unsigned char class, unsigned char name,
		  unsigned short int extra_length)
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

	cmd->mcode1 = info->model_code[0];
	cmd->mcode2 = info->model_code[1];
}

int read_eeprom_address(struct ieee1284_socket *d4_sock,
			unsigned short int addr, unsigned char* data)
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

	reink_dbg("=== read_eeprom_address ===\n");

	cmd[9] = addr & 0xFF;
	if (d4_sock->parent->info->twobyte_addresses)
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
			reink_dbg("Printer \"%s\" don't support two-byte addresses. Continuing using low byte only.\n", d4_sock->parent->info->name);
			addr = addr & 0xFF;
		}
	}

	init_command((fcmd_header_t*)cmd, d4_sock->parent->info,
		     EFCLS_EEPROM_READ, EFCMD_EEPROM_READ, cmd_args_count);

	reink_dbg("Reading eeprom address %#x... ", addr);
	actual = INPUT_BUF_LEN;
	if (printer_transact(d4_sock, cmd, cmd_len, reply, &actual))
	{
		reink_dbg("Transact failed.\n");
		return -1;
	}

	if (get_tag(reply, actual, "EE:", reply_data, 7))
	{
		reink_dbg("Can't get reply data.\n");
		return -1;
	}
	DBG_OK();

	if (strlen(reply_data) != reply_data_len)
	{
		reink_dbg("ReplyData length != %d\n", reply_data_len);
		reply_data_len -= 2; //assuming this is one-byte addresses printer
		if (strlen(reply_data) == reply_data_len)
		{
			reink_dbg("Seems like printer with one-byte addresses EEPROM.\n");
		}
		else
			return -1;
	}

	strncpy(onebyte, reply_data, reply_data_len - 2);
	onebyte[reply_data_len - 2] = '\0';

	replyaddr = strtol(onebyte, NULL, 16);
	if (replyaddr != addr)
	{
		reink_dbg("Reply address (%x) don't match requested (%x).\n", replyaddr, addr);
		return -1;
	}

	strncpy(onebyte, reply_data + reply_data_len - 2, 2); //the data itself
	onebyte[2] = '\0';
	*data = strtol(onebyte, NULL, 16);

	reink_dbg("EEPROM addr %#x = %#x.\n", addr, *data);

	reink_dbg("^^^ read_eeprom_address ^^^\n");

	return 0;
}

int write_eeprom_address(struct ieee1284_socket *d4_sock,
			 unsigned short int addr, unsigned char data)
{
	char cmd[12]; // full command with address
	int cmd_len = 11; // full length of the command
	int cmd_args_len = 2; // command arguments count

	char reply[INPUT_BUF_LEN]; // buffer for printer reply
	int actual; // actual reply length
	char reply_data[6]; // buffer for "OK" tag

	reink_dbg("=== write_eeprom_address ===\n");

	cmd[9] = addr & 0xFF;
	if (d4_sock->parent->info->twobyte_addresses)
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
			reink_dbg("Printer \"%s\" don't support two-byte addresses. Continuing using low byte only.\n", d4_sock->parent->info->name);
	}

	init_command((fcmd_header_t*)cmd, d4_sock->parent->info,
		     EFCLS_EEPROM_WRITE, EFCMD_EEPROM_WRITE, cmd_args_len);

	reink_dbg("Writing %#x to eeprom address %#x... ", data, addr);
	actual = INPUT_BUF_LEN;
	if (printer_transact(d4_sock, cmd, cmd_len, reply, &actual))
	{
		reink_dbg("Transact failed.\n");
		return -1;
	}

	if (get_tag(reply, actual, "OK", reply_data, 6))
	{
		reink_dbg("Can't get reply data.\n");
		return -1;
	}
	DBG_OK();

	reink_dbg("^^^ write_eeprom_address ^^^\n");

	return 0;
}
