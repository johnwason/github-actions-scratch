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
B1 acceptance: the standalone debug_bridge, reached from Python.

A Python Robot Raconteur client (dynamic types) connects over LocalTransport
and reads the root properties; Ctrl+C (SIGINT) then stops the bridge
cleanly. So does closing its stdin with --stop-on-stdin-close (the way to
stop it on Windows, where Python cannot send SIGINT to one process;
docs/windows.md). Registered once per RMW.
"""

import os
import subprocess
import sys
import tempfile
import time

import pytest
import RobotRaconteur as RR

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100
import rr_test_safety  # noqa: E402

EXE = os.environ['DEBUG_BRIDGE_EXE']
RMW = os.environ['RMW_IMPLEMENTATION']


def connect_with_retry(node, url, timeout=30.0):
    deadline = time.monotonic() + timeout
    while True:
        try:
            return node.ConnectService(url)
        except Exception:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.2)


def run_and_stop(stdin_stop):
    node_name = 'debug_bridge_standalone_%d' % os.getpid()
    with tempfile.NamedTemporaryFile('w', suffix='.yaml', delete=False) as f:
        f.write('robotraconteur:\n  node_name: %s\n' % node_name)
        config = f.name
    stop_args, stop_kw = bridge_host.start_args(stdin_stop)
    proc = subprocess.Popen(
        [EXE, *stop_args, '--ros-args', '-p', 'config_file:=' + config],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **stop_kw)
    try:
        node = RR.RobotRaconteurNode()
        node.Init()
        with rr_test_safety.make_node_setup(
                'debug_bridge_client_%d' % os.getpid(), rr_test_safety.client_flags(),
                node=node):
            url = 'rr+local:///?nodename=%s&service=ros2_debug_bridge' % node_name
            bridge = connect_with_retry(node, url)
            assert bridge.ros_node_name == 'robotraconteur_ros2_debug_bridge'
            assert bridge.ros_namespace == '/'
            assert bridge.rmw_implementation == RMW
            assert bridge.bridge_version == '0.0.0'
            assert list(bridge.capabilities) == ['read']
            node.DisconnectService(bridge)
        bridge_host.stop(proc, stdin_stop)
        try:
            output, _ = proc.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            proc.kill()
            output, _ = proc.communicate()
            raise AssertionError('debug_bridge did not exit within 30 s of being stopped; '
                                 'its output:\n' + output)
        print(output)
        assert proc.returncode == 0, output
        assert 'serving' in output
        assert 'Robot Raconteur stopped' in output
        assert 'Bridge destroyed' in output
        return output
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        os.unlink(config)


@pytest.mark.skipif(os.name == 'nt', reason='Python cannot send SIGINT to one process on '
                    'Windows; test_stdin_close_stops covers stopping (user decision, B13)')
def test_python_client_reads_root_properties():
    run_and_stop(stdin_stop=False)


def test_stdin_close_stops():
    output = run_and_stop(stdin_stop=True)
    assert 'stdin closed; shutting down' in output
