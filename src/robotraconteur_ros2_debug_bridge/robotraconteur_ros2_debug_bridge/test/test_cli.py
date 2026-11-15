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
B10 acceptance: every subcommand of tools/robotraconteur_ros2_debug_cli.py.

The tool runs as a subprocess against the test host over LocalTransport
(the only transport it starts for an rr+local URL; it never runs
discovery). Registered once per RMW.
"""

import json
import os
import subprocess
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']
CLI = os.environ['DEBUG_CLI']


@pytest.fixture(scope='module')
def url():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'cli') as h:
        yield bridge_host.url(h[0])


def cli(url, *args, timeout=60):
    r = subprocess.run([sys.executable, CLI, url, *args], capture_output=True, text=True,
                       timeout=timeout)
    assert r.returncode == 0, 'cli %s: %s\n%s' % (args, r.stdout, r.stderr)
    return [json.loads(line) if line[:1] in '{[' else line
            for line in r.stdout.splitlines() if line.strip()]


def test_graph(url):
    g = cli(url, 'graph')[0]
    assert '/test_talker' in g['nodes']
    assert '/test/chatter' in {t['name'] for t in g['topics']}


def test_echo_pipe_and_wire(url):
    got = cli(url, 'echo', '/test/chatter', '--type', 'std_msgs/String', '-n', '3')
    assert len(got) == 3 and all(m['data'].startswith('hello ') for m in got)
    got = cli(url, 'echo', '/test/joint_states', '--wire', '-n', '2')
    assert len(got) == 2 and got[0]['name'] == ['j1', 'j2']


def test_pub_three_ways(url):
    for via in ['function', 'pipe', 'wire']:
        echo = subprocess.Popen(
            [sys.executable, CLI, url, 'echo', '/test/echo_chatter', '--type',
             'std_msgs/String', '-n', '1'], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        # Keep publishing until the echo has one (its subscription takes a moment).
        for _ in range(20):
            cli(url, 'pub', '/test/in_chatter', 'std_msgs/String',
                json.dumps({'data': 'from ' + via}), '--via', via, '-n', '3', '-r', '20')
            if echo.poll() is not None:
                break
        out, err = echo.communicate(timeout=30)
        assert echo.returncode == 0, err
        assert json.loads(out.splitlines()[0])['data'] == 'from ' + via


def test_call(url):
    res = cli(url, 'call', '/test/add_two_ints', 'example_interfaces/AddTwoInts',
              '{"a": 40, "b": 2}')[0]
    assert res['sum'] == 42


def test_param(url):
    assert 'speed' in cli(url, 'param', 'list', '/test_params')[0]
    assert cli(url, 'param', 'get', '/test_params', 'name')[0] == {'name': 'robot'}
    assert cli(url, 'param', 'set', '/test_params', 'speed', '3.25')[0]['successful']
    assert cli(url, 'param', 'get', '/test_params', 'speed')[0] == {'speed': 3.25}
    d = cli(url, 'param', 'describe', '/test_params', 'fixed')[0]
    assert d[0]['read_only']


def test_action(url):
    statuses = cli(url, 'action', '/fibonacci', 'action_tutorials_interfaces/Fibonacci',
                   '{"order": 5}')
    assert statuses[0]['status'] == 1 and statuses[-1]['status'] == 4
    assert statuses[-1]['result']['sequence'] == [0, 1, 1, 2, 3, 5]
    out = cli(url, 'action', '/fibonacci', 'action_tutorials_interfaces/Fibonacci',
              '{"order": 40}', '--cancel-after', '2')
    assert out[-1] == 'canceled'


def test_load_components_unload(url):
    res = cli(url, 'load', '/test_container', 'demo_nodes_cpp', 'demo_nodes_cpp::Talker',
              '--name', 'loaded_by_cli')[0]
    assert res['success'] and res['full_node_name'] == '/loaded_by_cli'
    listed = cli(url, 'components', '/test_container')[0]
    assert '/loaded_by_cli' in listed['full_node_names']
    assert cli(url, 'unload', '/test_container', str(res['unique_id']))[0]['success']
    assert '/loaded_by_cli' not in cli(url, 'components', '/test_container')[0][
        'full_node_names']


def test_errors_fail_cleanly(url):
    r = subprocess.run([sys.executable, CLI, url, 'echo', '/test/secret', '--type',
                        'std_msgs/String'], capture_output=True, text=True, timeout=60)
    assert r.returncode != 0 and 'not available' in r.stderr
