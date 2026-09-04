#!/usr/bin/env python3
"""
Poll kraken_doa_v2's DOA_value.html endpoint and forward bearings to
Chasemapper as Horus UDP JSON.

The script is intentionally Python 3.8 standard-library only.

Example:
    python3 tools/doa_value_to_chasemapper.py --server 192.168.88.2
"""

import argparse
import csv
import io
import json
import math
import socket
import sys
import time
from dataclasses import dataclass
from typing import Iterable, List, Optional
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


DEFAULT_DOA_PORT = 8081
DEFAULT_POLL_RATE_HZ = 5.0
DEFAULT_UDP_HOST = "255.255.255.255"
DEFAULT_UDP_PORT = 55672
DOA_VALUE_PATH = "/DOA_value.html"
MIN_FIELD_COUNT = 377
DOA_VALUES_START = 17
DOA_VALUES_COUNT = 360


@dataclass
class DoaRecord:
    timestamp_ms: int
    bearing_deg: int
    confidence: float
    power_db: float
    fft_peak_power_db: float
    frequency_hz: int
    antenna_type: str
    station_id: str
    latitude: float
    longitude: float
    gps_heading: float
    compass_heading: float
    heading_sensor: str
    raw_doa: List[float]


def build_doa_url(server: str, default_port: int) -> str:
    server = server.strip()
    if not server:
        raise ValueError("server must not be empty")

    if server.startswith("http://") or server.startswith("https://"):
        parsed = urlparse(server)
        if parsed.path and parsed.path != "/":
            return server
        return server.rstrip("/") + DOA_VALUE_PATH

    if "/" in server:
        return "http://" + server

    if ":" in server.rsplit("@", 1)[-1]:
        return "http://" + server.rstrip("/") + DOA_VALUE_PATH

    return "http://{}:{}{}".format(server, default_port, DOA_VALUE_PATH)


def fetch_doa_value(url: str, timeout: float) -> str:
    req = Request(
        url,
        headers={
            "User-Agent": "kraken-doa-v2-chasemapper-bridge/1.0",
            "Cache-Control": "no-cache",
            "Pragma": "no-cache",
        },
    )
    with urlopen(req, timeout=timeout) as response:
        return response.read().decode("utf-8", "replace")


def _int_field(fields: List[str], index: int) -> int:
    return int(float(fields[index].strip()))


def _float_field(fields: List[str], index: int) -> float:
    return float(fields[index].strip())


def parse_doa_value(text: str) -> List[DoaRecord]:
    if "<!DOCTYPE html" in text[:256] or "<html" in text[:256].lower():
        raise ValueError(
            "DOA_value response looks like UI HTML; use the compatibility "
            "endpoint, normally http://<server>:8081/DOA_value.html"
        )

    records = []
    reader = csv.reader(io.StringIO(text), skipinitialspace=True)
    for row in reader:
        fields = [item.strip() for item in row]
        if not fields or all(not item for item in fields):
            continue
        if len(fields) < MIN_FIELD_COUNT:
            raise ValueError(
                "expected at least {} CSV fields, got {}".format(
                    MIN_FIELD_COUNT, len(fields)
                )
            )

        raw_doa = [
            float(value)
            for value in fields[DOA_VALUES_START : DOA_VALUES_START + DOA_VALUES_COUNT]
        ]

        records.append(
            DoaRecord(
                timestamp_ms=_int_field(fields, 0),
                bearing_deg=_int_field(fields, 1) % 360,
                confidence=_float_field(fields, 2),
                power_db=_float_field(fields, 3),
                fft_peak_power_db=(
                    _float_field(fields, DOA_VALUES_START + DOA_VALUES_COUNT)
                    if len(fields) > DOA_VALUES_START + DOA_VALUES_COUNT
                    else _float_field(fields, 3)
                ),
                frequency_hz=_int_field(fields, 4),
                antenna_type=fields[5],
                station_id=fields[7] or "KrakenSDR",
                latitude=_float_field(fields, 8),
                longitude=_float_field(fields, 9),
                gps_heading=_float_field(fields, 10),
                compass_heading=_float_field(fields, 11),
                heading_sensor=fields[12],
                raw_doa=raw_doa,
            )
        )

    return records


