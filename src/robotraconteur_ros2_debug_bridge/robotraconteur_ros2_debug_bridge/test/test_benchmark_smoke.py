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

"""B12: the benchmark (docs/benchmark.md) runs; one short case, pipe and wire."""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def test_benchmark_quick():
    out = subprocess.run(
        [sys.executable, os.path.join(HERE, 'benchmark.py'), '--exe',
         os.environ['BENCHMARK_HOST_EXE'], '--quick', '--duration', '1'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=240)
    print(out.stdout)
    assert out.returncode == 0, out.stdout
    rows = [line.split('|')[1].split() for line in out.stdout.splitlines()
            if line.startswith('image') and (' pipe ' in line or ' wire ' in line)]
    assert len(rows) == 4, out.stdout  # pipe and wire, fast path on and off
    for r in rows:
        assert float(r[0]) > 10, out.stdout  # received Hz, of 30
