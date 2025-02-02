/*
  MCP2515.cpp - Library for Microchip MCP2515 CAN Controller
  
  Author: David Harding
  Modification: Collin Kidder		-can_common integration
  Further Modification: Wim Boone	-port to atmel ASF4 framework
  
  Created: 11/08/2010
  
  For further information see:
  
  http://ww1.microchip.com/downloads/en/DeviceDoc/21801e.pdf
  http://en.wikipedia.org/wiki/CAN_bus


  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "MCP2515.h"
#include "MCP2515_defs.h"

MCP2515::MCP2515(uint8_t CS_Pin, uint8_t INT_Pin)
{
	numfilt_init(6); // quickfix
	gpio_set_pin_direction(CS_Pin, GPIO_DIRECTION_OUT);
	gpio_set_pin_level(CS_Pin,true);
	gpio_set_pin_direction(INT_Pin,GPIO_DIRECTION_IN);
	gpio_set_pin_pull_mode(INT_Pin,GPIO_PULL_UP);

	_CS = CS_Pin;
	_INT = INT_Pin;
	
	savedBaud = 0;
	savedFreq = 0;
	running = 0;
	
	InitBuffers();
}

//set all buffer counters to zero to reset them
void MCP2515::InitBuffers() {
	rx_frame_read_pos = 0;
	rx_frame_write_pos = 0;
	tx_frame_read_pos = 0;
	tx_frame_write_pos = 0;
}  

/*
  Initialize MCP2515
  
  int CAN_Bus_Speed = transfer speed in kbps (or raw CAN speed in bits per second)
  int Freq = MCP2515 oscillator frequency in MHz
  int SJW = Synchronization Jump Width Length bits - 1 to 4 (see data sheet)
  
  returns baud rate set
  
  Sending a bus speed of 0 kbps initiates AutoBaud and returns zero if no
  baud rate could be determined.  There must be two other active nodes on the bus!
*/
int MCP2515::Init(uint32_t CAN_Bus_Speed, uint8_t Freq) {
  if(CAN_Bus_Speed>0) {
    if(_init(CAN_Bus_Speed, Freq, 1, false)) {
		savedBaud = CAN_Bus_Speed;
		savedFreq = Freq;
		running = 1;
	    return CAN_Bus_Speed;
    }
  } else {
	int i=0;
	uint8_t interruptFlags = 0;
	for(i=5; i<1000; i=i+5) {
	  if(_init(i, Freq, 1, true)) {
		// check for bus activity
		Write(CANINTF,0);
		delay_ms(500); // need the bus to be communicating within this time frame
		if(Interrupt()) {
		  // determine which interrupt flags have been set
		  interruptFlags = Read(CANINTF);
		  if(!(interruptFlags & MERRF)) {
		    // to get here we must have received something without errors
		    Mode(MODE_NORMAL);
			savedBaud = i;
			savedFreq = Freq;	
			running = 1;
			return i;
		  }
		}
	  }
	}
  }
  return 0;
}

int MCP2515::Init(uint32_t CAN_Bus_Speed, uint8_t Freq, uint8_t SJW) {
  if(SJW < 1) SJW = 1;
  if(SJW > 4) SJW = 4;
  if(CAN_Bus_Speed>0) {
    if(_init(CAN_Bus_Speed, Freq, SJW, false)) {
		savedBaud = CAN_Bus_Speed;
		savedFreq = Freq;
		running = 1;
	    return CAN_Bus_Speed;
    }
  } else {
	int i=0;
	uint8_t interruptFlags = 0;
	for(i=5; i<1000; i=i+5) {
	  if(_init(i, Freq, SJW, true)) {
		// check for bus activity
		Write(CANINTF,0);
		delay_ms(500); // need the bus to be communicating within this time frame
		if(Interrupt()) {
		  // determine which interrupt flags have been set
		  interruptFlags = Read(CANINTF);
		  if(!(interruptFlags & MERRF)) {
		    // to get here we must have received something without errors
		    Mode(MODE_NORMAL);
			savedBaud = i;
			savedFreq = Freq;
			running = 1;
			return i;
		  }
		}
	  }
	}
  }
  return 0;
}

