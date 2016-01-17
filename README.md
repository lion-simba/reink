# ReInk
An utility to reset Epson printer ink counters.

## About

ReInk - is an **experimental** utility to reset ink level in new chiped *Epson*
 cartridges programmatically through regular printer interface (i.e. without
 any additional hardware controllers).

This utility is supposed to be an Open Source alternative to Windows program
[SSC Service Utility](http://www.ssclg.com/epsone.shtml)

## Disclaimer

THIS UTILITY MAY DAMAGE YOUR PRINTER !!!

USE CAREFULLY AND AT YOUR OWN RISK.

## Features
To find out features of current ReInk version look at the source code. :)

Major functions list:
* Infrastructure for communicating with printer by means of IEEE 1284.4 protocol (initializing, opening/closing
channels, service discovery, data transmission and so on).
* Read from arbitary EEPROM address for supported printers.
* Write to arbitary EEPROM address for supported printers.
* Get ink levels for every EPSON Stylus printer I think (almost "copied" from [escputil](http://gimp-print.sourceforge.net/)).
* Reset ink levels for supported printers.
* Reset waste ink counter for supported printers.
* Can generate test reports, containing information about printer.

## Supported printers
* EPSON Stylus Photo 790
* EPSON Stylus Color 580
* EPSON Stylus Photo 1290
* EPSON Stylus Color 680
* EPSON Stylus Photo T50
* EPSON Stylus Photo P50

## How to get my printer supported?
See [here](http://lion-simba.github.io/reink/report.html).

## Usage
Compile (`make`) and run (`./reink`) the program without arguments. :)
In most cases you would need to power-off and then power-on your printer after reseting ink level. This will allow printer
to save new data in the cartridges.
