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
B9 acceptance: load a demo component, see it in the graph, unload it.

The test host is also a component container (/test_container); the bridge
uses its composition services. Registered once per RMW; the AC asks for
ASan, which the asan variant of the gate runs.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']
LOAD = 'rosservice_composition_interfaces__LoadNode.LoadNodeRequest'


@pytest.fixture(scope='module')
def host():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'components') as h:
        yield h


@pytest.fixture
def bridge(host):
    with bridge_host.client(host[0], 'components') as c:
        yield c


def load_request(node, comps, plugin='demo_nodes_cpp::Talker', name='loaded_talker'):
    r = node.GetStructureType(LOAD, comps)()
    r.package_name = 'demo_nodes_cpp'
    r.plugin_name = plugin
    r.node_name = name
    return r


def test_load_see_unload(bridge):
    node, c = bridge
    comps = c.get_components()
    assert list(bridge_host.wait_for(lambda: list(comps.containers()),
                                     what='the container')) == ['/test_container']
    res = comps.load('test_container', load_request(node, comps))
    assert res.success, res.error_message
    assert res.full_node_name == '/loaded_talker'
    bridge_host.wait_for(lambda: '/loaded_talker' in c.graph.nodes,
                         what='the component in the graph')
    listed = comps.list('/test_container')
    assert '/loaded_talker' in list(listed.full_node_names)
    assert res.unique_id in list(listed.unique_ids)
    out = comps.unload('/test_container', res.unique_id)
    assert out.success, out.error_message
    bridge_host.wait_for(lambda: '/loaded_talker' not in c.graph.nodes,
                         what='the component to leave the graph')
    assert res.unique_id not in list(comps.list('/test_container').unique_ids)


def test_failures_and_denial(bridge):
    node, c = bridge
    comps = c.get_components()
    res = comps.load('/test_container', load_request(node, comps, plugin='demo_nodes_cpp::NoSuch'))
    assert not res.success and res.error_message
    assert not comps.unload('/test_container', 987654).success
    for denied in ['/other_container', '/robotraconteur_ros2_debug_bridge']:
        with pytest.raises(Exception) as e:
            comps.list(denied)
        assert 'is not available' in str(e.value), denied
