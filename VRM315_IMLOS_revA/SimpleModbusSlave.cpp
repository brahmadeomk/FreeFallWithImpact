#include "SimpleModbusSlave.h"
#include "HardwareSerial.h"
//#include <SoftwareSerial.h>

#define DEBUG_MODBUS 0




// SimpleModbusSlaveV10
// PATCHED for VRM315 rev E: single added call to modbus_read_complete()
// after a function 3 response is transmitted. Search "PATCH (rev E)".

// PATCH (rev E): raised 64 -> 128 so a single function-3 request can
// return the whole 42-register map in one sweep.
//   response frame = 5 + 2*N bytes;  N=42 -> 89 bytes, N=61 -> 127 bytes
// This was the ONLY thing capping reads at 29 registers -- it is not a
// protocol limit. The Arduino TX buffer is unaffected: write() blocks
// when full rather than dropping bytes, and flush() waits for completion.
// Costs 64 bytes of SRAM; total static use is ~558 B of 2048 B.
// Raised 160 -> 192 for map 15. A 76-register sweep is 5 + 2*76 = 157
// bytes, which fitted 160 with three bytes to spare -- a cliff the next
// register would have gone over. 192 restores room for 17 more and
// costs 32 B of SRAM against ~940 B free. Taken deliberately, with the
// figures, rather than drifted into.
#define BUFFER_SIZE 192

// frame[] is used to recieve and transmit packages.

unsigned char frame[BUFFER_SIZE];
unsigned int holdingRegsSize; // size of the register array
unsigned int* regs; // user array address
unsigned char broadcastFlag;
unsigned char slaveID;
unsigned char function;
unsigned char TxEnablePin;
unsigned int errorCount;
unsigned int requestCount;   // CRC-valid frames addressed to us
unsigned int T1_5; // inter character time out
unsigned int T3_5; // frame delay
HardwareSerial* ModbusPort;

// function definitions
void exceptionResponse(unsigned char exception);
static void rxReset();                 // defined with the receive state below
unsigned int calculateCRC(unsigned char bufferSize);
void sendPacket(unsigned char bufferSize);

void modbus_configure(HardwareSerial *SerialPort,
                      long baud,
                      unsigned char byteFormat,
                      unsigned char _slaveID,
                      unsigned char _TxEnablePin,
                      unsigned int _holdingRegsSize,
                      unsigned int* _regs)
{
  ModbusPort = SerialPort;
  modbus_update_comms(baud, byteFormat, _slaveID);
  holdingRegsSize = _holdingRegsSize;
  regs = _regs;
  TxEnablePin = _TxEnablePin;
  pinMode(TxEnablePin, OUTPUT);
  digitalWrite(TxEnablePin, LOW);
  errorCount = 0; // initialize errorCount
  requestCount = 0;
  rxReset();
}

void modbus_update_comms(long baud, unsigned char byteFormat, unsigned char _slaveID)
{
  //(*ModbusPort).begin(baud,byteFormat);
  slaveID = _slaveID;

  // Modbus states that a baud rate higher than 19200 must use a fixed 750 us
  // for inter character time out and 1.75 ms for a frame delay for baud rates
  // below 19200 the timing is more critical and has to be calculated.
  // E.g. 9600 baud in a 10 bit packet is 960 characters per second
  // In milliseconds this will be 960characters per 1000ms. So for 1 character
  // 1000ms/960characters is 1.04167ms per character and finally modbus states
  // an inter-character must be 1.5T or 1.5 times longer than a character. Thus
  // 1.5T = 1.04167ms * 1.5 = 1.5625ms. A frame delay is 3.5T.

  if (baud > 19200)
  {
    T1_5 = 750;
    T3_5 = 1750;
  }
  else
  {
    T1_5 = 15000000 / baud; // 1T * 1.5 = T1.5
    T3_5 = 35000000 / baud; // 1T * 3.5 = T3.5
  }
}

