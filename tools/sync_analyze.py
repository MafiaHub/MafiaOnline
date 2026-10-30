#!/usr/bin/env python3
"""Sync trace analyzer (stdlib only; run with python -I).

Reads the JSONL files the clients' sync trace writes (logs/synctrace/
<role>-<pid>.jsonl next to each client), joins every observer's shown poses
with the simulating process's truth by entity and time, and reports per entity
kind and observer:

  rep err     shown position against the truth at the owner clock the shown
              pose stands for (pure presentation error, delay removed)
  now err     shown position against the truth at the same wall time (what
              the delay costs)
  d2 obs      per-frame second difference of the shown position, mm: the
              wobble a viewer sees
  d2 ideal    the same for the truth sampled at the shown owner clocks: what
              a perfect interpolator would show on this render clock
  d2 truth    the same on the owner's own frames
  clock       owner clock steps per frame against the game frame time
  pipeline    sample modes, delay, lead, corrections, contact flips
  native      written vs after C_car::Update vs final frame position, ticks
  camera      camera wobble relative to the car (passenger view)
  arrivals    sample gaps, lateness, clock offset slews
  events      latency of each event from its first record to the others

Usage:
  python -I tools/sync_analyze.py [paths...] [--kind car] [--id N]
         [--window SECONDS] [--follow SECONDS] [--top N]

Paths are files or directories (default: builds/*/bin/logs/synctrace).
--follow re-reads the files every SECONDS and summarizes the last --window.
"""

import argparse
import bisect
import glob
import json
import math
import os
import sys
import time
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def norm(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def lerp(a, b, u):
    return (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, a[2] + (b[2] - a[2]) * u)


def qdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]


def qangle(a, b):
    """Angle between two orientations, degrees."""
    d = min(1.0, abs(qdot(a, b)))
    return math.degrees(2.0 * math.acos(d))


def qnlerp(a, b, u):
    if qdot(a, b) < 0:
        b = tuple(-x for x in b)
    q = tuple(a[i] + (b[i] - a[i]) * u for i in range(4))
    n = math.sqrt(sum(x * x for x in q)) or 1.0
    return tuple(x / n for x in q)


def qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def qinv(a):
    return (a[0], -a[1], -a[2], -a[3])


def qlog(a):
    """Rotation vector (radians) of a unit quaternion."""
    if a[0] < 0:
        a = tuple(-x for x in a)
    s = math.sqrt(a[1] * a[1] + a[2] * a[2] + a[3] * a[3])
    if s < 1e-9:
        return (0.0, 0.0, 0.0)
    angle = 2.0 * math.atan2(s, a[0])
    return (a[1] / s * angle, a[2] / s * angle, a[3] / s * angle)


def pct(values, p):
    if not values:
        return float("nan")
    values = sorted(values)
    k = (len(values) - 1) * p / 100.0
    f = math.floor(k)
    c = min(f + 1, len(values) - 1)
    return values[f] + (values[c] - values[f]) * (k - f)


def stats(values, scale=1.0, digits=1):
    if not values:
        return "-"
    v = [x * scale for x in values]
    fmt = "{:." + str(digits) + "f}"
    return "p50 " + fmt.format(pct(v, 50)) + " p95 " + fmt.format(pct(v, 95)) + " max " + fmt.format(max(v)) + " (n " + str(len(v)) + ")"


class Reader:
    """Incremental JSONL reader over a set of files."""

    def __init__(self, paths):
        self.paths = paths
        self.offsets = {}
        self.records = []
        self.roles = {}

    def files(self):
        found = []
        for path in self.paths:
            if os.path.isdir(path):
                found += glob.glob(os.path.join(path, "*.jsonl"))
            else:
                found += glob.glob(path)
        return sorted(set(found))

    def poll(self):
        for path in self.files():
            offset = self.offsets.get(path, 0)
            try:
                size = os.path.getsize(path)
            except OSError:
                continue
            if size < offset:
                offset = 0
            if size == offset:
                continue
            with open(path, "rb") as handle:
                handle.seek(offset)
                data = handle.read()
            end = data.rfind(b"\n")
            if end < 0:
                continue
            self.offsets[path] = offset + end + 1
            source = os.path.basename(path).split(".")[0]
            for raw in data[:end].split(b"\n"):
                try:
                    record = json.loads(raw)
                except ValueError:
                    continue
                if record.get("k") == "hdr":
                    self.roles[source] = record.get("role", source)
                    continue
                record["src"] = source
                self.records.append(record)

    def role(self, source):
        return self.roles.get(source, source)