bool MCP2515::_init(uint32_t CAN_Bus_Speed, uint8_t Freq, uint8_t SJW, bool autoBaud) {
  
  // Reset MCP2515 which puts it in configuration mode
  Reset();
  //delay(2);

  Write(CANINTE,0); //disable all interrupts during init
  Write(TXB0CTRL, 0); //reset transmit control
  Write(TXB1CTRL, 0); //reset transmit control
  Write(TXB2CTRL, 0); //reset transmit control
  
  // Calculate bit timing registers
  uint8_t BRP;
  float TQ;
  uint8_t BT;
  float tempBT;
  float freqMhz = Freq * 1000000.0;
  float bestMatchf = 10.0;
  int bestMatchIdx = 10;
  float savedBT;

  float speed = CAN_Bus_Speed;
  if (speed > 5000.0) speed *= 0.001;

  float NBT = 1.0 / (speed * 1000.0); // Nominal Bit Time - How long a single CAN bit should take

  //Now try each divisor to see which can most closely match the target.
  for(BRP=0; BRP < 63; BRP++) {
    TQ = 2.0 * (float)(BRP + 1) / (float)freqMhz;
    tempBT = NBT / TQ;
#ifdef DEBUG_SETUP
    SerialUSB.print("BRP: ");
    SerialUSB.print(BRP);
    SerialUSB.print("  tempBT: ");
    SerialUSB.println(tempBT);
#endif
    BT = (int)tempBT;
    if ( (tempBT - BT) < bestMatchf)
    {
        if (BT > 7 && BT < 25)
        {
            bestMatchf = (tempBT - BT);
            bestMatchIdx = BRP;
            savedBT = BT;
        }
    }
  }

  BT = savedBT;
  BRP = bestMatchIdx;
#ifdef DEBUG_SETUP  
  SerialUSB.print("BRP: ");
  SerialUSB.print(BRP);
  SerialUSB.print("  BT: ");
  SerialUSB.println(BT);
#endif
  
  uint8_t SPT = (0.7 * BT); // Sample point
  uint8_t PRSEG = (SPT - 1) / 2;
  uint8_t PHSEG1 = SPT - PRSEG - 1;
  uint8_t PHSEG2 = BT - PHSEG1 - PRSEG - 1;
#ifdef DEBUG_SETUP
  SerialUSB.print("PROP: ");
  SerialUSB.print(PRSEG);
  SerialUSB.print("  SEG1: ");
  SerialUSB.print(PHSEG1);
  SerialUSB.print("  SEG2: ");
  SerialUSB.println(PHSEG2);
#endif
  // Programming requirements
  if(PRSEG + PHSEG1 < PHSEG2) 
  {
      //SerialUSB.println("PRSEG + PHSEG1 less than PHSEG2!");
      return false;
  }
  if(PHSEG2 <= SJW) 
  {
      //SerialUSB.println("PHSEG2 less than SJW");
      return false;
  }
  
  uint8_t BTLMODE = 1;
  uint8_t SAMPLE = 0;
  
  // Set registers
  uint8_t data = (((SJW-1) << 6) | BRP);
  Write(CNF1, data);
  Write(CNF2, ((BTLMODE << 7) | (SAMPLE << 6) | ((PHSEG1-1) << 3) | (PRSEG-1)));
  Write(CNF3, (0b10000000 | (PHSEG2-1)));
  Write(TXRTSCTRL,0);

  Write(CNF1, data);
  delay_ms(1);
  
  if(!autoBaud) {
    // Return to Normal mode
    if(!Mode(MODE_NORMAL)) 
    {
        //SerialUSB.println("Could not enter normal mode");
        return false;
    }
  } else {
    // Set to Listen Only mode
    if(!Mode(MODE_LISTEN)) 
    {
        //SerialUSB.println("Could not enter listen only mode");
        return false;
    }
  }
  // Enable all interupts
  Write(CANINTE,255);
  
  // Test that we can read back from the MCP2515 what we wrote to it
  uint8_t rtn = Read(CNF1);
  if (rtn == data) return true;
  else 
  {
    //SerialUSB.println(data, HEX);
    //SerialUSB.println(rtn, HEX);
    return false;
  }

  return false;
}