/* ===================================================================
   PATCH (rev A, map 15): NON-BLOCKING RECEIVE.

   The original drained the UART with delayMicroseconds(T1_5) after
   EVERY byte -- 1562 us each at 9600 baud -- and only looked at the
   slave ID once the whole frame was buffered. Three things followed,
   and all of them got worse as slaves were added to the bus:

   1. Every slave paid that cost for every frame on the bus, its own
      and everybody else's, because a multi-drop receiver hears the
      lot. A 15-byte poll exchange kept every slave busy 31 ms against
      15.6 ms of actual wire time.

   2. The drain was slower than the arrival rate -- 1562 us per byte
      against 1042 us -- so the ring buffer grew during any burst and
      only emptied in the gaps. With the ADXL345 interrupt taking
      24.3% of the CPU the real figure was 2060 us, because
      delayMicroseconds() burns cycles and does not know interrupts
      exist, so every ISR stretches it. Drain against arrival was
      2:1.

   3. Worst of all, the receive loop treated everything it drained in
      one pass as ONE frame keyed on the first byte. A slave still
      draining when the next request arrived appended that request to
      a frame already carrying somebody else's ID -- and discarded
      its own request. That is a silent, permanent-looking
      disconnection, and it needed ~15 ms of bus silence after every
      poll to avoid.

   This version never blocks. It takes whatever has arrived, remembers
   when the last byte landed, and declares the frame complete after
   T3_5 of silence -- which is what the Modbus specification actually
   asks for. The required gap between frames drops from ~15 ms to the
   spec's 3.645 ms, the ring buffer is drained every loop() pass
   instead of once per frame, and the ISR no longer inflates anything
   because there is nothing left to inflate.

   The slave ID is now read from byte 0, so a frame addressed to
   another slave is counted and dropped rather than buffered.
   =================================================================== */
static unsigned int  rxSeen     = 0;   // bytes in this frame, stored or not
static unsigned char rxStored   = 0;   // bytes actually placed in frame[]
static unsigned long rxLastUs   = 0;   // micros() of the most recent byte
static unsigned char rxOverflow = 0;
static unsigned char rxForeign  = 0;   // byte 0 was not our ID or broadcast

static void rxReset()
{
  rxSeen = 0; rxStored = 0; rxOverflow = 0; rxForeign = 0;
}

unsigned int modbus_error_count()   { return errorCount; }
unsigned int modbus_request_count() { return requestCount; }

void modbus_reset_diagnostics()
{
  errorCount = 0;
  requestCount = 0;
}

