#!/usr/bin/env python3
"""Raspberry Pi client for the CTX311 loss-of-support monitor (map v9).

Separate from ctx310_client.py on purpose. That client knows map v8 and
would misread a CTX311 in ways that matter -- register 35 is a reserved
hole here too, but registers 49-63 do not exist in v8 at all, and
register 60 looks like a magnitude while meaning something different from
anything in v8.

The things this client exists to get right:

  * It refuses to run against anything that is not map version 9.
  * Register 57 reads 0xFFFF when the height estimate is not valid. That
    is "no answer", not 65535 cm.
  * Register 60 includes gravity (~1000 mg at rest). It is NOT comparable
    with register 23, which is AC-coupled and sits near 0 at rest. This
    is the most likely integration misreading, so it is called out
    wherever the value is printed.
  * A CLEAR_LOS refused because the detection channel is still faulted
    reports CMD_STATUS_UNKNOWN -- the same code an unrecognised command
    gets. Since this client only ever sends codes it knows, that status
    on a known code means REFUSED, and it says so rather than reporting
    success.

See docs/REGISTER_MAP_CTX311.md for the full map. Requires minimalmodbus
(pip install minimalmodbus).

  ./ctx311_client.py --port /dev/ttyUSB0 dump
  ./ctx311_client.py --port /dev/ttyUSB0 status
  ./ctx311_client.py --port /dev/ttyUSB0 command clear-los
  ./ctx311_client.py --port /dev/ttyUSB0 los-threshold 850
"""

import argparse
import sys
import time

EXPECTED_MAP_VERSION = 9
REGISTER_COUNT = 64

# register indices used by name
SLAVE_ID = 21
THRESHOLD = 22
THRESHOLD_EFF = 24
VECTOR_RMS = 23         # AC-coupled -- see RAW_MAG
COMMAND = 28
MAP_VERSION = 43
LAST_COMMAND = 45
COMMAND_STATUS = 46
COMMAND_COUNT = 47

LOS_STATUS = 49
LOS_THRESHOLD = 50
LOS_THRESHOLD_EFF = 51
LOS_TIME_MS = 52
LOS_TIME_MS_EFF = 53
LOS_COUNT = 54
LAST_DURATION = 55
LAST_MIN_MAG = 56
LAST_HEIGHT = 57        # 0xFFFF = not valid
LAST_IMPACT = 58
LOS_HOLD_MS = 59
RAW_MAG = 60            # gravity INCLUDED -- not comparable with reg 23
FAULT_FLAGS = 61
BOOT_CHECK = 62
FAULT_ACTION = 63

HEIGHT_INVALID = 0xFFFF

SIGNED_REGISTERS = (32, 33, 34)
RESERVED_REGISTERS = (35, 48)

# Register 48 reads 0 in a release build. A firmware built with
# -DCTX311_STACK_DEBUG reports minimum free SRAM there instead, for the
# soak run. Seeing it non-zero in the field means a debug image is
# installed, which is worth saying out loud rather than printing as a
# puzzling "reserved" value.
STACK_DEBUG_REG = 48

COMMANDS = {
    "clear-peakhold": 0x0001,
    "clear-tripcount": 0x0002,
    "clear-diag": 0x0003,
    "clear-los": 0x0004,
    "clear-loscount": 0x0005,
    "clear-faults": 0x0006,
    "factory-reset": 0x5A5A,
}

CMD_STATUS_IDLE = 0
CMD_STATUS_ACCEPTED = 1
CMD_STATUS_UNKNOWN = 2
CMD_STATUS = {0: "idle", 1: "accepted", 2: "not accepted"}

BOOT_STATES = {0: "pending", 1: "pass", 2: "FAIL"}

# Register 49 bits.
LOS_STATUS_BITS = (
    (0x01, "latched"),
    (0x02, "active now"),
    (0x04, "impact followed"),
    (0x08, "reached free-fall depth"),
    (0x10, "impact clipped"),
    (0x20, "tripped by FAULT, not by an event"),
    (0x40, "still arming -- detection suppressed"),
)