def recalculate_legacy_confidence(raw_doa: List[float]) -> float:
    """Estimate the old krakensdr_doa confidence scale from DOA_value plot data.

    Legacy krakensdr_doa used 10*log10(max(MUSIC)/mean(MUSIC)). DOA_value.html
    only carries a shifted dB plot, so reconstruct a normalized linear spectrum
    from that plot. This is not bit-identical to the original raw MUSIC array
    because values below -100 dB were clipped before publication, but it puts
    confidence back on the same dB-like scale Chasemapper previously saw.
    """
    if not raw_doa:
        return 0.0

    peak_db = max(raw_doa)
    linear = []
    for value in raw_doa:
        if not math.isfinite(value):
            continue
        linear.append(math.pow(10.0, (value - peak_db) / 10.0))

    if not linear:
        return 0.0

    peak = max(linear)
    mean = sum(linear) / len(linear)
    if peak <= 0.0 or mean <= 0.0:
        return 0.0

    return 10.0 * math.log10(peak / mean)


def chasemapper_message(
    record: DoaRecord,
    source: Optional[str],
    bearing_type: str,
    include_extra_fields: bool,
    flip_raw_doa: bool,
    recalculate_confidence: bool,
) -> dict:
    raw_doa = list(reversed(record.raw_doa)) if flip_raw_doa else record.raw_doa
    confidence = (
        recalculate_legacy_confidence(record.raw_doa)
        if recalculate_confidence
        else record.confidence
    )

    msg = {
        "type": "BEARING",
        "bearing_type": bearing_type,
        "bearing": record.bearing_deg,
        "source": source or record.station_id or "KrakenSDR",
        "timestamp": record.timestamp_ms / 1000.0,
        "confidence": confidence,
        "power": record.fft_peak_power_db,
        "raw_bearing_angles": list(range(DOA_VALUES_COUNT)),
        "raw_doa": raw_doa,
    }

    if bearing_type == "absolute":
        msg["latitude"] = record.latitude
        msg["longitude"] = record.longitude

    if include_extra_fields:
        msg["frequency_hz"] = record.frequency_hz
        msg["antenna_type"] = record.antenna_type
        msg["gps_heading"] = record.gps_heading
        msg["compass_heading"] = record.compass_heading
        msg["heading_sensor"] = record.heading_sensor

    return msg


