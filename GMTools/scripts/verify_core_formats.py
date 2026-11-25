"""Focused, opt-in verification of the GMTools CLI. Python 3.9+; ObsPy is optional."""
import argparse
from datetime import datetime, timedelta, timezone
from fractions import Fraction
import hashlib
from io import BytesIO
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys

HELENA = "RSN1_HELENA.A_A-HMC180.AT2"
SAC = "IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac"
HUALIEN = "20240902_Hualian_TAPBA0.mseed"
WUHAN_SHA = "7f1b574adbdb3ffa8db174547a9e4d40245270cf88e3e5a31d7f4f37dbb3bfe3"


class Skipped(Exception):
    pass


def check(condition, message):
    if not condition:
        raise ValueError(message)


class Run:
    def __init__(self, args):
        self.exe = args.gmtools.resolve(strict=True)
        self.examples = args.examples.resolve(strict=True)
        self.modified = args.modified
        self.out = args.output.resolve()
        check(self.examples not in self.out.parents, "output directory must be outside the examples")
        self.out.mkdir(parents=True, exist_ok=False)

    def cli(self, *args, reject=False):
        argv = [str(self.exe), *map(str, args)]
        p = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
        with (self.out / "commands.log").open("a", encoding="utf-8") as log:
            print(json.dumps(argv, ensure_ascii=False), p.returncode, p.stdout, p.stderr, sep="\n", file=log)
        check((p.returncode != 0) == reject, f"unexpected exit {p.returncode}: {argv}\n{p.stderr}")
        return p

    def waveform(self, src, filename, *args):
        dest = self.out / filename
        self.cli("convert", src, "--to", "csv", "--text-header", "metadata", "-o", dest, *args)
        return load_waveform(dest)


def load_waveform(path):
    with path.open(encoding="utf-8") as stream:
        check(stream.readline().rstrip() == "# GMTools waveform 1", f"{path}: marker")
        line = stream.readline()
        check(line.startswith("# "), f"{path}: JSON header")
        meta = json.loads(line[2:])
        check(stream.readline().rstrip() == meta["columns"], f"{path}: columns")
        samples = [float(line) for line in stream]  # these checks request amplitude-only CSV
    check(len(samples) == meta["npts"], f"{path}: sample count")
    return meta, samples


def same_samples(actual, expected, tolerance=0):
    check(len(actual) == len(expected), f"sample counts {len(actual)} and {len(expected)} differ")
    for i, (a, b) in enumerate(zip(actual, expected)):
        check(math.isclose(a, b, rel_tol=tolerance), f"sample {i + 1}: {a} != {b}")


def text_inputs(run):
    src = run.examples / HELENA
    lines = src.read_text(encoding="ascii").splitlines()
    npts, dt = re.search(r"NPTS=\s*(\d+),\s*DT=\s*([\d.]+)", lines[3]).groups()
    meta, values = run.waveform(src, "helena.csv")
    check(meta["unit"] == "g" and meta["dt"] == float(dt) and len(values) == int(npts), "AT2 header")
    same_samples(values, [float(x) for line in lines[4:] for x in line.split()])

    src = run.examples / "Trinidad.dat"
    times, amplitudes = zip(*(map(float, row.split()) for row in src.read_text(encoding="ascii").splitlines()[5:]))
    profile = run.examples / "trinidad.profile.json"
    dest = run.out / "trinidad.txt"
    run.cli("convert", src, "--profile", profile, "--to", "txt", "-o", dest)
    same_samples([float(x) for x in dest.read_text().splitlines()], amplitudes)
    meta, _ = run.waveform(src, "trinidad.csv", "--profile", profile)
    check(meta["relative_start"] == times[0] and meta["unit"] == "g", "Trinidad metadata")
    same_samples([times[0] + i * meta["dt"] for i in range(len(times))], times, 1e-12)


def sac_input(run):
    src = run.examples / SAC
    data = src.read_bytes()
    year, doy, hour, minute, sec, ms, version = struct.unpack_from("<7i", data, 280)
    npts, = struct.unpack_from("<i", data, 316)
    check(version == 6 and len(data) == 632 + npts * 4, "expected Taiwan little-endian v6 sample")
    dt, = struct.unpack_from("<f", data)
    begin, = struct.unpack_from("<f", data, 20)
    start = datetime(year, 1, 1, tzinfo=timezone.utc) + timedelta(days=doy - 1, hours=hour, minutes=minute,
                                                                   seconds=sec + begin, milliseconds=ms)
    meta, values = run.waveform(src, "sac.csv")
    same_samples(values, struct.unpack_from(f"<{npts}f", data, 632))
    check(meta["dt"] == dt and meta["relative_start"] == begin, "SAC dt/B")
    check(meta["start_utc"] == start.strftime("%Y-%m-%dT%H:%M:%S.%f") + "000Z", "SAC NZ + B")
    for key, word in (("station.code", 110), ("station.location", 116), ("channel.code", 150), ("station.network", 152)):
        check(meta[key] == data[word * 4:word * 4 + 8].decode("ascii").strip(" \0"), f"SAC {key}")