uint16_t MCP2515::available()
{
	int val;
	val = rx_frame_read_pos - rx_frame_write_pos;
	//Now, because this is a cyclic buffer it is possible that the ordering was reversed
	//So, handle that case
	if (val < 0) val += 8;
}

int MCP2515::_setFilter(uint32_t id, uint32_t mask, bool extended)
{
    uint32_t filterVal;
    bool isExtended;

    for (int i = 0; i < 6; i++)
    {
        GetRXFilter(i, filterVal, isExtended);
        if (filterVal == 0) //empty filter. Let's fill it and leave
        {
            if (i < 2)
            {
                GetRXMask(RXMASK0, filterVal);
                if (filterVal == 0) filterVal = mask;
                filterVal &= mask;
                SetRXMask(RXMASK0, filterVal);
            }
            else
            {
                GetRXMask(RXMASK1, filterVal);
                if (filterVal == 0) filterVal = mask;
                filterVal &= mask;
                SetRXMask(RXMASK1, filterVal);
            }
            if (i < 3)
                SetRXFilter(FILTER0 + (i * 4), id, extended);
            else
                SetRXFilter(FILTER3 + ((i-3) * 4), id, extended);
            return i;
        }
    }

    //if we got here then there were no free filters. Return value of deaaaaath!
    return -1;
}

//we don't exactly have mailboxes, we have filters (6 of them) but it's the same basic idea
int MCP2515::_setFilterSpecific(uint8_t mailbox, uint32_t id, uint32_t mask, bool extended)
{
    uint32_t oldMask;
    if (mailbox < 2) //MASK0
    {
        GetRXMask(RXMASK0, oldMask);
        oldMask &= mask;
        SetRXMask(RXMASK0, oldMask);
        
    }
    else //MASK1
    {
        GetRXMask(RXMASK1, oldMask);
        oldMask &= mask;
        SetRXMask(RXMASK0, oldMask);
        
    }
    if (mailbox < 3)
        SetRXFilter(FILTER0 + (mailbox * 4), id, extended);
    else
        SetRXFilter(FILTER3 + ((mailbox-3) * 4), id, extended);
}

uint32_t MCP2515::init(uint32_t ul_baudrate)
{
    Init(ul_baudrate, 16);
}

uint32_t MCP2515::beginAutoSpeed()
{
    Init(0, 16);
}

uint32_t MCP2515::set_baudrate(uint32_t ul_baudrate)
{
    Init(ul_baudrate, 16);
}

void MCP2515::setListenOnlyMode(bool state)
{
    if (state)
        Mode(MODE_LISTEN);
    else Mode(MODE_NORMAL);
}

void MCP2515::enable()
{
    Mode(MODE_NORMAL);
}

void MCP2515::disable()
{
    Mode(MODE_SLEEP);
}

bool MCP2515::sendFrame(CAN_FRAME& txFrame)
{
    EnqueueTX(txFrame);
}

bool MCP2515::rx_avail()
{
    return available()>0?true:false;
}

uint32_t MCP2515::get_rx_buff(CAN_FRAME &msg)
{
    return GetRXFrame(msg);
}

void MCP2515::Reset() {
	gpio_set_pin_level(_CS,false);
	uint8_t command = CAN_RESET;
	io_write(spi_0, &command, 1);
	gpio_set_pin_level(_CS,true);
}