# Register 61 bits. "detection lost" means the protective function is
# gone and (subject to register 63) the arrest engages. The rest are
# advisory: they open health but do not touch the arrest.
FAULT_BITS = (
    (0x0001, "RATE", True, "sample rate outside 1200-2000 Hz"),
    (0x0002, "STUCK", True, "data path frozen"),
    (0x0004, "IMPLAUSIBLE", True, "magnitude not near 1 g at rest"),
    (0x0008, "CONFIG", False, "EEPROM defaulted"),
    (0x0010, "BOOTCHECK", True, "boot check failed"),
    (0x0020, "WDT_RESET", False, "watchdog fired (sticky)"),
)

DETECTION_LOST_MASK = sum(bit for bit, _, lost, _ in FAULT_BITS if lost)

NAMES = {
    0: "X RMS (mg)", 1: "Y RMS (mg)", 2: "Z RMS (mg)",
    3: "impact output state (1=closed)",
    4: "X peak+ (mg)", 5: "Y peak+ (mg)", 6: "Z peak+ (mg)",
    7: "X RMS (cm/s2)", 8: "Y RMS (cm/s2)", 9: "Z RMS (cm/s2)",
    10: "X peak+ (cm/s2)", 11: "Y peak+ (cm/s2)", 12: "Z peak+ (cm/s2)",
    13: "X peak- (mg)", 14: "Y peak- (mg)", 15: "Z peak- (mg)",
    16: "X peak- (cm/s2)", 17: "Y peak- (cm/s2)", 18: "Z peak- (cm/s2)",
    19: "max ISR time (us)", 20: "max loop time (x100us)",
    21: "slave id [R/W]", 22: "impact threshold (mg) [R/W]",
    23: "vector RMS (mg, AC-coupled)",
    24: "effective impact threshold (mg)",
    25: "status bits", 26: "blocks missed", 27: "sample rate (/s)",
    28: "command [W, self-clearing]", 29: "reset cause",
    30: "peak vector RMS hold (mg)", 31: "impact trip count",
    32: "gravity X (mg, signed)", 33: "gravity Y (mg, signed)",
    34: "gravity Z (mg, signed)",
    35: "reserved (always 0)", 36: "peak |a| hold (mg)",
    37: "last impact |a| (mg, read-to-clear)",
    38: "1s RMS X (mg)", 39: "1s RMS Y (mg)", 40: "1s RMS Z (mg)",
    41: "1s vector RMS (mg)",
    42: "firmware version", 43: "map version", 44: "build date",
    45: "last command", 46: "command status", 47: "command count",
    48: "reserved (0), or min free SRAM in a debug build",
    49: "LOS status bits",
    50: "LOS threshold (mg) [R/W]", 51: "LOS threshold effective (mg)",
    52: "LOS confirm time (ms) [R/W]", 53: "LOS confirm time eff (ms)",
    54: "LOS trip count",
    55: "last event duration (ms)",
    56: "last event minimum magnitude (mg)",
    57: "last event height (cm, 0xFFFF=invalid)",
    58: "last event impact peak (mg)",
    59: "LOS output hold (ms, 0=latch) [R/W]",
    60: "live raw magnitude (mg, GRAVITY INCLUDED)",
    61: "fault flags", 62: "boot check", 63: "fault action [R/W]",
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


# A full sweep is 64 registers = a 133-byte RTU frame. At 9600 baud that
# is ~139 ms on the wire, and ONE corrupted bit anywhere in it fails the
# CRC and throws the whole read away. On a noisy line, a long cable, or a
# marginal RS-485 turnaround that happens often enough to matter.
#
# READ_CHUNK splits the sweep into smaller requests: 0 means one 64-register
# read (the default, and the only atomic option), 32 means two reads, 16
# means four. Shorter frames are far more likely to survive intact.
#
# The cost is atomicity, and it is a real cost for this device: chunks are
# separate requests, so the register block can change between them and an
# event can straddle the split. Prefer the single read; fall back to chunks
# only when the line will not carry one.
READ_CHUNK = 0
READ_RETRIES = 3


def read_all(instrument):
    """Read the whole map, retrying on a corrupted frame.

    64 registers is a 133-byte response, which needs the CTX311 copy of
    SimpleModbusSlave with BUFFER_SIZE 160. Against the CTX310 copy (128)
    this request times out.

    A checksum error is not a device fault -- it means the frame did not
    survive the wire. Retrying is the right response; giving up on the
    first one is not, which is what this used to do.
    """
    last = None
    for attempt in range(READ_RETRIES):
        try:
            if READ_CHUNK:
                regs = []
                start = 0
                while start < REGISTER_COUNT:
                    n = min(READ_CHUNK, REGISTER_COUNT - start)
                    regs.extend(
                        instrument.read_registers(start, n, functioncode=3))
                    start += n
                return regs
            return instrument.read_registers(0, REGISTER_COUNT,
                                             functioncode=3)
        except Exception as exc:            # minimalmodbus raises several
            last = exc
            time.sleep(0.05 * (attempt + 1))
    raise last


def describe_read_failure(exc):
    """Turn a minimalmodbus failure into something actionable.

    The raw traceback says "checksum error" and prints 133 bytes of hex,
    which tells a tester nothing about what to do next.
    """
    text = str(exc)
    lines = ["could not read the device: %s" % type(exc).__name__]

    if "hecksum" in text or "CRC" in text:
        lines += [
            "",
            "The device ANSWERED -- a checksum error means the reply arrived",
            "and was corrupted on the way, not that the device is absent or",
            "at the wrong address. Things that cause it, in the order worth",
            "checking:",
            "",
            "  1. Power. A Raspberry Pi showing an undervoltage warning will",
            "     corrupt serial traffic. Fix that first -- it invalidates",
            "     every other measurement you take.",
            "  2. RS-485 wiring: A/B swapped or marginal, missing 120 ohm",
            "     termination at both ends, or no bias resistors.",
            "  3. Frame length. A full sweep is 133 bytes; one bad bit loses",
            "     all of it. Retry with --chunk 16 to use short frames.",
            "  4. Ground. RS-485 needs a common reference, not just A and B.",
        ]
    elif "o response" in text or "imeout" in text:
        lines += [
            "",
            "No reply at all. Check the slave id (--id, default 71), the",
            "port (--port), the baud rate (--baud, default 9600), and that",
            "the RS-485 transceiver is actually populated (H-02).",
        ]
    return "\n".join(lines)


def as_signed(value):
    return value - 65536 if value > 32767 else value


def check_map_version(regs):
    """Refuse to run against anything that is not map version 9.

    Same discipline as the CTX310 client, and it matters more here: a
    CTX310 answering this client would return 49 registers, so registers
    49-63 would either fail to read or -- worse -- a short read would be
    silently interpreted as a device with no faults and no events.
    """
    version = regs[MAP_VERSION]
    if version != EXPECTED_MAP_VERSION:
        raise SystemExit(
            "register map version %d, expected %d -- this client would "
            "misinterpret the data. Refusing to continue.%s"
            % (version, EXPECTED_MAP_VERSION,
               "\nThat looks like a CTX310. Use tools/ctx310_client.py."
               if version == 8 else "")
        )


def decode_bits(value, table):
    return [name for bit, name in table if value & bit]


def decode_faults(value):
    """Return (lost, advisory) lists of (name, description)."""
    lost, advisory = [], []
    for bit, name, is_lost, desc in FAULT_BITS:
        if value & bit:
            (lost if is_lost else advisory).append((name, desc))
    return lost, advisory


def format_height(value):
    """Register 57 is withheld rather than guessed.

    0xFFFF means the assembly never got near free fall, so h = 1/2 g t^2
    would badly overstate the drop. Printing 65535 cm would be a 655 m
    fall, which is the kind of number that ends up in a report.
    """
    if value == HEIGHT_INVALID:
        return "not valid (event never reached free-fall depth)"
    return "%d cm (lower bound)" % value


def format_los_status(value):
    flags = decode_bits(value, LOS_STATUS_BITS)
    return ", ".join(flags) if flags else "clear"


def send_command(instrument, name, timeout=2.0):
    """Write a command and confirm it through the acknowledge registers.

    Register 28 self-clears, so reading it back proves nothing. Register
    47 (accepted count) is the acknowledgement.

    The refusal case is the one worth care. CMD_CLEAR_LOS is refused
    while a detection-lost fault stands and register 63 says a fault
    should arrest -- re-arming a device that still cannot see is exactly
    what must not happen. The firmware reports that refusal as
    CMD_STATUS_UNKNOWN, the same value an unrecognised code gets, and
    does not move the counter. This client only ever sends codes from
    COMMANDS, so that status on a known code is a REFUSAL, not a
    mis-typed command, and it is reported as such.
    """
    code = COMMANDS[name]
    before = instrument.read_register(COMMAND_COUNT, functioncode=3)
    instrument.write_register(COMMAND, code, functioncode=6)

    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.05)
        last, state, count = instrument.read_registers(
            LAST_COMMAND, 3, functioncode=3)
        if last != code or state == CMD_STATUS_IDLE:
            continue

        if state == CMD_STATUS_UNKNOWN:
            faults = instrument.read_register(FAULT_FLAGS, functioncode=3)
            action = instrument.read_register(FAULT_ACTION, functioncode=3)
            reason = ""
            if name == "clear-los" and (faults & DETECTION_LOST_MASK) and action:
                lost, _ = decode_faults(faults)
                reason = (" -- the detection channel is still faulted (%s). "
                          "Fix the fault, then clear-faults, then retry."
                          % ", ".join(n for n, _ in lost))
            raise SystemExit("%s REFUSED by the device%s" % (name, reason))

        if state == CMD_STATUS_ACCEPTED and count != before:
            return count

    raise SystemExit(
        "no acknowledgement for %s (0x%04X) within %.1fs (counter still %d)"
        % (name, code, timeout, before))


