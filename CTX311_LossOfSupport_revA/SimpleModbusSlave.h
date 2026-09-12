#ifndef SIMPLE_MODBUS_SLAVE_H
#define SIMPLE_MODBUS_SLAVE_H

// SimpleModbusSlaveV10
// PATCHED for VRM315 rev E: adds modbus_read_complete() hook.

/*
 SimpleModbusSlave allows you to communicate
 to any slave using the Modbus RTU protocol.
 
 This implementation DOES NOT fully comply with the Modbus specifications.
 
 Specifically the frame time out have not been implemented according
 to Modbus standards. The code does however combine the check for
 inter character time out and frame time out by incorporating a maximum
 time out allowable when reading from the message stream.
 
 SimpleModbusSlave implements an unsigned int return value on a call to modbus_update().
 This value is the total error count since the slave started. It's useful for fault finding.
 
 This code is for a Modbus slave implementing functions 3, 6 and 16
 function 3: Reads the binary contents of holding registers (4X references)
 function 6: Presets a value into a single holding register (4X reference)
 function 16: Presets values into a sequence of holding registers (4X references)
 
 All the functions share the same register array.
 
 Note:  
 The Arduino serial ring buffer changed in V1.05 it is now 64 bytes or 32 unsigned int registers.
 Most of the time you will connect the arduino to a master via serial
 using a MAX485 or similar.
 
 In a function 3 request the master will attempt to read from your
 slave and since 5 bytes is already used for ID, FUNCTION, NO OF BYTES
 and two BYTES CRC the master can only request 58 bytes or 29 registers.

 PATCH (rev E): BUFFER_SIZE raised from 64 to 128 in the .cpp, so a
 function 3 request can now return up to 61 registers in one sweep.
 The 29-register figure above applies to the UNPATCHED library.
 
 In a function 16 request the master will attempt to write to your 
 slave and since a 9 bytes is already used for ID, FUNCTION, ADDRESS, 
 NO OF REGISTERS, NO OF BYTES and two BYTES CRC the master can only write
 54 bytes or 27 registers.
 
 Using a USB to Serial converter the maximum bytes you can send is 
 limited to its internal buffer which differs between manufactures. 
 
 The functions included here have been derived from the 
 Modbus Specifications and Implementation Guides
 
 http://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf
 http://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b.pdf
 http://www.modbus.org/docs/PI_MBUS_300.pdf
 
*/

#include "Arduino.h"

// function definitions
unsigned int modbus_update();
void modbus_update_comms(long baud, unsigned char byteFormat, unsigned char _slaveID);
void modbus_configure(HardwareSerial *SerialPort,
											long baud,
											unsigned char byteFormat,
											unsigned char _slaveID, 
                      unsigned char _TxEnablePin, 
                      unsigned int _holdingRegsSize,
                      unsigned int* _regs);

// Called from inside the function 3 handler, BEFORE the response frame
// is assembled. Lets the sketch refresh registers at read time.
void sensorData();

// PATCH (rev E): called AFTER a function 3 response frame has been
// transmitted. Lets the sketch implement read-to-clear registers.
// sendPacket() blocks through flush(), so the frame really is on the
// wire by the time this is called.
// startAddress / quantity are the register range the master requested,
// so the sketch can test whether a given register was actually sent.
void modbus_read_complete(unsigned int startAddress, unsigned int quantity);

#endif