uint8_t MCP2515::Read(uint8_t address) {
	gpio_set_pin_level(_CS, false);
	
	uint8_t command = CAN_READ;
	io_write(spi_0, &command, 1);
	io_write(spi_0, &address, 1);
	
	uint8_t data = 0;
	io_read(spi_0, &data, 1);
  
	gpio_set_pin_level(_CS, true);
	return data;
}

void MCP2515::Read(uint8_t address, uint8_t data[], uint8_t bytes) {
	// allows for sequential reading of registers starting at address - see data sheet
	gpio_set_pin_level(_CS, false);
	
	uint8_t command = CAN_READ;
	io_write(spi_0, &command, 1);
	io_write(spi_0, &address, 1);
	io_read(spi_0, data, bytes);
	
	gpio_set_pin_level(_CS, true);
}

CAN_FRAME MCP2515::ReadBuffer(uint8_t buffer) {
 
	// Reads an entire RX buffer.
	// buffer should be either RXB0 or RXB1
	CAN_FRAME message;
	
	uint8_t command = CAN_READ_BUFFER | (buffer<<1);

	gpio_set_pin_level(_CS, false);
	io_write(spi_0, &command, 1);
	
	uint8_t bytes[5];
	//byte1 RXBnSIDH
	//byte2 RXBnSIDL
	//byte3 RXBnEID8
	//byte4 RXBnEID0
	//byte5 RXBnDLC
	
	io_read(spi_0, bytes, 5);

	message.extended = (bytes[1] & 0b00001000);

	if(message.extended) {
		message.id = (bytes[0]>>3);
		message.id = (message.id<<8) | ((bytes[0]<<5) | ((bytes[1]>>5)<<2) | (bytes[1] & 0b00000011));
		message.id = (message.id<<8) | bytes[2];
		message.id = (message.id<<8) | bytes[3];
	} else {
		message.id = ((bytes[0]>>5)<<8) | ((bytes[0]<<3) | (bytes[1]>>5));
	}

	message.rtr=(bytes[4] & 0b01000000);
	message.length = (bytes[4] & 0b00001111);  // Number of data bytes
	
	io_read(spi_0, message.data.byte, message.length);
	
	gpio_set_pin_level(_CS, true);
	return message;
}

void MCP2515::Write(uint8_t address, uint8_t data) {
	gpio_set_pin_level(_CS, false);
	uint8_t command = CAN_WRITE;
	io_write(spi_0, &command, 1);
	io_write(spi_0, &address, 1);
	io_write(spi_0, &data, 1);
	gpio_set_pin_level(_CS, true);
}

void MCP2515::Write(uint8_t address, uint8_t data[], uint8_t bytes) {
	// allows for sequential writing of registers starting at address - see data sheet
	uint8_t i;
	gpio_set_pin_level(_CS, false);
	uint8_t command = CAN_WRITE;
	io_write(spi_0, &command, 1);
	io_write(spi_0, &address, 1);
	io_write(spi_0, data, bytes);
	gpio_set_pin_level(_CS, true);
}

void MCP2515::SendBuffer(uint8_t buffers) {
	// buffers should be any combination of TXB0, TXB1, TXB2 ORed together, or TXB_ALL
	gpio_set_pin_level(_CS, false);
	uint8_t command = CAN_RTS | buffers;
	io_write(spi_0, &command, 1);
	gpio_set_pin_level(_CS, true);
}