def make_udp_socket(host: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if host in ("255.255.255.255", "<broadcast>") or host.endswith(".255"):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    return sock


def send_messages(
    sock: socket.socket,
    messages: Iterable[dict],
    host: str,
    port: int,
    dry_run: bool,
) -> int:
    sent = 0
    target_host = "255.255.255.255" if host == "<broadcast>" else host
    for msg in messages:
        payload = json.dumps(msg, separators=(",", ":")).encode("utf-8")
        if dry_run:
            print(payload.decode("utf-8"))
        else:
            sock.sendto(payload, (target_host, port))
        sent += 1
    return sent


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0.0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Poll kraken_doa_v2 DOA_value.html and emit Chasemapper/Horus "
            "UDP bearing JSON."
        )
    )
    parser.add_argument(
        "--server",
        default="127.0.0.1",
        help=(
            "Kraken DOA server host or URL. A bare host uses port 8081 and "
            "/DOA_value.html. Default: %(default)s"
        ),
    )
    parser.add_argument(
        "--doa-port",
        type=positive_int,
        default=DEFAULT_DOA_PORT,
        help="Port used when --server is a bare host. Default: %(default)s",
    )
    parser.add_argument(
        "--poll-rate",
        type=positive_float,
        default=DEFAULT_POLL_RATE_HZ,
        help="Poll rate in Hz. Default: %(default)s",
    )
    parser.add_argument(
        "--udp-host",
        default=DEFAULT_UDP_HOST,
        help="UDP destination host. Default: %(default)s",
    )
    parser.add_argument(
        "--udp-port",
        type=positive_int,
        default=DEFAULT_UDP_PORT,
        help="UDP destination port. Default: %(default)s",
    )
    parser.add_argument(
        "--source",
        default=None,
        help="Override the Chasemapper source field. Default: station ID from DOA_value.",
    )
    parser.add_argument(
        "--bearing-type",
        choices=("relative", "absolute"),
        default="relative",
        help="Chasemapper bearing type. Default: %(default)s",
    )
    parser.add_argument(
        "--timeout",
        type=positive_float,
        default=2.0,
        help="HTTP request timeout in seconds. Default: %(default)s",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Poll once, emit any messages, then exit.",
    )
    parser.add_argument(
        "--no-dedupe",
        action="store_true",
        help="Emit identical repeated DOA_value rows. By default these are skipped.",
    )
    parser.add_argument(
        "--extra-fields",
        action="store_true",
        help="Include frequency, antenna, and heading metadata in the UDP JSON.",
    )
    parser.add_argument(
        "--no-flip-raw-doa",
        action="store_true",
        help=(
            "Do not reverse raw_doa before sending. By default raw_doa is "
            "reversed to match Chasemapper's plot orientation."
        ),
    )
    parser.add_argument(
        "--no-recalculate-confidence",
        action="store_true",
        help=(
            "Use the confidence value from DOA_value.html directly. By default "
            "confidence is recalculated from raw_doa on the old krakensdr_doa "
            "dB-like peak/average scale."
        ),
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print JSON payloads instead of sending UDP.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print polling and transmit status to stderr.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        url = build_doa_url(args.server, args.doa_port)
    except ValueError as exc:
        print("Configuration error: {}".format(exc), file=sys.stderr)
        return 2

    sock = make_udp_socket(args.udp_host)
    period = 1.0 / args.poll_rate
    last_rows = set()
    last_error = None

    if args.verbose:
        print(
            "Polling {} at {:.3f} Hz; sending to {}:{}".format(
                url, args.poll_rate, args.udp_host, args.udp_port
            ),
            file=sys.stderr,
        )

    try:
        while True:
            started = time.monotonic()
            try:
                text = fetch_doa_value(url, args.timeout)
                rows = [line.strip() for line in text.splitlines() if line.strip()]
                if not args.no_dedupe:
                    fresh_rows = [line for line in rows if line not in last_rows]
                    last_rows = set(rows)
                    text = "\n".join(fresh_rows)

                records = parse_doa_value(text) if text.strip() else []
                messages = [
                    chasemapper_message(
                        record,
                        source=args.source,
                        bearing_type=args.bearing_type,
                        include_extra_fields=args.extra_fields,
                        flip_raw_doa=not args.no_flip_raw_doa,
                        recalculate_confidence=not args.no_recalculate_confidence,
                    )
                    for record in records
                ]
                sent = send_messages(
                    sock,
                    messages,
                    args.udp_host,
                    args.udp_port,
                    args.dry_run,
                )
                if args.verbose and sent:
                    print("sent {} bearing message(s)".format(sent), file=sys.stderr)
                last_error = None
            except (HTTPError, URLError, OSError, ValueError) as exc:
                error = str(exc)
                if error != last_error:
                    print("poll/send error: {}".format(error), file=sys.stderr)
                    last_error = error

            if args.once:
                break

            elapsed = time.monotonic() - started
            time.sleep(max(0.0, period - elapsed))
    except KeyboardInterrupt:
        if args.verbose:
            print("stopped", file=sys.stderr)
        return 0
    finally:
        sock.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
