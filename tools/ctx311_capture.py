#!/usr/bin/env python3
"""Drop-rig capture for the CTX311. Logs every register to CSV and
summarises each event.

Threshold and tolerance tuning (H-04) is the real schedule risk on this
product, and it needs data rather than opinions. This is the instrument
for collecting it: plug in, run one command, do a drop, read the summary.

What it does:

  * Polls all 64 registers as fast as the line allows and writes one CSV
    row per poll, with a host timestamp.
  * Watches registers 49 (LOS status) and 61 (fault flags) and prints a
    line whenever either changes, so the console is a live event log
    while the CSV is the full record.
  * Summarises each event when it ends: minimum magnitude, duration,
    whether free-fall depth was reached, impact peak, and -- the
    distinction that matters most for tuning -- whether the trip was a
    real event or the device faulting.

What it deliberately does NOT do: decide whether a drop passed. That
judgement belongs to the tester and goes in docs/DROP_TEST_PROTOCOL.md.

A note on poll rate. The device samples at 1600 Hz; this polls at maybe
5-15 Hz over 9600 baud. The CSV is therefore a record of what the DEVICE
REPORTED, not a waveform of the drop. That is the right level for
threshold tuning, because the registers being tuned (50, 52) are decided
inside the firmware from data this link could never carry. Do not expect
to reconstruct the fall from the CSV -- use registers 55/56/57/58, which
the firmware latched at full rate.

Requires minimalmodbus (pip install minimalmodbus).

  ./ctx311_capture.py --port /dev/ttyUSB0 --out drop_2026-08-12.csv
  ./ctx311_capture.py --port /dev/ttyUSB0 --out run.csv --duration 120
"""

import argparse
import csv
import datetime
import sys
import time

# Reuse the client's register map, decoders and refusal logic rather than
# restating them -- two copies of a register map is how they drift apart.
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctx311_client as ctx  # noqa: E402


HEADER = (["host_time", "elapsed_s"]
          + ["reg%d" % i for i in range(ctx.REGISTER_COUNT)])


def decode_event(regs, status_mask):
    """Pull the firmware's latched event record out of a register block.

    Registers 54-58 are LATCHED: the firmware fills them at 1600 Hz and
    they survive the event, so the falling-edge block is the right place
    to read them. They describe the event far better than anything this
    poll rate could observe.

    Register 49's bits do NOT survive -- they clear when the device
    re-arms, and the re-arm is often the same poll that ends the event.
    Reading them from the falling-edge block loses exactly the two facts
    worth the most: whether free-fall depth was reached, and whether the
    trip was a fault rather than an event. So status_mask is the OR of
    every register 49 value seen while the event was open, not the last
    one. Bits also appear at different moments -- free-fall depth is
    reached partway down, well after the trip latches -- so accumulating
    is right even ignoring the re-arm.
    """
    status = status_mask
    return {
        "duration_ms": regs[ctx.LAST_DURATION],
        "min_mg": regs[ctx.LAST_MIN_MAG],
        "height": regs[ctx.LAST_HEIGHT],
        "impact_mg": regs[ctx.LAST_IMPACT],
        "reached_free_fall": bool(status & 0x08),
        "by_fault": bool(status & 0x20),
        "impact_followed": bool(status & 0x04),
        "clipped": bool(status & 0x10),
        "trips": regs[ctx.LOS_COUNT],
    }


def print_event_summary(index, event, faults):
    """One block per event. The event/fault distinction leads.

    A trip caused by the device faulting tells you nothing about the
    threshold and must never be averaged into tuning data -- so it is
    said first and said plainly, not left to be inferred from a bit.
    """
    print()
    print("  ---- event %d ----" % index)
    if event["by_fault"]:
        lost, advisory = ctx.decode_faults(faults)
        names = ", ".join(n for n, _ in lost) or "none latched now"
        print("  TRIP CAUSE:  FAULT (%s)" % names)
        print("               NOT a loss-of-support event. Exclude this from")
        print("               threshold tuning -- it measures the device,")
        print("               not the drop.")
        if advisory:
            print("  advisory:    %s"
                  % ", ".join(n for n, _ in advisory))
    else:
        print("  TRIP CAUSE:  loss-of-support event")

    print("  duration:    %d ms" % event["duration_ms"])
    print("  minimum:     %d mg   (the discriminator: a clean drop"
          % event["min_mg"])
    print("               approaches 0, a guided descent stays high)")
    print("  free-fall:   %s" % ("reached" if event["reached_free_fall"]
                                 else "NOT reached -- constrained descent"))
    print("  height:      %s" % ctx.format_height(event["height"]))
    print("  impact peak: %d mg%s"
          % (event["impact_mg"],
             "  [CLIPPED -- true peak is higher]" if event["clipped"] else ""))
    print("  impact followed: %s" % ("yes" if event["impact_followed"]
                                     else "no"))
    print("  LOS trip count now %d" % event["trips"])
    print()


