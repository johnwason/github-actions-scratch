# Copyright 2026 Wason Technology, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
B12 benchmark: large topics through the bridge (docs/benchmark.md).

Starts benchmark_host (the bridge plus a publisher) for each case and
subscribes from this process over LocalTransport, through the pipe and then
the wire. For each it reports the rate received, the latency (receive time
minus header.stamp, both system time on one machine), the host's CPU use and
the publisher's mean time in publish(). "none" is the same host with no
subscriber, the publisher's baseline.

    benchmark.py --exe <benchmark_host> [--duration S] [--quick] [--json FILE]

RMW_IMPLEMENTATION selects the RMW, as for the tests.
"""

import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

CONFIG = """
exposure:
  nodes:
    allow: ['/bench_publisher']
  topics:
    allow: ['/bench/*']
fast_path: %s
"""

# name, kind, width, height
SIZES = [
    ('image 640x480 rgb8', 'image', 640, 480),
    ('image 1920x1080 rgb8', 'image', 1920, 1080),
    ('cloud 65536 points', 'cloud', 65536, 1),
    ('cloud 262144 points', 'cloud', 262144, 1),
]
RATES = [30.0, 200.0]


def payload_bytes(kind, width, height):
    return width * height * (3 if kind == 'image' else 16)


def host_cpu_seconds(proc):
    """CPU seconds of the host process; NaN where there is no /proc (Windows)."""
    if not os.path.exists('/proc/%d/stat' % proc.pid):
        return float('nan')
    with open('/proc/%d/stat' % proc.pid) as f:
        fields = f.read().rsplit(')', 1)[1].split()
    # utime and stime: fields 14 and 15 of stat (11 and 12 after the name).
    return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')


def publish_stats(output, start, end):
    lines = [line.split() for line in list(output)[start:end] if line.startswith('BENCH ')]
    count = sum(int(f[2]) for f in lines)
    total_us = sum(int(f[2]) * float(f[4]) for f in lines)
    return count, (total_us / count if count else 0.0)


def percentile(sorted_values, q):
    if not sorted_values:
        return float('nan')
    return sorted_values[min(len(sorted_values) - 1, int(q * len(sorted_values)))]


class Recorder:

    def __init__(self):
        self.lock = threading.Lock()
        self.latencies = []
        self.recording = False

    def record(self, value):
        now = time.time()
        stamp = value.header.stamp
        latency = now - (stamp.sec + stamp.nanosec * 1e-9)
        with self.lock:
            if self.recording:
                self.latencies.append(latency)


def measure(proc, output, duration, setup):
    """Run one measurement; setup(recorder) connects and returns a close function."""
    rec = Recorder()
    close = setup(rec)
    try:
        time.sleep(1.0)  # warm up
        start_line = len(output)
        cpu0 = host_cpu_seconds(proc)
        t0 = time.monotonic()
        with rec.lock:
            rec.recording = True
        time.sleep(duration)
        with rec.lock:
            rec.recording = False
            latencies = sorted(rec.latencies)
        elapsed = time.monotonic() - t0
        cpu = (host_cpu_seconds(proc) - cpu0) / elapsed
        time.sleep(1.1)  # the next BENCH line covers the end of the window
        published, publish_us = publish_stats(output, start_line, len(output))
    finally:
        close()
    return {
        'received_hz': len(latencies) / elapsed,
        'latency_ms_p50': 1e3 * percentile(latencies, 0.5),
        'latency_ms_p90': 1e3 * percentile(latencies, 0.9),
        'latency_ms_p99': 1e3 * percentile(latencies, 0.99),
        'host_cpu_percent': 100.0 * cpu,
        'published_hz': published / (elapsed + 1.1),
        'publish_us': publish_us,
    }


def run_case(exe, kind, width, height, rate, fast_path, duration):
    topic = '/bench/' + kind
    msgtype = 'sensor_msgs/Image' if kind == 'image' else 'sensor_msgs/PointCloud2'
    ros_args = ['-p', 'kind:=' + kind, '-p', 'width:=%d' % width, '-p', 'height:=%d' % height,
                '-p', 'rate:=%s' % rate]
    results = {}
    with bridge_host.running_host(
            exe, CONFIG % ('true' if fast_path else 'false'), 'bench', ros_args=ros_args,
            quiet=True) as (node_name, output):
        proc = bridge_host.HOST_PROCESS[0]
        with bridge_host.client(node_name, 'bench') as (node, c):
            results['none'] = measure(proc, output, duration, lambda rec: (lambda: None))

            def pipe(rec):
                sub = c.get_subscribers(c.subscribe(topic, msgtype))
                ep = sub.subscriberpipe.Connect(-1)

                def received(e):
                    while e.Available > 0:
                        rec.record(e.ReceivePacket())
                ep.PacketReceivedEvent += received

                def close():
                    ep.Close()
                    sub.unsubscribe()
                return close

            def wire(rec):
                sub = c.get_subscribers(c.subscribe(topic, msgtype))
                w = sub.subscriberwire.Connect()
                w.WireValueChanged += lambda conn, value, ts: rec.record(value)

                def close():
                    w.Close()
                    sub.unsubscribe()
                return close

            results['pipe'] = measure(proc, output, duration, pipe)
            results['wire'] = measure(proc, output, duration, wire)
        fast = any('Subscribed ' + topic in line and 'fast path' in line for line in output)
    return results, fast


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    parser.add_argument('--exe', required=True)
    parser.add_argument('--duration', type=float, default=5.0)
    parser.add_argument('--quick', action='store_true', help='one small case (smoke test)')
    parser.add_argument('--json', help='write the results here')
    args = parser.parse_args()
    rmw = os.environ.get('RMW_IMPLEMENTATION', '')
    sizes = SIZES[:1] if args.quick else SIZES
    rates = RATES[:1] if args.quick else RATES
    rows = []
    print('RMW %s' % rmw)
    print('%-22s %6s %4s %5s | %8s %8s %8s %8s %8s %8s'
          % ('message', 'rate', 'fast', 'via', 'recv Hz', 'p50 ms', 'p99 ms', 'host %',
             'pub Hz', 'pub us'))
    for name, kind, width, height in sizes:
        for rate in rates:
            for fast_path in (True, False):
                results, fast = run_case(
                    args.exe, kind, width, height, rate, fast_path, args.duration)
                for via in ('none', 'pipe', 'wire'):
                    r = results[via]
                    print('%-22s %6.0f %4s %5s | %8.1f %8.2f %8.2f %8.1f %8.1f %8.1f'
                          % (name, rate, 'on' if fast else 'off', via, r['received_hz'],
                             r['latency_ms_p50'], r['latency_ms_p99'], r['host_cpu_percent'],
                             r['published_hz'], r['publish_us']))
                    sys.stdout.flush()
                    row = dict(r, message=name, bytes=payload_bytes(kind, width, height),
                               rate=rate, fast_path_config=fast_path, fast_path=fast, via=via,
                               rmw=rmw)
                    rows.append(row)
                if fast_path and not fast and rmw == 'rmw_intraprocess_cpp':
                    raise SystemExit('fast path did not activate with rmw_intraprocess_cpp')
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(rows, f, indent=1)


if __name__ == '__main__':
    main()