void MCP2515::LoadBuffer(uint8_t buffer, CAN_FRAME *message) {
 
	// buffer should be one of TXB0, TXB1 or TXB2
	if(buffer==TXB0) buffer = 0; //the values we need are 0, 2, 4 TXB1 and TXB2 are already 2 / 4

	uint8_t bytes[5];
	// byte1 TXBnSIDH
	// byte2 TXBnSIDL
	// byte3 TXBnEID8
	// byte4 TXBnEID0
	// byte5 TXBnDLC

	if(message->extended) {
		bytes[0] = uint8_t((message->id<<3)>>24); // 8 MSBits of SID
		bytes[1] = uint8_t((message->id<<11)>>24) & 0b11100000; // 3 LSBits of SID
		bytes[1] = bytes[1] | uint8_t((message->id<<14)>>30); // 2 MSBits of EID
		bytes[1] = bytes[1] | 0b00001000; // EXIDE
		bytes[2] = uint8_t((message->id<<16)>>24); // EID Bits 15-8
		bytes[3] = uint8_t((message->id<<24)>>24); // EID Bits 7-0
	} else {
		bytes[0] = uint8_t((message->id<<21)>>24); // 8 MSBits of SID
		bytes[1] = uint8_t((message->id<<29)>>24) & 0b11100000; // 3 LSBits of SID
		bytes[2] = 0; // TXBnEID8
		bytes[3] = 0; // TXBnEID0
	}
	bytes[4] = message->length;
	if(message->rtr) {
	bytes[4] = bytes[4] | 0b01000000;
	}
  
	gpio_set_pin_level(_CS, false);
	
	uint8_t command = CAN_LOAD_BUFFER | buffer;
	io_write(spi_0, &command, 1);
	io_write(spi_0, bytes, 5);
	io_write(spi_0, message->data.byte, message->length);
	
	gpio_set_pin_level(_CS, true);
}

uint8_t MCP2515::Status() {
	gpio_set_pin_level(_CS, false);
	uint8_t command = CAN_STATUS;
	io_write(spi_0, &command, 1);
	uint8_t data;
	io_read(spi_0, &data, 1);
	gpio_set_pin_level(_CS, true);
	return data;
	/*
	bit 7 - CANINTF.TX2IF
	bit 6 - TXB2CNTRL.TXREQ
	bit 5 - CANINTF.TX1IF
	bit 4 - TXB1CNTRL.TXREQ
	bit 3 - CANINTF.TX0IF
	bit 2 - TXB0CNTRL.TXREQ
	bit 1 - CANINTFL.RX1IF
	bit 0 - CANINTF.RX0IF
	*/
}

uint8_t MCP2515::RXStatus() {
	gpio_set_pin_level(_CS, false);
	uint8_t command = CAN_RX_STATUS;
	io_write(spi_0, &command, 1);
	uint8_t data;
	io_read(spi_0, &data, 1);
	gpio_set_pin_level(_CS, true);
	return data;
	/*
	bit 7 - CANINTF.RX1IF
	bit 6 - CANINTF.RX0IF
	bit 5 - 
	bit 4 - RXBnSIDL.EIDE
	bit 3 - RXBnDLC.RTR
	bit 2 | 1 | 0 | Filter Match
	------|---|---|-------------
		0 | 0 | 0 | RXF0
		0 | 0 | 1 | RXF1
		0 | 1 | 0 | RXF2
		0 | 1 | 1 | RXF3
		1 | 0 | 0 | RXF4
		1 | 0 | 1 | RXF5
		1 | 1 | 0 | RXF0 (rollover to RXB1)
		1 | 1 | 1 | RXF1 (rollover to RXB1)
	*/
}

void MCP2515::BitModify(uint8_t address, uint8_t mask, uint8_t data) {
	// see data sheet for explanation
	gpio_set_pin_level(_CS, false);
	
	uint8_t command = CAN_BIT_MODIFY;
	io_write(spi_0, &command, 1);
	io_write(spi_0, &address, 1);
	io_write(spi_0, &mask, 1);
	io_write(spi_0, &data, 1);
	gpio_set_pin_level(_CS, true);
}

bool MCP2515::Interrupt() {
	return (gpio_get_pin_level(_INT) == false);
}

bool MCP2515::Mode(uint8_t mode) {
  /*
  mode can be one of the following:
  MODE_CONFIG
  MODE_LISTEN
  MODE_LOOPBACK
  MODE_SLEEP
  MODE_NORMAL
  */
  BitModify(CANCTRL, 0b11100000, mode);
  delay_ms(10); // allow for any transmissions to complete
  uint8_t data = Read(CANSTAT); // check mode has been set
  return ((data & mode)==mode);
}

