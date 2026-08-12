#!/usr/bin/env python3
"""Raspberry Pi client for the CTX310 / VRM315 impact monitor.

Covers the two things that are easy to get wrong from the master side:

  * Register 28 is a self-clearing trigger. Writing it and reading it back
    always returns 0, so this client confirms a command by watching the
    accepted-command counter in register 47 instead.
  * Registers 32-34 are signed; every other register is unsigned.
  * Registers 35 and 48 are reserved holes and always read 0.

See docs/REGISTER_MAP.md for the full map. Requires minimalmodbus
(pip install minimalmodbus).

  ./ctx310_client.py --port /dev/ttyUSB0 dump
  ./ctx310_client.py --port /dev/ttyUSB0 command clear-peakhold
  ./ctx310_client.py --port /dev/ttyUSB0 threshold 2500
"""

import argparse
import sys
import time

EXPECTED_MAP_VERSION = 8
REGISTER_COUNT = 49

# register indices used by name
SLAVE_ID = 21
THRESHOLD = 22
THRESHOLD_EFF = 24
COMMAND = 28
RSVD_35 = 35            # reserved, always 0 (was the RMS trip threshold)
MAP_VERSION = 43
LAST_COMMAND = 45
COMMAND_STATUS = 46
COMMAND_COUNT = 47
RSVD_48 = 48            # reserved, always 0

SIGNED_REGISTERS = (32, 33, 34)

COMMANDS = {
    "clear-peakhold": 0x0001,
    "clear-tripcount": 0x0002,
    "clear-diag": 0x0003,
    "factory-reset": 0x5A5A,
}

CMD_STATUS = {0: "idle", 1: "accepted", 2: "unknown code"}

NAMES = {
    0: "X RMS (mg)", 1: "Y RMS (mg)", 2: "Z RMS (mg)",
    3: "output state (1=closed)",
    4: "X peak+ (mg)", 5: "Y peak+ (mg)", 6: "Z peak+ (mg)",
    7: "X RMS (cm/s2)", 8: "Y RMS (cm/s2)", 9: "Z RMS (cm/s2)",
    10: "X peak+ (cm/s2)", 11: "Y peak+ (cm/s2)", 12: "Z peak+ (cm/s2)",
    13: "X peak- (mg)", 14: "Y peak- (mg)", 15: "Z peak- (mg)",
    16: "X peak- (cm/s2)", 17: "Y peak- (cm/s2)", 18: "Z peak- (cm/s2)",
    19: "max ISR time (us)", 20: "max loop time (x100us)",
    21: "slave id [R/W]", 22: "peak threshold (mg) [R/W]",
    23: "vector RMS (mg)", 24: "effective peak threshold (mg)",
    25: "status bits", 26: "blocks missed", 27: "sample rate (/s)",
    28: "command [W, self-clearing]", 29: "reset cause",
    30: "peak vector RMS hold (mg)", 31: "trip count",
    32: "gravity X (mg, signed)", 33: "gravity Y (mg, signed)",
    34: "gravity Z (mg, signed)",
    35: "reserved (always 0)", 36: "peak |a| hold (mg)",
    37: "last impact |a| (mg, read-to-clear)",
    38: "1s RMS X (mg)", 39: "1s RMS Y (mg)", 40: "1s RMS Z (mg)",
    41: "1s vector RMS (mg)",
    42: "firmware version", 43: "map version", 44: "build date",
    45: "last command", 46: "command status", 47: "command count",
    48: "reserved (always 0)",
}


def connect(port, slave_id, baudrate=9600, timeout=1.0):
    import minimalmodbus
    import serial

    instrument = minimalmodbus.Instrument(port, slave_id)
    instrument.serial.baudrate = baudrate
    instrument.serial.bytesize = 8
    instrument.serial.parity = serial.PARITY_NONE
    instrument.serial.stopbits = 1
    instrument.serial.timeout = timeout
    instrument.mode = minimalmodbus.MODE_RTU
    instrument.clear_buffers_before_each_transaction = True
    return instrument