class Truth:
    """One entity's truth, by owner clock and by wall time."""

    def __init__(self, records):
        dense = [r for r in records if r.get("sent") is False]
        self.sent = [r for r in records if r.get("sent") is not False]
        self.samples = sorted(dense or self.sent, key=lambda r: (r["clk"], r["t"]))
        # A clock that wraps or restarts splits the stream; keep the last run.
        self.clk = [r["clk"] for r in self.samples]
        self.by_time = sorted(self.samples, key=lambda r: r["t"])
        self.times = [r["t"] for r in self.by_time]

    def at_clock(self, clk):
        i = bisect.bisect_left(self.clk, clk)
        if i <= 0 or i >= len(self.clk):
            return None
        a, b = self.samples[i - 1], self.samples[i]
        span = b["clk"] - a["clk"]
        if span <= 0 or span > 200:
            return None
        u = (clk - a["clk"]) / span
        return lerp(a["p"], b["p"], u), qnlerp(a["q"], b["q"], u), a, b

    def at_time(self, t):
        i = bisect.bisect_left(self.times, t)
        if i <= 0 or i >= len(self.times):
            return None
        a, b = self.by_time[i - 1], self.by_time[i]
        span = b["t"] - a["t"]
        if span <= 0 or span > 200000:
            return None
        u = (t - a["t"]) / span
        return lerp(a["p"], b["p"], u), a["clk"] + (b["clk"] - a["clk"]) * u


def second_differences(points):
    """|p[i+1] - 2 p[i] + p[i-1]| for consecutive points."""
    out = []
    for i in range(1, len(points) - 1):
        out.append(norm(add(sub(points[i + 1], mul(points[i], 2.0)), points[i - 1])))
    return out


def rotation_second_differences(rotations):
    """Change of the per-frame rotation step, degrees."""
    steps = [qlog(qmul(rotations[i], qinv(rotations[i - 1]))) for i in range(1, len(rotations))]
    return [math.degrees(norm(sub(steps[i], steps[i - 1]))) for i in range(1, len(steps))]


