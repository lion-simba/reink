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
 
 
//printer models 
#define PM_UNKNOWN		0	//Unknown model
#define PM_SP790		1	//EPSON Stylus Photo 790
#define PM_SC580		2	//EPSON Stylus Color 580
#define PM_SP1290		3	//EPSON Stylus Photo 1290
#define PM_SC680		4	//EPSON Stylus Color 680
#define PM_SPT50		5	//EPSON Stylus Photo T50
#define PM_SPP50		6	//EPSON Stylus Photo P50

//ink counters EEPROM map
#define INK_BLACK			0x01
#define INK_CYAN			0x02
#define INK_MAGENTA			0x04
#define INK_YELLOW			0x08
#define INK_LIGHTCYAN		0x10
#define INK_LIGHTMAGENTA	0x20
typedef struct _ink_map {
	unsigned int  mask; //bit sum of INK_ defines of available inks	
	unsigned char black[4];
	unsigned char cyan[4];
	unsigned char magenta[4];
	unsigned char yellow[4];
	unsigned char lightcyan[4];
	unsigned char lightmagenta[4];
} ink_map_t;

//waste counter EEPROM map
typedef struct _waste_map {
	unsigned char len;		//count of used bytes from addr
	unsigned char addr[4];
} waste_map_t;

//the printer
#define MAX_NAME_LEN	100
#define MAX_MODEL_LEN	100
typedef struct _printer_s {
	unsigned char name[MAX_NAME_LEN];			//printer textual name
	unsigned char model_name[MAX_MODEL_LEN];	//printer model name as returned by printer itself
	unsigned char model_code[2];	//"password" for this printer
	int twobyte_addresses;			//is printer's EEPROM uses two-byte addresses?
	ink_map_t inkmap;
	waste_map_t wastemap;
} printer_t;

extern printer_t printers[];
extern const unsigned int printers_count;