def obspy_reader():
    try:
        from obspy import read
    except ImportError as e:
        raise Skipped(f"optional ObsPy unavailable: {e}") from e
    return read


def standard_mseed(run):
    if not any(row.startswith("mseed\tread\t") for row in run.cli("formats").stdout.splitlines()):
        raise Skipped("build has no standard miniSEED reader")
    read = obspy_reader()
    src = run.examples / HUALIEN
    reference = {t.id: t for t in read(str(src), format="MSEED")}
    folder = run.out / "hualien"
    run.cli("convert", src, "--to", "csv", "--text-header", "metadata", "-o", folder)
    check(len(reference) == 28, "expected the Hualien 28-channel record")
    for path in folder.glob("*.csv"):
        meta, values = load_waveform(path)
        key = ".".join(meta[k] for k in ("station.network", "station.code", "station.location", "channel.code"))
        trace = reference.pop(key)
        check(meta["dt"] == trace.stats.delta, f"{key}: dt")
        check(meta["start_utc"] == trace.stats.starttime.strftime("%Y-%m-%dT%H:%M:%S.%f") + "000Z", f"{key}: UTC")
        same_samples(values, trace.data)
    check(not reference, f"missing channels: {list(reference)}")


def history(run, src, counts, coefficient, calibration_source, *options):
    prefix = "manual" if calibration_source == "manual" else "wuhan"
    calibrated = f"{prefix}-calibrated.csv"
    meta, values = run.waveform(src, calibrated, *options, "--numeric-factor", 2, "--output-unit", "cm/s2")
    expected = [float(x) * coefficient * 2 * 100 for x in counts]
    same_samples(values, expected, 5e-15)
    p = meta["provenance"]
    steps = [{"type": "calibration", "source": calibration_source, "factor": coefficient, "unit": "m/s²"},
             {"type": "scale", "factor": 2}, {"type": "unit_conversion", "from": "m/s²", "to": "cm/s²"}]
    check(meta["unit"] == "cm/s²" and p["steps"] == steps and p["original_unit"] == p["original_quantity"] == "counts",
          "current unit or processing history")
    written = run.out / calibrated
    same, restored = run.waveform(written, f"{prefix}-unchanged.csv")
    check(same["provenance"] == p, "reopening changed history")
    same_samples(restored, values)
    new, scaled = run.waveform(written, f"{prefix}-again.csv", "--numeric-factor", 3, "--output-unit", "g")
    check(new["unit"] == "g" and new["provenance"]["steps"] == steps + [
        {"type": "scale", "factor": 3}, {"type": "unit_conversion", "from": "cm/s²", "to": "g"}], "re-export history")
    same_samples(scaled, [x * 3 * .01 / 9.80665 for x in expected], 5e-15)
    run.cli("info", written, "--quantity", "counts", "--input-unit", "counts", reject=True)
    run.cli("info", written, "--input-unit", "counts", "--calibration", "manual",
            "--calibration-factor", 1, "--calibration-unit", "g", reject=True)


def legacy_and_history(run):
    src = run.out / "legacy.csv"
    header = {"name": "fixture", "npts": 3, "quantity": "counts", "unit": "counts", "dt": .01,
              "columns": "amplitude", "delimiter": "comma", "time_basis": "values"}
    def fixture(path):
        path.write_text("# GMTools waveform 1\n# " + json.dumps(header) + "\namplitude\n1\n-2\n5\n", encoding="utf-8")

    fixture(src)
    meta, raw = run.waveform(src, "raw.csv")
    check("provenance" not in meta and raw == [1, -2, 5], "legacy v1 input")
    run.cli("info", src, "--numeric-factor", 2, "--quantity", "acceleration", "--input-unit", "g", reject=True)
    history(run, src, raw, .25, "manual", "--calibration", "manual", "--calibration-factor", .25,
            "--calibration-unit", "m/s2")
    header["provenance"] = {"steps": [{"type": "scale", "factor": "two"}]}
    invalid = run.out / "invalid-history.csv"
    fixture(invalid)
    check("invalid waveform provenance" in run.cli("info", invalid, reject=True).stderr, "damaged history diagnostic")


def wuhan(run):
    if not run.modified:
        raise Skipped("no local --modified Wuhan record supplied")
    if not run.modified.is_file():
        raise Skipped(f"Wuhan file not found: {run.modified}")
    data = run.modified.read_bytes()
    check(hashlib.sha256(data).hexdigest() == WUHAN_SHA, "--modified differs from the documented Wuhan fixture")
    return data


