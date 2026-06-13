Midex hardware components

PID 0x1000 - without ROM
  1x EZ-USB AN2131QC 0004 ENY0498 = micro processor
  2x ST16C454CJ DC 0022 = 4x UART
  1x CY62256VLL-70SNC PHI 0031 631106 D 04 = 256kbit (32kbit * 8) low-power SRAM (cypress semiconductor)
  1x PC849 SHARP = opto coupler
  1x Microchip 24LC00/P CAN 9846 = EEPROM 128 bit / 16 * 8
  1x PALLV16V8-10PC 99400BA B = Electrically-erasable CMOS Programmable Array Logic (PAL) 
  2x 05CNXHK HC04
  1x 74HC138D = 3-to-8 line demux (inverting)
  1x 74HC123D = dual retriggerable monostable multivibrator with reset and pulse-width modulation 
  1x 74HC04 = Hex inverter
  3x 74HC245D D7245ME = 8-bit transceiver with 3-state outputs
  1x 8746H HC74A = dual D flip-flop
  Serial: BEK 040 0031 27 006 SHTIFQ 02678

PID 0x1010 - with ROM - R2 COMP
  1x EZ-USB FX CY7C64613-126NC 0121 CNZ4052 = microprocessor
  2x 74HC245D D7245ME Hnn 0044E = 8-bit transceiver with 3-state outputs
  1x 74HC123D E1214 12 Unn0148 D = dual retriggerable monostable multivibrator with reset and pulse-width modulation
  1x 74HC03 90X213 = Quad 2-input NAND gate; open-drain output
  1x ST16C454CJ DC 0022 = 4x UART
  1x ST16C452CJ DC 0147 = 2x UART
  24LC64 I/P 346 = EEPROM 64kbit / 8k * 8
  ALLIANCE AS7C3256-12JC 0101 34118B = 256kbit (32kbit * 8) high-speed, asynchronous CMOS SRAM
  ...
  Serial: BEK 040 0035 26 201 SHTIFQ 06691



PID 0x1001 - with latest firmware uploaded

## r1
ST16C454CJ
- D1 = PIN 67 = connected to both ST16C454CJ and to the D1 pin of the EZUSB.
- D2 = PIN 68 = connected to both ST16C454CJ and to the D2 pin of the EZUSB.
- IOW = PIN 18 - connected to both ST16C454CJ and to the Output Enable pin of the PALL16V8. Also to the PC6/WR# pin of EZUSB
- IOR = PIN 52 - connected to both ST16C454CJ and to the PC7/RD# pin of the EZUSB.
- 16/-68 = PIN 31 - pulled down with 2.7kOhm on the first of the 2 ST16C454CJ, with 66kOhm on the second (which could thus be pulled up by the internal resistor)
- INTC = pin 49 -> AN2131 pin 70 PA2/EO#
- INTD = pin 55 -> AN2131 pin 71 PA3/CS#

second ST16C454CJ
- INTD = pin 55 -> AN2131 pin 76 PA7/RXD1 out

PALLV16
- some of the input pins seem connected to the SRAM
- other input pins likely connected to EZUSB but untraceable.
- pin 2 (i1) -> TH138D pin 5 (E2) -> 
- pin 5 (i4) -> TH138D pin 6 (E3) -> SRAM pin 4
- pin 6 (i5) -> ST16C454CJ pin 37 (RESET) --> EZUSB pin 52 (PB4/int4)
- pin 7 (i6) -> ST16C454CJ pin 7 (RXA)
- pin 28 (i/o6) -> SRAM pin 22 (OE) 

TH138D
- Pin 15 (Y0) -> st16c CSA (first ST16C)
- Pin 14 (Y1) -> st16c CSB (first ST16C)
- Pin 13 (Y2) -> st16c CSC (first ST16C)
- Pin 12 (Y3) -> st16c CSD (first ST16C)
- Pin 11 (Y4) -> st16c CSA (second ST16C)
- Pin 10 (Y5) -> st16c CSB (second ST16C)
- Pin 09 (Y6) -> st16c CSC (second ST16C)
- Pin 07 (Y7) -> st16c CSD (second ST16C)


## r2

ST16C454CJ
- pin 35 (XTAL1) - looks connected to ST16C452CJ CLK
- pin 36 (XTAL2) - NC?

74HC138D
- Pin 15 (Y0) -> st16c CSA (first ST16C)
- Pin 14 (Y1) -> st16c CSB (first ST16C)
- Pin 13 (Y2) -> st16c CSC (first ST16C)
- Pin 12 (Y3) -> st16c CSD (first ST16C)
- Pin 11 (Y4) -> st16c452 CSA (second ST16C)
- Pin 10 (Y5) -> st16c452 CSB (second ST16C)
- Pin 09 (Y6) -> st16c452 ? (second ST16C)
- Pin 07 (Y7) -> st16c452 ? (second ST16C)

Alliance AS