/*Initializes all filters to either accept all frames or accept none
//This doesn't need to be called if you want to accept everything
//because that's the default. So, call this with permissive = false
//to state with an accept nothing state and then add acceptance masks/filters
//thereafter 
*/
void MCP2515::InitFilters(bool permissive) {
	uint32_t value;
	uint32_t value32;
	if (permissive) {
		value = 0;
        value32 = 0;
	}	
	else {
		value = 0x7FF; //all 11 bits set
        value32 = 0x1FFFFFFF; //all 29 bits set
	}
	SetRXMask(RXMASK0, value32);
	SetRXMask(RXMASK1, value);
	SetRXFilter(FILTER0, value32, 1);
	SetRXFilter(FILTER1, value32, 1);
	SetRXFilter(FILTER2, value, 0);
	SetRXFilter(FILTER3, value, 0);
	SetRXFilter(FILTER4, value, 0);
	SetRXFilter(FILTER5, value, 0);
}

/*
mask = either MASK0 or MASK1
MaskValue is either an 11 or 29 bit mask value to set 
Note that maskes do not store whether they'd be standard or extended. Filters do that. It's a little confusing
*/
void MCP2515::SetRXMask(uint8_t mask, uint32_t MaskValue) {
	uint8_t temp_buff[4];
	uint8_t oldMode;
	
	oldMode = Read(CANSTAT);
	Mode(MODE_CONFIG); //have to be in config mode to change mask
	
    temp_buff[0] = uint8_t((MaskValue << 3) >> 24);
	temp_buff[1] = uint8_t((MaskValue << 11) >> 24) & 0b11100000;
	temp_buff[1] |= uint8_t((MaskValue << 14) >> 30);
	temp_buff[2] = uint8_t((MaskValue << 16)>>24);
	temp_buff[3] = uint8_t((MaskValue << 24)>>24);
	
	Write(mask, temp_buff, 4); //send the four byte mask out to the proper address
	
	Mode(oldMode);
}

/*
filter = FILTER0, FILTER1, FILTER2, FILTER3, FILTER4, FILTER5 (pick one)
FilterValue = 11 or 29 bit filter to use
ext is true if this filter should apply to extended frames or false if it should apply to standard frames.
Do note that, while this function looks a lot like the mask setting function is is NOT identical
It might be able to be though... The setting of EXIDE would probably just be ignored by the mask
*/
void MCP2515::SetRXFilter(uint8_t filter, uint32_t FilterValue, bool ext) {
	uint8_t temp_buff[4];
	uint8_t oldMode;
		
	oldMode = Read(CANSTAT);

	Mode(MODE_CONFIG); //have to be in config mode to change mask
	
	if (ext) { //fill out all 29 bits
		temp_buff[0] = uint8_t((FilterValue << 3) >> 24);
		temp_buff[1] = uint8_t((FilterValue << 11) >> 24) & 0b11100000;
		temp_buff[1] |= uint8_t((FilterValue << 14) >> 30);
		temp_buff[1] |= 0b00001000; //set EXIDE
		temp_buff[2] = uint8_t((FilterValue << 16)>>24);
		temp_buff[3] = uint8_t((FilterValue << 24)>>24);
	}
	else { //make sure to set mask as 11 bit standard mask
		temp_buff[0] = uint8_t((FilterValue << 21)>>24);
		temp_buff[1] = uint8_t((FilterValue << 29) >> 24) & 0b11100000;
		temp_buff[2] = 0;
		temp_buff[3] = 0;
	}
	
	Write(filter, temp_buff, 4); //send the four byte mask out to the proper address
	
	Mode(oldMode);
}