def modified_calibration(run):
    data = wuhan(run)
    read = obspy_reader()
    standard = bytearray()
    offset = 0
    while offset < len(data):
        size = 1 << data[offset + 54]
        standard.extend(b"000001" + data[offset + 6:offset + size])
        offset += size
    stream = read(BytesIO(standard), format="MSEED")
    stream.merge(method=-1)
    north, = stream.select(id="WH.XY001.40.EIN")
    sensitivity = struct.unpack_from(">I", data, 60)[0] & 0x3fffffff
    check(sensitivity == 5000000 and len(north) == 240000, "Wuhan calibration reference")
    history(run, run.modified, north.data, 100 / sensitivity, "Modified miniSEED",
            "--channel", 1, "--calibration", "source")


def modified_timing(run):
    data = wuhan(run)
    first = data[:1 << data[54]]
    n, = struct.unpack_from(">H", first, 30)

    def packet(ticks, rate):
        p = bytearray(first)
        secs, frac = divmod(ticks or 0, 10000)  # exact BTime ticks since midnight
        struct.pack_into(">HHBBBBH", p, 20, 2024, 1, 0 if ticks is not None else 99, secs // 60, secs % 60, 0, frac)
        struct.pack_into(">hh", p, 32, rate, 1)
        struct.pack_into(">i", p, 40, 0)
        return p

    single = run.out / "single.mseed"
    single.write_bytes(packet(0, 100))
    _, block = run.waveform(single, "block.csv")
    exact = run.out / "continuous.mseed"
    exact.write_bytes(packet(0, 100) + packet(n * 100, 100))
    p = run.cli("info", exact)
    check("Traces: 1\n" in p.stdout and f"  samples = {2*n}\n" in p.stdout and "residual_ns" not in p.stdout
          and "joined within" not in p.stderr, "exact continuity")
    meta, values = run.waveform(exact, "continuous.csv")
    check(meta["start_utc"] == "2024-01-01T00:00:00.000000000Z" and meta["dt"] == .01, "continuous timeline")
    same_samples(values, block * 2)

    step = Fraction(n * 10000, 3)
    ticks = [0, math.ceil(step), math.ceil(2 * step)]
    residual = round(max(abs(t - i * step) for i, t in enumerate(ticks)) * 100000)
    gap = math.ceil(3 * step) + 2  # more than 100 us from the expected start
    f = run.out / "segments.mseed"
    f.write_bytes(b"".join(packet(t, 3) for t in [*ticks, gap, None]))
    p = run.cli("info", f)
    check(re.findall(r"^  samples = (\d+)$", p.stdout, re.M) == list(map(str, [3*n, n, n])), "split boundaries")
    check(re.findall(r"modified\.max_start_residual_ns = (\d+)", p.stdout) == [str(residual)], "segment maximum")
    check(p.stderr.count("joined within") == 1 and "channel 1: record 2:" in p.stderr, "residual warning location")
    check("unknown BTime" in p.stderr and p.stdout.count("  start = ") == 2, "unknown-time segment")
    gap_start = datetime(2024, 1, 1) + timedelta(microseconds=gap * 100)
    starts = (meta["start_utc"], gap_start.strftime("%Y-%m-%dT%H:%M:%S.%f") + "000Z", None)
    for channel, (start, blocks) in enumerate(zip(starts, (3, 1, 1)), 1):
        meta, values = run.waveform(f, f"segment-{channel}.csv", "--channel", channel)
        check(meta.get("start_utc") == start and meta["dt"] == 1 / 3, "segment timeline")
        same_samples(values, block * blocks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gmtools", type=Path, required=True, help="built CLI executable")
    parser.add_argument("--examples", type=Path, required=True, help="GMTools example directory")
    parser.add_argument("--output", type=Path, required=True, help="new output directory")
    parser.add_argument("--modified", type=Path, help="optional original Wuhan wc file")
    try:
        run = Run(parser.parse_args())
    except (OSError, ValueError) as e:
        parser.exit(2, f"BLOCKED: {e}\n")
    failed = False
    for label, fn in (("Helena and Trinidad", text_inputs), ("Taiwan SAC", sac_input),
                      ("legacy v1 and provenance", legacy_and_history), ("Hualien miniSEED", standard_mseed),
                      ("Wuhan calibration", modified_calibration), ("Modified BTime residuals", modified_timing)):
        try:
            fn(run)
        except Skipped as e:
            print(f"SKIPPED {label}: {e}")
        except Exception as e:
            print(f"FAIL {label}: {e}", file=sys.stderr)
            failed = True
        else:
            print(f"PASS {label}")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
