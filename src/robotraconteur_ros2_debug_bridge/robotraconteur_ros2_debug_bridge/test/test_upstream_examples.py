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
B4 acceptance: the upstream bridge's turtlesim topic examples, unchanged but for the URL.

examples/turtlesim_subscriber.py and turtlesim_publisher.py from the
upstream bridge run against this bridge (with the test host's stand-in
turtle) after two substitutions: the URL, and the client star import,
replaced by upstream_example_preamble (same names; local transports only,
through the safety helper; CLAUDE.md S2). Nothing else in the scripts
changes. Registered once per RMW.
"""

import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402

EXE = os.environ['TOPIC_TEST_HOST_EXE']
EXAMPLES = os.path.join(os.environ['UPSTREAM_BRIDGE_DIR'], 'examples')
TEST_DIR = os.path.dirname(os.path.abspath(__file__))
UPSTREAM_URL = 'rr+tcp://localhost:34572?service=ros2_bridge'
IMPORT_LINE = re.compile(r'^from RobotRaconteur\.\w+ import \*$', re.M)


def adapted(example, node_name):
    """Return a copy of the example with the URL and the client import replaced."""
    with open(os.path.join(EXAMPLES, example)) as f:
        src = f.read()
    assert src.count(UPSTREAM_URL) == 1, example
    assert len(IMPORT_LINE.findall(src)) == 1, example
    out = src.replace('"' + UPSTREAM_URL + '"', repr(bridge_host.url(node_name)))
    out = IMPORT_LINE.sub('from upstream_example_preamble import *', out)
    # Nothing else differs, and no default-flag client setup is left.
    assert out.count('\n') == src.count('\n')
    assert 'RobotRaconteur' not in IMPORT_LINE.sub('', out.replace('import RobotRaconteur', ''))
    path = os.path.join(tempfile.mkdtemp(), example)
    with open(path, 'w') as f:
        f.write(out)
    return path


def run_for(path, seconds):
    env = dict(os.environ, PYTHONPATH=TEST_DIR + os.pathsep + os.environ.get('PYTHONPATH', ''))
    proc = subprocess.Popen([sys.executable, '-u', path], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, env=env)
    return proc, time.monotonic() + seconds


def finish(proc, deadline):
    try:
        out, _ = proc.communicate(timeout=max(0.1, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        proc.kill()  # the examples loop forever
        out, _ = proc.communicate()
    return out


def test_turtlesim_examples():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'examples') as (name, _):
        # Subscriber: prints the stand-in turtle's pose.
        proc, deadline = run_for(adapted('turtlesim_subscriber.py', name), 8)
        out = finish(proc, deadline)
        assert re.search(r'pose: x: [0-9.]+, y: [0-9.]+, theta: ', out), out

        # Publisher: random velocity commands; the turtle's pose reports the
        # last command's velocities.
        proc, deadline = run_for(adapted('turtlesim_publisher.py', name), 8)
        try:
            with bridge_host.client(name, 'examples') as (node, c):
                sub = c.get_subscribers(c.subscribe('/turtle1/pose', 'turtlesim/Pose'))
                w = sub.subscriberwire.Connect()
                bridge_host.wait_for(
                    lambda: w.InValueValid and w.InValue.linear_velocity != 0.0,
                    timeout=8, what='a velocity command reaching the turtle')
        finally:
            out = finish(proc, deadline)
        assert 'Traceback' not in out, out