def analyze(reader, args):
    records = reader.records
    if args.window and records:
        newest = max(r["t"] for r in records)
        records = [r for r in records if r["t"] >= newest - args.window * 1e6]
    if not records:
        print("no records")
        return
    t0 = min(r["t"] for r in records)
    truth_records = defaultdict(list)
    observed = defaultdict(list)
    received = defaultdict(list)
    events = defaultdict(list)
    for r in records:
        if args.kind and r.get("e") != args.kind:
            continue
        if args.id and r.get("id") != args.id:
            continue
        k = r.get("k")
        key = (r.get("e"), r.get("id"))
        if k == "truth":
            truth_records[key].append(r)
        elif k == "obs":
            observed[(r["src"],) + key].append(r)
        elif k == "rx":
            received[(r["src"],) + key].append(r)
        elif k == "event":
            events[(r.get("e"), r.get("id"), r.get("name"), r.get("seq"))].append(r)
    truths = {key: Truth(rs) for key, rs in truth_records.items()}

    print("=" * 78)
    print("sync trace: %d records, %.1f s, files: %s" % (len(records), (max(r["t"] for r in records) - t0) / 1e6,
                                                        ", ".join(sorted(set(reader.role(r["src"]) for r in records)))))

    for key, truth in sorted(truths.items(), key=lambda kv: str(kv[0])):
        owner = reader.role(truth.samples[0]["src"]) if truth.samples else "?"
        steps = [b["clk"] - a["clk"] - b.get("fms", b["clk"] - a["clk"]) for a, b in zip(truth.samples, truth.samples[1:])]
        jumps = [s for s in steps if s != 0]
        d2 = second_differences([r["p"] for r in truth.by_time])
        # Sent samples against the step that produced them.
        mismatch = []
        index = {r["clk"]: r for r in truth.samples}
        for s in truth.sent:
            d = index.get(s["clk"])
            if d is not None and d is not s:
                mismatch.append(norm(sub(s["p"], d["p"])))
        # Sent velocity against the motion between sent samples.
        vel = []
        for a, b in zip(truth.sent, truth.sent[1:]):
            dt = (b["clk"] - a["clk"]) / 1000.0
            if 0.0 < dt < 0.1 and norm(a.get("v", (0, 0, 0))) > 2.0:
                chord = mul(sub(b["p"], a["p"]), 1.0 / dt)
                mean = mul(add(a["v"], b["v"]), 0.5)
                vel.append(norm(sub(chord, mean)) / max(norm(mean), 1e-3))
        print("-" * 78)
        print("TRUTH %s %s owner %s: %d steps, %d sent" % (key[0], key[1], owner, len(truth.samples), len(truth.sent)))
        print("  clock jumps (step != frame ms): %d, sizes %s" % (len(jumps), stats(jumps, 1, 0)))
        print("  owner d2 mm: %s" % stats(d2, 1000))
        print("  sent pose vs its step mm: %s" % stats(mismatch, 1000, 2))
        print("  sent velocity vs chord (relative): %s" % stats(vel, 100, 2) + " %")

    for key, frames in sorted(observed.items(), key=lambda kv: str(kv[0])):
        src, kind, eid = key
        frames.sort(key=lambda r: r["t"])
        truth = truths.get((kind, eid))
        own = [r for r in frames if r.get("mode") == "own"]
        if own:
            print("-" * 78)
            print("OWN %s %s, driven by %s: %d frames (the baseline view)" % (kind, eid, reader.role(src), len(own)))
            d2_cam, acc_cam, acc_car = [], [], []
            for i in range(1, len(own) - 1):
                a, b, c = own[i - 1], own[i], own[i + 1]
                if b["f"] != a["f"] + 1 or c["f"] != b["f"] + 1 or not all("cam" in r and "wc" in r for r in (a, b, c)):
                    continue
                dt0, dt1 = b.get("fms", 0) / 1000.0, c.get("fms", 0) / 1000.0
                if dt0 <= 0 or dt1 <= 0:
                    continue
                ca, cb, cc = sub(a["cam"], a["wc"]), sub(b["cam"], b["wc"]), sub(c["cam"], c["wc"])
                d2_cam.append(norm(add(sub(cc, mul(cb, 2.0)), ca)))
                acc_cam.append(norm(sub(mul(sub(cc, cb), 1 / dt1), mul(sub(cb, ca), 1 / dt0))) / ((dt0 + dt1) / 2))
                acc_car.append(norm(sub(mul(sub(c["p"], b["p"]), 1 / dt1), mul(sub(b["p"], a["p"]), 1 / dt0))) / ((dt0 + dt1) / 2))
            print("  car accel m/s2: %s" % stats(acc_car, 1, 2))
            print("  camera-car d2 mm: %s   accel m/s2: %s" % (stats(d2_cam, 1000, 2), stats(acc_cam, 1, 2)))
        frames = [r for r in frames if r.get("mode") != "own"]
        if not frames:
            continue
        interp = [r for r in frames if "oclk" in r]
        print("-" * 78)
        print("OBSERVED %s %s by %s: %d frames (%d placed by update)" % (kind, eid, reader.role(src), len(frames), len(frames) - len(interp)))
        if not interp:
            continue
        seated = sum(1 for r in interp if r.get("seat", -1) >= 0)
        print("  local player seated in it: %d frames" % seated)
        rep, rep_deg, now_err, lag = [], [], [], []
        ideal_points, ideal_rot, moving_flags = [], [], []
        offenders = []
        for r in interp:
            if not truth:
                break
            at = truth.at_clock(r["oclk"])
            moving_flags.append(bool(at) and norm(at[2].get("v", (0, 0, 0))) > 1.0)
            if at:
                rep.append(norm(sub(r["p"], at[0])))
                rep_deg.append(qangle(r["q"], at[1]))
                ideal_points.append(at[0])
                ideal_rot.append(at[1])
            else:
                ideal_points.append(None)
                ideal_rot.append(None)
            now = truth.at_time(r["t"])
            if now:
                now_err.append(norm(sub(r["p"], now[0])))
                lag.append(now[1] - r["oclk"])
        if truth:
            print("  rep err mm: %s" % stats(rep, 1000))
            print("  rep heading err deg: %s" % stats(rep_deg, 1, 3))
            print("  now err m: %s   owner clock lag ms: %s" % (stats(now_err, 1, 2), stats(lag, 1, 1)))
        # Wobble over consecutive frames: the second difference (mm) and the
        # acceleration it implies over the game's frame times (m/s^2), for
        # the shown pose and for the truth at the same owner clocks; their
        # difference is what presentation adds.
        d2_obs, d2_ideal, d2_rot, d2_ideal_rot, d2_cam = [], [], [], [], []
        acc_obs, acc_ideal, acc_excess, acc_cam = [], [], [], []
        clock_dev, qpc_dev = [], []
        for i in range(1, len(interp) - 1):
            a, b, c = interp[i - 1], interp[i], interp[i + 1]
            if b["f"] != a["f"] + 1 or c["f"] != b["f"] + 1:
                continue
            dt0 = b.get("fms", 0) / 1000.0
            dt1 = c.get("fms", 0) / 1000.0
            if dt0 <= 0 or dt1 <= 0:
                continue
            d2 = norm(add(sub(c["p"], mul(b["p"], 2.0)), a["p"]))
            d2_obs.append(d2)
            ao = norm(sub(mul(sub(c["p"], b["p"]), 1 / dt1), mul(sub(b["p"], a["p"]), 1 / dt0))) / ((dt0 + dt1) / 2)
            acc_obs.append(ao)
            offenders.append((ao, b))
            d2_rot += rotation_second_differences([a["q"], b["q"], c["q"]])
            ia, ib, ic = ideal_points[i - 1], ideal_points[i], ideal_points[i + 1]
            if truth and ia is not None and ib is not None and ic is not None:
                d2_ideal.append(norm(add(sub(ic, mul(ib, 2.0)), ia)))
                ai_vec = sub(mul(sub(ic, ib), 1 / dt1), mul(sub(ib, ia), 1 / dt0))
                ao_vec = sub(mul(sub(c["p"], b["p"]), 1 / dt1), mul(sub(b["p"], a["p"]), 1 / dt0))
                acc_ideal.append(norm(ai_vec) / ((dt0 + dt1) / 2))
                acc_excess.append(norm(sub(ao_vec, ai_vec)) / ((dt0 + dt1) / 2))
                d2_ideal_rot += rotation_second_differences([ideal_rot[i - 1], ideal_rot[i], ideal_rot[i + 1]])
            if all("cam" in r and "wc" in r for r in (a, b, c)):
                ca, cb, cc = sub(a["cam"], a["wc"]), sub(b["cam"], b["wc"]), sub(c["cam"], c["wc"])
                d2_cam.append(norm(add(sub(cc, mul(cb, 2.0)), ca)))
                acc_cam.append(norm(sub(mul(sub(cc, cb), 1 / dt1), mul(sub(cb, ca), 1 / dt0))) / ((dt0 + dt1) / 2))
        for a, b in zip(interp, interp[1:]):
            if b["f"] == a["f"] + 1:
                clock_dev.append((b["oclk"] - a["oclk"]) - b.get("fms", 0))
                qpc_dev.append((b["oclk"] - a["oclk"]) - (b["t"] - a["t"]) / 1000.0)
        print("  d2 obs mm: %s   ideal: %s" % (stats(d2_obs, 1000, 2), stats(d2_ideal, 1000, 2)))
        print("  accel obs m/s2: %s   ideal: %s" % (stats(acc_obs, 1, 2), stats(acc_ideal, 1, 2)))
        print("  accel added by presentation m/s2: %s" % stats(acc_excess, 1, 2))
        print("  rot d2 obs deg: %s   ideal: %s" % (stats(d2_rot, 1, 3), stats(d2_ideal_rot, 1, 3)))
        print("  camera-car d2 mm: %s   accel m/s2: %s" % (stats(d2_cam, 1000, 2), stats(acc_cam, 1, 2)))
        print("  owner clock step - frame ms: %s   - wall ms: %s" % (stats(clock_dev, 1, 2), stats(qpc_dev, 1, 2)))
        if truth:
            moving = [r for r, m in zip(interp, moving_flags) if m]
            if moving:
                share = defaultdict(int)
                for r in moving:
                    share[r.get("smp")] += 1
                print("  moving (> 1 m/s) %d frames: " % len(moving) + ", ".join("%s %.1f%%" % (k, 100.0 * n / len(moving)) for k, n in sorted(share.items(), key=lambda kv: -kv[1])))
        modes = defaultdict(int)
        for r in interp:
            modes[(r.get("mode"), r.get("smp"))] += 1
        print("  modes: " + ", ".join("%s/%s %.1f%%" % (m[0], m[1], 100.0 * n / len(interp)) for m, n in sorted(modes.items(), key=lambda kv: -kv[1])))
        flips = sum(1 for a, b in zip(interp, interp[1:]) if a.get("mode") != b.get("mode"))
        print("  contact flips: %d   ticks != 1: %d" % (flips, sum(1 for r in interp if r.get("ticks", 1) != 1)))
        print("  delay ms: %s   per-frame change: %s" % (stats([r["delay"] for r in interp], 1, 1),
                                                         stats([abs(b["delay"] - a["delay"]) for a, b in zip(interp, interp[1:])], 1, 3)))
        print("  lead ms: %s   ahead of newest ms: %s" % (stats([r.get("lead", 0) for r in interp], 1, 1), stats([r.get("ahead", 0) for r in interp], 1, 1)))
        print("  correction mm: %s   deg: %s" % (stats([r.get("corr", 0) for r in interp], 1000, 1), stats([r.get("corrDeg", 0) for r in interp], 1, 3)))
        written = [norm(sub(r["wr"], r["p"])) for r in interp if "wr" in r]
        after = [norm(sub(r["nat"], r["wr"])) for r in interp if "nat" in r and "wr" in r]
        final = [norm(sub(r["fin"], r["nat"])) for r in interp if "fin" in r and "nat" in r]
        print("  native mm: written-shown %s | after Update-written %s | final-after %s" % (stats(written, 1000, 2), stats(after, 1000, 2), stats(final, 1000, 2)))
        offenders.sort(key=lambda o: -o[0])
        for value, r in offenders[:args.top]:
            print("    accel %.1f m/s2 at %+.3f s (t %d) smp %s u %.2f ahead %.1f delay %.1f lead %.1f corr %.1f mm fms %s mode %s" % (
                value, (r["t"] - t0) / 1e6, r["t"], r.get("smp"), r.get("u", 0), r.get("ahead", 0), r.get("delay", 0),
                r.get("lead", 0), r.get("corr", 0) * 1000, r.get("fms"), r.get("mode")))

    for key, rx in sorted(received.items(), key=lambda kv: str(kv[0])):
        rx.sort(key=lambda r: r["t"])
        gaps = [b["clk"] - a["clk"] for a, b in zip(rx, rx[1:])]
        late = [r["arrival"] - r["ptime"] for r in rx]
        slew = [b["offset"] - a["offset"] for a, b in zip(rx, rx[1:]) if not b.get("reset")]
        print("-" * 78)
        print("RECEIVED %s %s by %s: %d samples" % (key[1], key[2], reader.role(key[0]), len(rx)))
        print("  clock gaps ms: %s   > 50 ms: %d" % (stats(gaps, 1, 1), sum(1 for g in gaps if g > 50)))
        truth = truths.get((key[1], key[2]))
        if truth and rx:
            low, high = min(r["clk"] for r in rx), max(r["clk"] for r in rx)
            sent = sum(1 for r in truth.sent if low <= r["clk"] <= high)
            print("  delivered: %d of %d sent in that clock range (%.1f%%)" % (len(rx), sent, 100.0 * len(rx) / max(sent, 1)))
        print("  arrival - dated ms: %s   offset slew ms: %s   resets: %d" % (stats(late, 1, 2), stats([abs(s) for s in slew], 1, 3),
                                                                           sum(1 for r in rx if r.get("reset"))))

    if events:
        print("-" * 78)
        latency = defaultdict(list)
        for key, rs in events.items():
            rs.sort(key=lambda r: r["t"])
            for r in rs[1:]:
                latency[key[2]].append((r["t"] - rs[0]["t"]) / 1000.0)
        for name, values in sorted(latency.items()):
            print("EVENT %s latency ms: %s" % (name, stats(values, 1, 1)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--kind")
    parser.add_argument("--id", type=int)
    parser.add_argument("--window", type=float, default=0.0, help="only the last SECONDS")
    parser.add_argument("--follow", type=float, default=0.0, help="re-summarize every SECONDS")
    parser.add_argument("--top", type=int, default=5)
    args = parser.parse_args()
    paths = args.paths or glob.glob(os.path.join(ROOT, "builds", "*", "bin", "logs", "synctrace"))
    reader = Reader(paths)
    if args.follow:
        if not args.window:
            args.window = 10.0
        while True:
            reader.poll()
            analyze(reader, args)
            sys.stdout.flush()
            time.sleep(args.follow)
    reader.poll()
    analyze(reader, args)


if __name__ == "__main__":
    main()