def set_register(instrument, register, echo_register, value, units):
    """Write a setting and confirm through its effective-value echo.

    Settings hold their value, so these can be verified by reading back --
    but read the echo, which is what the firmware is actually using. An
    out-of-range write is rejected into the value already in force.
    """
    instrument.write_register(register, value, functioncode=6)
    time.sleep(0.1)
    effective = instrument.read_register(echo_register, functioncode=3)
    if effective != value:
        raise SystemExit(
            "device rejected %d %s (out of range?); still using %d %s"
            % (value, units, effective, units))
    return effective


def print_summary(regs):
    fw, build = regs[42], regs[44]
    print("firmware %d.%d, map version %d, built %04d-%02d-%02d"
          % (fw >> 8, fw & 0xFF, regs[MAP_VERSION],
             2000 + (build >> 9), (build >> 5) & 0x0F, build & 0x1F))

    status = regs[LOS_STATUS]
    print("LOS status:      %s" % format_los_status(status))

    if status & 0x40:
        print("                 device is NOT armed yet -- health output is")
        print("                 open and no loss of support can be detected.")

    faults = regs[FAULT_FLAGS]
    if faults:
        lost, advisory = decode_faults(faults)
        if lost:
            print("FAULTS (detection lost -- protective function GONE):")
            for name, desc in lost:
                print("    %-12s %s" % (name, desc))
        if advisory:
            print("FAULTS (advisory -- health open, arrest untouched):")
            for name, desc in advisory:
                print("    %-12s %s" % (name, desc))
    else:
        print("faults:          none")

    print("boot check:      %s" % BOOT_STATES.get(regs[BOOT_CHECK], "?"))
    print("fault action:    %s"
          % ("a detection-lost fault also arrests" if regs[FAULT_ACTION]
             else "health output only"))
    print("LOS trips:       %d" % regs[LOS_COUNT])
    print("LOS threshold:   %d mg (effective %d), confirm %d ms (effective %d)"
          % (regs[LOS_THRESHOLD], regs[LOS_THRESHOLD_EFF],
             regs[LOS_TIME_MS], regs[LOS_TIME_MS_EFF]))
    print("output hold:     %s"
          % ("latch until commanded" if regs[LOS_HOLD_MS] == 0
             else "%d ms" % regs[LOS_HOLD_MS]))
    print()
    print("last event:      duration %d ms, minimum %d mg, impact peak %d mg"
          % (regs[LAST_DURATION], regs[LAST_MIN_MAG], regs[LAST_IMPACT]))
    print("                 height %s" % format_height(regs[LAST_HEIGHT]))
    print()
    if regs[STACK_DEBUG_REG]:
        print()
        print("!! register 48 reads %d, not 0."
              % regs[STACK_DEBUG_REG])
        print("   This device is running a -DCTX311_STACK_DEBUG image and")
        print("   is reporting minimum free SRAM there. That build is for")
        print("   the soak run only -- reflash with a release image before")
        print("   the device goes back into service.")

    print()
    print("live raw magnitude (reg 60): %d mg" % regs[RAW_MAG])
    print("    Includes gravity -- ~1000 mg at rest is CORRECT, not a fault.")
    print("    Not comparable with register 23 (%d mg), which is AC-coupled"
          % regs[VECTOR_RMS])
    print("    and sits near 0 at rest. Do not trend them on one axis.")