void MCP2515::GetRXFilter(uint8_t filter, uint32_t &filterVal, bool &isExtended)
{
    uint8_t temp_buff[4];
	uint8_t oldMode;
		
	oldMode = Read(CANSTAT);

	Mode(MODE_CONFIG); //have to be in config mode to change mask

    Read(filter, temp_buff, 4);

    //these 11 bits are used either way and stored the same way either way
    filterVal = temp_buff[0] << 3;
    filterVal |= temp_buff[1] >> 5;
    isExtended = false;

    if (temp_buff[1] & 0b00001000) //extended / 29 bit filter - get the remaining 18 bits we need
    {
        isExtended = true;
        filterVal |= (temp_buff[1] & 3) << 27;
        filterVal |= temp_buff[2] << 19;
        filterVal |= temp_buff[3] << 11;
    }

	Mode(oldMode);
}

void MCP2515::GetRXMask(uint8_t mask, uint32_t &filterVal)
{
    uint8_t temp_buff[4];
	uint8_t oldMode;
		
	oldMode = Read(CANSTAT);

	Mode(MODE_CONFIG); //have to be in config mode to change mask

    Read(mask, temp_buff, 4);

    filterVal = temp_buff[0] << 3;
    filterVal |= temp_buff[1] >> 5;
    filterVal |= (temp_buff[1] & 3) << 27;
    filterVal |= temp_buff[2] << 19;
    filterVal |= temp_buff[3] << 11;

	Mode(oldMode);
}


//Places the given frame into the receive queue
void MCP2515::EnqueueRX(CAN_FRAME& newFrame) {
	uint8_t counter;
	rx_frames[rx_frame_write_pos].id = newFrame.id;
	rx_frames[rx_frame_write_pos].rtr = newFrame.rtr;
	rx_frames[rx_frame_write_pos].extended = newFrame.extended;
	rx_frames[rx_frame_write_pos].length = newFrame.length;
	for (counter = 0; counter < 8; counter++) rx_frames[rx_frame_write_pos].data.byte[counter] = newFrame.data.byte[counter];
	rx_frame_write_pos = (rx_frame_write_pos + 1) % 8;
}

//Places the given frame into the transmit queue
//Well, maybe. If there is currently an open hardware buffer
//it will place it into hardware immediately instead of using
//the software queue
void MCP2515::EnqueueTX(CAN_FRAME& newFrame) {
	uint8_t counter;
	uint8_t status = Status() & 0b01010100; //mask for only the transmit buffer empty bits
	
	//don't allow sending or queueing of frames if we're not properly initialized
	if (running == 0) {
		return;
	}
		
	if (status != 0b01010100) { //found an open slot
		if ((status & 0b00000100) == 0) { //transmit buffer 0 is open
			LoadBuffer(TXB0, &newFrame);
			SendBuffer(TXB0);
		}
		else if ((status & 0b00010000) == 0) { //transmit buffer 1 is open
			LoadBuffer(TXB1, &newFrame);
			SendBuffer(TXB1);
		}
		else { // must have been buffer 2 then.
			LoadBuffer(TXB2, &newFrame);
			SendBuffer(TXB2);
		}
	}
	else { //hardware is busy. queue it in software
		if (tx_frame_write_pos != tx_frame_read_pos) { //don't add another frame if the buffer is already full
			tx_frames[tx_frame_write_pos].id = newFrame.id;
			tx_frames[tx_frame_write_pos].rtr = newFrame.rtr;
			tx_frames[tx_frame_write_pos].extended = newFrame.extended;
			tx_frames[tx_frame_write_pos].length = newFrame.length;
			for (counter = 0; counter < 8; counter++) tx_frames[tx_frame_write_pos].data.byte[counter] = newFrame.data.byte[counter];
			tx_frame_write_pos = (tx_frame_write_pos + 1) % 8;
		}		
	}		
}

bool MCP2515::GetRXFrame(CAN_FRAME &frame) {
	uint8_t counter;
	if (rx_frame_read_pos != rx_frame_write_pos) {
		frame.id = rx_frames[rx_frame_read_pos].id;
		frame.rtr = rx_frames[rx_frame_read_pos].rtr;
		frame.extended = rx_frames[rx_frame_read_pos].extended;
		frame.length = rx_frames[rx_frame_read_pos].length;
		for (counter = 0; counter < 8; counter++) frame.data.byte[counter] = rx_frames[rx_frame_read_pos].data.byte[counter];
		rx_frame_read_pos = (rx_frame_read_pos + 1) % 8;
		return true;
	}
	else return false;
}