unsigned int modbus_update()
{
  // ---- take everything the UART has, without waiting for anything ----
  while ((*ModbusPort).available())
  {
    unsigned char b = (unsigned char)(*ModbusPort).read();
    rxLastUs = micros();

    // Byte 0 decides whether this frame is worth storing at all.
    if (rxSeen == 0)
      rxForeign = (b != slaveID && b != 0);

    if (rxSeen < 0xFFFFU)
      rxSeen++;

    if (!rxForeign && !rxOverflow)
    {
      if (rxStored < BUFFER_SIZE)
        frame[rxStored++] = b;
      else
        rxOverflow = 1;   // never write past the array
    }
  }

  // ---- a frame ends on silence, not on a per-byte delay ----
  if (rxSeen == 0)
    return errorCount;

  if ((unsigned long)(micros() - rxLastUs) < (unsigned long)T3_5)
    return errorCount;          // still arriving; come back next loop()

  unsigned char buffer   = rxStored;
  unsigned char overflow = rxOverflow;
  unsigned char foreign  = rxForeign;
  rxReset();

  // Addressed to another slave. Not an error -- just not ours.
  if (foreign)
    return errorCount;

  // If an overflow occurred increment the errorCount
  // variable and return to the main sketch without
  // responding to the request i.e. force a timeout
  if (overflow)
    return errorCount++;

  {
    // The minimum request packet is 8 bytes for function 3 & 16
    if (buffer > 7)
    {
      unsigned char id = frame[0];

      broadcastFlag = 0;

      if (id == 0)
        broadcastFlag = 1;

      if (id == slaveID || broadcastFlag) // if the recieved ID matches the slaveID or broadcasting id (0), continue
      {
        unsigned int crc = ((frame[buffer - 2] << 8) | frame[buffer - 1]); // combine the crc Low & High bytes
        if (calculateCRC(buffer - 2) == crc) // if the calculated crc matches the recieved crc continue
        {
          if (requestCount < 0xFFFFU) requestCount++;
          function = frame[1];
          unsigned int startingAddress = ((frame[2] << 8) | frame[3]); // combine the starting address bytes
          unsigned int no_of_registers = ((frame[4] << 8) | frame[5]); // combine the number of register bytes
          unsigned int maxData = startingAddress + no_of_registers;
          unsigned char index;
          unsigned char address;
          unsigned int crc16;

          // broadcasting is not supported for function 3
          if (!broadcastFlag && (function == 3))
          {
            sensorData();

            if (startingAddress < holdingRegsSize) // check exception 2 ILLEGAL DATA ADDRESS
            {
              if (maxData <= holdingRegsSize) // check exception 3 ILLEGAL DATA VALUE
              {
                unsigned char noOfBytes = no_of_registers * 2;
                // ID, function, noOfBytes, (dataLo + dataHi)*number of registers,
                //  crcLo, crcHi
                unsigned char responseFrameSize = 5 + noOfBytes;
                frame[0] = slaveID;
                frame[1] = function;
                frame[2] = noOfBytes;
                address = 3; // PDU starts at the 4th byte
                unsigned int temp;

                for (index = startingAddress; index < maxData; index++)
                {
                  temp = regs[index];
                  frame[address] = temp >> 8; // split the register into 2 bytes
                  address++;
                  frame[address] = temp & 0xFF;
                  address++;
                }

                crc16 = calculateCRC(responseFrameSize - 2);
                frame[responseFrameSize - 2] = crc16 >> 8; // split crc into 2 bytes
                frame[responseFrameSize - 1] = crc16 & 0xFF;
                sendPacket(responseFrameSize);

                // PATCH (rev E): the response is now fully transmitted
                // (sendPacket blocks through flush). Notify the sketch so
                // it can service read-to-clear registers.
                modbus_read_complete(startingAddress, no_of_registers);
              }
              else
                exceptionResponse(3); // exception 3 ILLEGAL DATA VALUE
            }
            else
              exceptionResponse(2); // exception 2 ILLEGAL DATA ADDRESS
          }
          else if (function == 6)
          {
            if (startingAddress < holdingRegsSize) // check exception 2 ILLEGAL DATA ADDRESS
            {
              regs[startingAddress] = ((frame[4] << 8) | frame[5]);// the 4th and 5th elements in frame is the 16 bit data value

              // only the first 6 bytes are used for CRC calculation
              crc16 = calculateCRC(6);
              frame[6] = crc16 >> 8; // split crc into 2 bytes
              frame[7] = crc16 & 0xFF;

              // a function 16 response is an echo of the first 6 bytes from
              // the request + 2 crc bytes
              if (!broadcastFlag) // don't respond if it's a broadcast message
                sendPacket(8);
            }
            else
              exceptionResponse(2); // exception 2 ILLEGAL DATA ADDRESS
          }
          else if (function == 16)
          {
            // Check if the recieved number of bytes matches the calculated bytes
            // minus the request bytes.
            // id + function + (2 * address bytes) + (2 * no of register bytes) +
            // byte count + (2 * CRC bytes) = 9 bytes
            if (frame[6] == (buffer - 9))
            {
              if (startingAddress < holdingRegsSize) // check exception 2 ILLEGAL DATA ADDRESS
              {
                if (maxData <= holdingRegsSize) // check exception 3 ILLEGAL DATA VALUE
                {
                  address = 7; // start at the 8th byte in the frame

                  for (index = startingAddress; index < maxData; index++)
                  {
                    regs[index] = ((frame[address] << 8) | frame[address + 1]);
                    address += 2;
                  }

                  // only the first 6 bytes are used for CRC calculation
                  crc16 = calculateCRC(6);
                  frame[6] = crc16 >> 8; // split crc into 2 bytes
                  frame[7] = crc16 & 0xFF;

                  // a function 16 response is an echo of the first 6 bytes from
                  // the request + 2 crc bytes
                  if (!broadcastFlag) // don't respond if it's a broadcast message
                    sendPacket(8);
                }
                else
                  exceptionResponse(3); // exception 3 ILLEGAL DATA VALUE
              }
              else
                exceptionResponse(2); // exception 2 ILLEGAL DATA ADDRESS
            }
            else
              errorCount++; // corrupted packet
          }
          else
          {
            exceptionResponse(1); // exception 1 ILLEGAL FUNCTION

            #if DEBUG_MODBUS
              Serial.println("Illegal Function");
            #endif

          }
        }
        else // checksum failed
        {
          errorCount++;
          #if DEBUG_MODBUS
          Serial.println("CRC Error");
          #endif

        }
      }
      else // incorrect id
      {
       #if DEBUG_MODBUS
        Serial.println("Incorrect Slave id");
        #endif
      }
    }
    else if (buffer > 0 && buffer < 8)
      errorCount++; // corrupted packet
  }

  //SetFlag = 1;
  return errorCount;
}

