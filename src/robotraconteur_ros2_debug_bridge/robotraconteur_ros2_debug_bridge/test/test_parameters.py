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
B7 acceptance: parameters of a composed node from a Python client.

List, describe, get and set, parameter events, a read-only parameter, and
denial: setting where only reading is allowed, and nodes not listed.
Registered once per RMW.
"""

import os
import sys
import time

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']
# rcl_interfaces/msg/ParameterType
PARAMETER_BOOL, PARAMETER_INTEGER, PARAMETER_DOUBLE, PARAMETER_STRING = 1, 2, 3, 4
PARAMETER_DOUBLE_ARRAY = 8


@pytest.fixture(scope='module')
def host():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'params') as h:
        yield h


@pytest.fixture
def bridge(host):
    with bridge_host.client(host[0], 'params') as c:
        yield c


def parameter(node, p, name, type_, field, value):
    v = node.GetStructureType('rosmsg_rcl_interfaces__ParameterValue.ParameterValue', p)()
    v.type = type_
    setattr(v, field, value)
    m = node.GetStructureType('rosmsg_rcl_interfaces__Parameter.Parameter', p)()
    m.name = name
    m.value = v
    return m


def test_list_describe_get(bridge):
    node, c = bridge
    p = c.get_parameters()
    nodes = bridge_host.wait_for(
        lambda: (lambda n: n if '/test_params' in n else None)(list(p.nodes())),
        what='parameter nodes')
    assert '/test_talker' in nodes
    assert '/test_math' not in nodes
    names = set(p.list('/test_params', [], 0).names)
    assert {'speed', 'name', 'enabled', 'gains', 'fixed'} <= names
    d = p.describe('test_params', ['fixed', 'speed'])  # a relative name works too
    assert d[0].name == 'fixed' and d[0].read_only and d[0].type == PARAMETER_INTEGER
    assert d[0].description == 'cannot change'
    assert d[1].type == PARAMETER_DOUBLE and not d[1].read_only
    v = p.get('/test_params', ['speed', 'name', 'enabled', 'gains', 'fixed'])
    assert (v[0].type, v[0].double_value) == (PARAMETER_DOUBLE, 1.5)
    assert (v[1].type, v[1].string_value) == (PARAMETER_STRING, 'robot')
    assert (v[2].type, bool(v[2].bool_value)) == (PARAMETER_BOOL, True)
    assert v[3].type == PARAMETER_DOUBLE_ARRAY and list(v[3].double_array_value) == [1, 2, 3]
    assert (v[4].type, v[4].integer_value) == (PARAMETER_INTEGER, 7)
    # Reading is allowed on the talker too.
    assert 'use_sim_time' in set(p.list('/test_talker', [], 0).names)


def test_set_and_events(bridge):
    node, c = bridge
    p = c.get_parameters()
    events = p.events.Connect(-1)
    r = p.set('/test_params', [
        parameter(node, p, 'speed', PARAMETER_DOUBLE, 'double_value', 2.5),
        parameter(node, p, 'gains', PARAMETER_DOUBLE_ARRAY, 'double_array_value',
                  np.array([4.0, 5.0])),
        parameter(node, p, 'fixed', PARAMETER_INTEGER, 'integer_value', 8),
    ])
    assert r[0].successful and r[1].successful
    assert not r[2].successful and r[2].reason  # read only
    v = p.get('/test_params', ['speed', 'gains', 'fixed'])
    assert v[0].double_value == 2.5
    assert list(v[1].double_array_value) == [4.0, 5.0]
    assert v[2].integer_value == 7
    changed = {}
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline and 'speed' not in changed:
        while events.Available > 0:
            ev = events.ReceivePacket()
            if ev.node == '/test_params':
                for cp in ev.changed_parameters:
                    changed[cp.name] = cp.value
        time.sleep(0.05)
    assert changed['speed'].double_value == 2.5
    events.Close()


def test_denied(bridge):
    node, c = bridge
    p = c.get_parameters()
    with pytest.raises(Exception) as e:
        p.set('/test_talker', [parameter(node, p, 'use_sim_time', PARAMETER_BOOL,
                                         'bool_value', True)])
    assert 'not available for setting' in str(e.value)
    for denied in ['/test_math', '/robotraconteur_ros2_debug_bridge', '/nonexistent']:
        with pytest.raises(Exception) as e:
            p.get(denied, ['use_sim_time'])
        assert 'are not available' in str(e.value), denied