def read_all_or_exit(instrument):
    """Read the map, or exit with advice instead of a traceback."""
    try:
        return read_all(instrument)
    except Exception as exc:
        raise SystemExit(describe_read_failure(exc))


def cmd_dump(instrument, args):
    regs = read_all_or_exit(instrument)
    check_map_version(regs)
    print_summary(regs)
    print()
    for index, value in enumerate(regs):
        shown = as_signed(value) if index in SIGNED_REGISTERS else value
        if index == LAST_HEIGHT and value == HEIGHT_INVALID:
            print("%3d  %-42s %s" % (index, NAMES.get(index, ""), "invalid"))
        else:
            print("%3d  %-42s %6d" % (index, NAMES.get(index, ""), shown))


def cmd_status(instrument, args):
    regs = read_all_or_exit(instrument)
    check_map_version(regs)
    print_summary(regs)


def cmd_command(instrument, args):
    # Guard the map version before writing anything at all: sending
    # CLEAR_LOS (0x0004) to a CTX310 would land on a code it does not
    # know, which is harmless, but the next command in the list is not.
    check_map_version(read_all_or_exit(instrument))
    count = send_command(instrument, args.name)
    print("%s accepted (command count now %d)" % (args.name, count))
    if args.name == "factory-reset":
        print("slave id is now 71 -- reconnect on that id")


