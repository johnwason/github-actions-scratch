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

"""Start a bridge test process (topic_test_host) and connect to it safely."""

import contextlib
import os
import signal
import subprocess
import tempfile
import threading
import time

import RobotRaconteur as RR
import rr_test_safety

# Output of the running test host (for failure messages).
HOST_OUTPUT = []
# The running test host process (benchmark CPU time).
HOST_PROCESS = []

# How test hosts are stopped: SIGINT (Ctrl+C, rclcpp's signal handler), or on
# Windows, where Python cannot send SIGINT to one process, by closing their
# stdin (--stop-on-stdin-close, src/stdin_stop.hpp; docs/windows.md).
STOP_ON_STDIN_CLOSE = '--stop-on-stdin-close'
STOP_WITH_STDIN = os.name == 'nt'


def start_args(stdin_stop=STOP_WITH_STDIN):
    """Extra arguments and Popen keywords for a process stopped by stop()."""
    if stdin_stop:
        return [STOP_ON_STDIN_CLOSE], {'stdin': subprocess.PIPE}
    return [], {}


def stop(proc, stdin_stop=STOP_WITH_STDIN):
    """Ask a test host (or debug_bridge) to stop cleanly."""
    if stdin_stop:
        proc.stdin.close()
        proc.stdin = None  # communicate() must not touch it
    else:
        proc.send_signal(signal.SIGINT)


# The B4 tests' exposure rules.
TOPICS_CONFIG = """
capabilities: {publish: true, call_service: true, serve_service: true, set_parameter: true,
               action: true, load_component: true}
exposure:
  nodes:
    allow: ['/test_*', '/dynamic_node', '/turtlesim_standin', '/loaded_*']
    deny: ['/test_echo']
  topics:
    allow: ['/test/chatter', '/test/joint_states', '/test/latched', '/test/echo_*',
            '/turtle1/pose', '/test/dynamic_topic', '/test/served_result',
            '/test/fibonacci_events']
    publish: ['/test/in_*', '/turtle1/cmd_vel', '/test/control', '/test/call_served']
    deny: ['/test/secret']
  services:
    allow: ['/test_talker/**']
    call: ['/test/add_two_ints', '/test/slow_add', '/test/missing', '/test/denied_svc']
    serve: ['/test/served']
    deny: ['/test_talker/set_parameters*', '/test/denied_svc']
  actions:
    allow: ['/fib']
    send: ['/fibonacci']
  parameters:
    allow: ['/test_talker', '/test_params']
    set: ['/test_params']
  components:
    allow: ['/test_container']
"""


@contextlib.contextmanager
def running_host(exe, config_body, tag, robotraconteur_extra='', ros_args=(), quiet=False):
    """Run `exe` with a config (unique node name); yields (node_name, output)."""
    node_name = 'bridge_%s_%d' % (tag, os.getpid())
    with tempfile.NamedTemporaryFile('w', suffix='.yaml', delete=False) as f:
        f.write('robotraconteur:\n  node_name: %s\n' % node_name)
        f.write(robotraconteur_extra)
        f.write(config_body)
        config = f.name
    stop_args, stop_kw = start_args()
    proc = subprocess.Popen(
        [exe, *stop_args, '--ros-args', '-p', 'config_file:=' + config, *ros_args],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **stop_kw)
    output = []
    HOST_OUTPUT[:] = [output]
    HOST_PROCESS[:] = [proc]
    reader = threading.Thread(target=lambda: output.extend(proc.stdout), daemon=True)
    reader.start()
    try:
        deadline = time.monotonic() + 60
        while not any('READY' in line for line in list(output)):
            if proc.poll() is not None or time.monotonic() > deadline:
                raise AssertionError('test host did not start:\n' + ''.join(output))
            time.sleep(0.1)
        yield node_name, output
    finally:
        if proc.poll() is None:
            stop(proc)
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise AssertionError(
                    'test host did not exit within 30 s of being stopped:\n' + ''.join(output))
        reader.join(timeout=5)
        if not quiet or proc.returncode != 0:
            print('---- test host output ----\n' + ''.join(output))
        os.unlink(config)
        assert proc.returncode == 0, ''.join(output)


def url(node_name):
    return 'rr+local:///?nodename=%s&service=ros2_debug_bridge' % node_name


def connect(node, node_name, timeout=30.0):
    deadline = time.monotonic() + timeout
    while True:
        try:
            return node.ConnectService(url(node_name))
        except Exception:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.2)


@contextlib.contextmanager
def client(node_name, tag):
    """Connect a client node (safety helper) to the bridge; yield (node, root)."""
    node = RR.RobotRaconteurNode()
    node.Init()
    with rr_test_safety.make_node_setup(
            'client_%s_%d' % (tag, os.getpid()), rr_test_safety.client_flags(), node=node):
        root = connect(node, node_name)
        try:
            yield node, root
        finally:
            try:
                node.DisconnectService(root)
            except Exception:
                pass


def wait_for(predicate, timeout=10.0, what='condition'):
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() > deadline:
            tail = ''.join(HOST_OUTPUT[0][-60:]) if HOST_OUTPUT else ''
            raise AssertionError('timed out waiting for ' + what + '\ntest host output:\n' + tail)
        time.sleep(0.05)