def read_all(instrument):
    """Read the whole map in one request.

    This needs the patched SimpleModbusSlave (128-byte buffer). Against
    the stock 64-byte library the request times out and the map has to be
    read in two chunks of 29 or fewer registers.
    """
    return instrument.read_registers(0, REGISTER_COUNT, functioncode=3)


def as_signed(value):
    return value - 65536 if value > 32767 else value


def check_map_version(regs):
    version = regs[MAP_VERSION]
    if version != EXPECTED_MAP_VERSION:
        raise SystemExit(
            "register map version %d, expected %d -- this client would "
            "misinterpret the data. Refusing to continue."
            % (version, EXPECTED_MAP_VERSION)
        )


def send_command(instrument, code, timeout=2.0):
    """Write a command and confirm it ran.

    Register 28 self-clears, so reading it back proves nothing. The
    accepted-command counter in register 47 is the acknowledgement, and it
    moves even when the same command is sent twice in a row.
    """
    before = instrument.read_register(COMMAND_COUNT, functioncode=3)
    instrument.write_register(COMMAND, code, functioncode=6)

    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.05)
        status = instrument.read_registers(LAST_COMMAND, 3, functioncode=3)
        last, state, count = status
        if last == code and state != 0:
            if state == 2:
                raise SystemExit(
                    "device rejected command 0x%04X as an unknown code" % code
                )
            if count != before:
                return count
    raise SystemExit(
        "no acknowledgement for command 0x%04X within %.1fs "
        "(counter still %d)" % (code, timeout, before)
    )


def set_threshold(instrument, register, echo_register, milli_g):
    """Write a setting and confirm it took.

    Settings hold their value, so unlike the command register these can be
    verified by reading back -- but read the effective-value echo, which
    reflects what the firmware is actually using.
    """
    instrument.write_register(register, milli_g, functioncode=6)
    time.sleep(0.1)
    effective = instrument.read_register(echo_register, functioncode=3)
    if effective != milli_g:
        raise SystemExit(
            "device rejected %d mg (out of range?); still using %d mg"
            % (milli_g, effective)
        )
    return effective


def cmd_dump(instrument, args):
    regs = read_all(instrument)
    check_map_version(regs)

    fw = regs[42]
    build = regs[44]
    print("firmware %d.%d, map version %d, built %04d-%02d-%02d"
          % (fw >> 8, fw & 0xFF, regs[MAP_VERSION],
             2000 + (build >> 9), (build >> 5) & 0x0F, build & 0x1F))
    print("last command 0x%04X, status %s, %d accepted since boot"
          % (regs[LAST_COMMAND],
             CMD_STATUS.get(regs[COMMAND_STATUS], "?"),
             regs[COMMAND_COUNT]))
    print()
    for index, value in enumerate(regs):
        shown = as_signed(value) if index in SIGNED_REGISTERS else value
        print("%3d  %-38s %6d" % (index, NAMES.get(index, ""), shown))


def cmd_command(instrument, args):
    code = COMMANDS[args.name]
    count = send_command(instrument, code)
    print("%s accepted (command count now %d)" % (args.name, count))
    if args.name == "factory-reset":
        print("slave id is now 71 -- reconnect on that id")


def cmd_threshold(instrument, args):
    value = set_threshold(instrument, THRESHOLD, THRESHOLD_EFF, args.milli_g)
    print("peak threshold now %d mg" % value)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--id", type=int, default=71, dest="slave_id")
    parser.add_argument("--baud", type=int, default=9600)

    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("dump", help="read and print the whole register map")

    p = sub.add_parser("command", help="send a command and confirm it ran")
    p.add_argument("name", choices=sorted(COMMANDS))

    p = sub.add_parser("threshold", help="set the peak trip threshold")
    p.add_argument("milli_g", type=int)

    args = parser.parse_args(argv)

    handlers = {
        "dump": cmd_dump,
        "command": cmd_command,
        "threshold": cmd_threshold,
    }

    try:
        instrument = connect(args.port, args.slave_id, args.baud)
    except ImportError:
        raise SystemExit("minimalmodbus is not installed "
                         "(pip install minimalmodbus)")
    return handlers[args.action](instrument, args)


if __name__ == "__main__":
    sys.exit(main())