void MCP2515::intHandler(void) {
    CAN_FRAME message;
    uint32_t ctrlVal;
    // determine which interrupt flags have been set
    uint8_t interruptFlags = Read(CANINTF);
    //Now, acknowledge the interrupts by clearing the intf bits
    Write(CANINTF, 0); 	
    
    if(interruptFlags & RX0IF) {
      // read from RX buffer 0
		message = ReadBuffer(RXB0);
        ctrlVal = Read(RXB0CTRL);
        handleFrameDispatch(&message, ctrlVal & 1);
    }
    if(interruptFlags & RX1IF) {
        // read from RX buffer 1
        message = ReadBuffer(RXB1);
        ctrlVal = Read(RXB1CTRL);
        handleFrameDispatch(&message, ctrlVal & 7);
    }
    if(interruptFlags & TX0IF) {
		// TX buffer 0 sent
       if (tx_frame_read_pos != tx_frame_write_pos) {
			LoadBuffer(TXB0, (CAN_FRAME *)&tx_frames[tx_frame_read_pos]);
		   	SendBuffer(TXB0);
			tx_frame_read_pos = (tx_frame_read_pos + 1) % 8;
	   }
    }
    if(interruptFlags & TX1IF) {
		// TX buffer 1 sent
	  if (tx_frame_read_pos != tx_frame_write_pos) {
		  LoadBuffer(TXB1, (CAN_FRAME *)&tx_frames[tx_frame_read_pos]);
		  SendBuffer(TXB1);
		  tx_frame_read_pos = (tx_frame_read_pos + 1) % 8;
	  }
    }
    if(interruptFlags & TX2IF) {
		// TX buffer 2 sent
		if (tx_frame_read_pos != tx_frame_write_pos) {
			LoadBuffer(TXB2, (CAN_FRAME *)&tx_frames[tx_frame_read_pos]);
			SendBuffer(TXB2);
			tx_frame_read_pos = (tx_frame_read_pos + 1) % 8;
		}
    }
    if(interruptFlags & ERRIF) {
      if (running == 1) { //if there was an error and we had been initialized then try to fix it by reinitializing
		  running = 0;
		  //InitBuffers();
		  //Init(savedBaud, savedFreq);
	  }
    }
    if(interruptFlags & MERRF) {
      // error handling code
      // if TXBnCTRL.TXERR set then transmission error
      // if message is lost TXBnCTRL.MLOA will be set
      if (running == 1) { //if there was an error and we had been initialized then try to fix it by reinitializing
		running = 0;
		//InitBuffers();
		//Init(savedBaud, savedFreq);
	  }	  
    }
}

void MCP2515::handleFrameDispatch(CAN_FRAME *frame, int filterHit)
{
    CANListener *thisListener;

    //First, try to send a callback. If no callback registered then buffer the frame.
    if (cbCANFrame[filterHit]) 
	{
		(*cbCANFrame[filterHit])(frame);
        return;
	}
	else if (cbGeneral) 
	{
		(*cbGeneral)(frame);
        return;
	}
	else
	{
		for (int listenerPos = 0; listenerPos < SIZE_LISTENERS; listenerPos++)
		{
			thisListener = listener[listenerPos];
			if (thisListener != NULL)
			{
				if (thisListener->isCallbackActive(filterHit)) 
				{
					thisListener->gotFrame(frame, filterHit);
                    return;
				}
				else if (thisListener->isCallbackActive(numFilters)) //global catch-all 
				{
					thisListener->gotFrame(frame, -1);
                    return;
				}
			}
		}
	}
	//if none of the callback types caught this frame then queue it in the buffer
    EnqueueRX(*frame);
}