void exceptionResponse(unsigned char exception)
{
  // each call to exceptionResponse() will increment the errorCount
  errorCount++;
  if (!broadcastFlag) // don't respond if its a broadcast message
  {
    frame[0] = slaveID;
    frame[1] = (function | 0x80); // set MSB bit high, informs the master of an exception
    frame[2] = exception;
    unsigned int crc16 = calculateCRC(3); // ID, function|0x80, exception code
    frame[3] = crc16 >> 8;
    frame[4] = crc16 & 0xFF;
    // exception response is always 5 bytes
    // ID, function + 0x80, exception code, 2 bytes crc
    sendPacket(5);
  }
}

unsigned int calculateCRC(unsigned char bufferSize)
{
  unsigned int temp, temp2, flag;
  temp = 0xFFFF;
  for (unsigned char i = 0; i < bufferSize; i++)
  {
    temp = temp ^ frame[i];
    for (unsigned char j = 1; j <= 8; j++)
    {
      flag = temp & 0x0001;
      temp >>= 1;
      if (flag)
        temp ^= 0xA001;
    }
  }
  // Reverse byte order.
  temp2 = temp >> 8;
  temp = (temp << 8) | temp2;
  temp &= 0xFFFF;
  // the returned value is already swapped
  // crcLo byte is first & crcHi byte is last
  return temp;
}

void sendPacket(unsigned char bufferSize)
{
  digitalWrite(TxEnablePin, HIGH);

  // PATCH (map 15): the T3_5 that used to sit here is gone.
  //
  // Its job was to guarantee 3.5 character times of silence before the
  // reply, which the Modbus specification requires between frames. The
  // receive path now provides that guarantee itself -- it will not
  // declare a frame complete, and so will never reach this function,
  // until it has measured T3_5 of silence. Keeping the delay here
  // bought a second helping of a gap that had already happened, and
  // charged 3.645 ms of response latency for it.
  //
  // What is genuinely needed is driver-enable settling: a MAX485 takes
  // tens to a few hundred nanoseconds from DE going high to the driver
  // being active. 10 us is three orders of magnitude of margin, and in
  // practice digitalWrite() plus the first write() already cover it.
  delayMicroseconds(10);

  for (unsigned char i = 0; i < bufferSize; i++)
    (*ModbusPort).write(frame[i]);

  (*ModbusPort).flush();

  // The post-transmit delay STAYS. flush() above waits on TXC0, so the
  // last stop bit really is on the wire by now and this is not needed
  // for correctness -- but dropping DE early truncates the final byte
  // for every slave on the bus, and that failure is silent and
  // intermittent. It is the one place on this path where insurance is
  // worth more than the microseconds it costs.
  delayMicroseconds(T3_5);

  digitalWrite(TxEnablePin, LOW);

  // PATCH (map 15): throw away anything the UART caught while we were
  // driving the bus. Where /RE is not tied to DE the slave hears its
  // own frame, and that frame starts with our own ID -- so without
  // this it would be parsed as a request and fail CRC, once per reply.
  while ((*ModbusPort).available()) (*ModbusPort).read();
  rxReset();
}