def cmd_los_threshold(instrument, args):
    check_map_version(read_all_or_exit(instrument))
    value = set_register(instrument, LOS_THRESHOLD, LOS_THRESHOLD_EFF,
                         args.milli_g, "mg")
    print("LOS threshold now %d mg" % value)


def cmd_los_time(instrument, args):
    check_map_version(read_all_or_exit(instrument))
    value = set_register(instrument, LOS_TIME_MS, LOS_TIME_MS_EFF,
                         args.milliseconds, "ms")
    print("LOS confirm time now %d ms" % value)
    print("Arrest energy goes with the SQUARE of velocity -- confirm the")
    print("arrestor can absorb this, plus its own engagement time.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--id", type=int, default=71, dest="slave_id")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--chunk", type=int, default=0, metavar="N",
                        help="read the map in N-register chunks instead of "
                             "one 64-register request. Shorter frames "
                             "survive a noisy line better, but chunks are "
                             "NOT atomic -- the block can change between "
                             "them. Try 16 if you get checksum errors.")
    parser.add_argument("--retries", type=int, default=3,
                        help="attempts per read before giving up (default 3)")

    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("dump", help="read and print the whole register map")
    sub.add_parser("status", help="print the decoded summary only")

    p = sub.add_parser("command", help="send a command and confirm it ran")
    p.add_argument("name", choices=sorted(COMMANDS))

    p = sub.add_parser("los-threshold", help="set the loss-of-support threshold")
    p.add_argument("milli_g", type=int)

    p = sub.add_parser("los-time", help="set the loss-of-support confirm time")
    p.add_argument("milliseconds", type=int)

    args = parser.parse_args(argv)

    global READ_CHUNK, READ_RETRIES
    READ_CHUNK = max(0, args.chunk)
    READ_RETRIES = max(1, args.retries)

    handlers = {
        "dump": cmd_dump,
        "status": cmd_status,
        "command": cmd_command,
        "los-threshold": cmd_los_threshold,
        "los-time": cmd_los_time,
    }

    try:
        instrument = connect(args.port, args.slave_id, args.baud)
    except ImportError:
        raise SystemExit("minimalmodbus is not installed "
                         "(pip install minimalmodbus)")
    return handlers[args.action](instrument, args)


if __name__ == "__main__":
    sys.exit(main())