def capture(instrument, writer, duration, quiet):
    start = time.time()
    polls = 0
    events = 0
    prev_status = None
    prev_faults = None
    event_open = False
    event_status = 0
    event_faults = 0

    print("polling -- Ctrl-C to stop")
    print("t=0.0  waiting for the first read...")

    try:
        while duration is None or (time.time() - start) < duration:
            try:
                regs = ctx.read_all(instrument)
            except Exception as exc:            # keep the rig running
                print("  read error: %s" % exc)
                time.sleep(0.2)
                continue

            now = time.time()
            elapsed = now - start
            polls += 1

            writer.writerow(
                [datetime.datetime.fromtimestamp(now).isoformat(
                    timespec="milliseconds"),
                 "%.3f" % elapsed] + list(regs))

            status = regs[ctx.LOS_STATUS]
            faults = regs[ctx.FAULT_FLAGS]

            if prev_status is None:
                # First poll: report the resting state so the log has a
                # baseline, then treat later reads as transitions.
                print("t=%6.2f  initial: LOS[%s] faults[%s]"
                      % (elapsed, ctx.format_los_status(status),
                         format_faults_short(faults)))
            else:
                if status != prev_status:
                    print("t=%6.2f  reg49 %#06x -> %#06x   %s"
                          % (elapsed, prev_status, status,
                             ctx.format_los_status(status)))
                if faults != prev_faults:
                    print("t=%6.2f  reg61 %#06x -> %#06x   %s"
                          % (elapsed, prev_faults, faults,
                             format_faults_short(faults)))

            # An event is "open" while bit0 (latched) or bit1 (active) is
            # set. Summarise on the falling edge, when the firmware's
            # latched record is complete and stable.
            now_open = bool(status & 0x03)
            if now_open:
                if not event_open:
                    event_open = True
                    event_status = 0
                    event_faults = 0
                event_status |= status
                event_faults |= faults
            elif event_open:
                events += 1
                print_event_summary(events, decode_event(regs, event_status),
                                    event_faults)
                event_open = False

            prev_status, prev_faults = status, faults

    except KeyboardInterrupt:
        print("\nstopped")

    elapsed = time.time() - start
    if event_open:
        print()
        print("  NOTE: an event was still latched when capture ended.")
        print("  Its summary was not printed -- the CSV has the record, and")
        print("  registers 55-58 still hold it. Clear with clear-los.")

    print()
    print("%d polls in %.1f s (%.1f Hz), %d completed event(s)"
          % (polls, elapsed, polls / elapsed if elapsed else 0, events))
    return events


def format_faults_short(value):
    if not value:
        return "none"
    lost, advisory = ctx.decode_faults(value)
    parts = ["%s!" % n for n, _ in lost] + [n for n, _ in advisory]
    return " ".join(parts)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--id", type=int, default=71, dest="slave_id")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument("--out", required=True, help="CSV file to write")
    parser.add_argument("--duration", type=float, default=None,
                        help="seconds to capture (default: until Ctrl-C)")
    parser.add_argument("--quiet", action="store_true",
                        help="suppress per-transition lines")
    parser.add_argument("--chunk", type=int, default=0, metavar="N",
                        help="read the map in N-register chunks instead of "
                             "one 64-register request. Shorter frames "
                             "survive a noisy line better. NOT atomic -- an "
                             "event can straddle the split, so prefer the "
                             "single read for drop captures. Try 16 if you "
                             "get checksum errors.")
    parser.add_argument("--retries", type=int, default=3,
                        help="attempts per read before giving up (default 3)")
    args = parser.parse_args(argv)

    try:
        instrument = ctx.connect(args.port, args.slave_id, args.baud)
    except ImportError:
        raise SystemExit("minimalmodbus is not installed "
                         "(pip install minimalmodbus)")

    ctx.READ_CHUNK = max(0, args.chunk)
    ctx.READ_RETRIES = max(1, args.retries)

    # Same refusal as the client. Capturing a map-8 device would produce
    # a CSV that looks valid and is not.
    #
    # This read used to be unprotected, so one corrupted frame on connect
    # ended the run with a raw minimalmodbus traceback and 133 bytes of
    # hex -- at a drop rig, with the unit already rigged. It now retries
    # and, failing that, says what to check.
    try:
        ctx.check_map_version(ctx.read_all(instrument))
    except SystemExit:
        raise                                   # version refusal: already clear
    except Exception as exc:
        raise SystemExit(ctx.describe_read_failure(exc))

    with open(args.out, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(HEADER)
        capture(instrument, writer, args.duration, args.quiet)

    print("CSV written to %s" % args.out)
    print("Record the run in docs/DROP_TEST_PROTOCOL.md -- the CSV is")
    print("evidence only once someone writes down what was dropped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
