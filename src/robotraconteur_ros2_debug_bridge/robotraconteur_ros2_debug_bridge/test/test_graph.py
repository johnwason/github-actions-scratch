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
B5 acceptance: graph introspection from a Python client.

The graph shows what the exposure rules allow and nothing else; endpoint
QoS; the client sees a node and its topic appear and disappear (through
the graph_changes wire). Registered once per RMW.
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']


@pytest.fixture(scope='module')
def host():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'graph') as h:
        yield h


@pytest.fixture
def bridge(host):
    with bridge_host.client(host[0], 'graph') as c:
        yield c


def names(entities):
    return {e.name: e for e in entities}


def test_graph_shows_only_allowed(bridge):
    node, c = bridge
    # The graph can take a moment to be complete after start-up.
    g = bridge_host.wait_for(
        lambda: (lambda g: g if '/fib' in names(g.actions) else None)(c.graph),
        what='complete graph')
    assert '/test_talker' in g.nodes
    assert '/test_controller' in g.nodes
    assert '/test_echo' not in g.nodes                       # denied
    assert '/robotraconteur_ros2_debug_bridge' not in g.nodes  # not allowed
    topics = names(g.topics)
    assert list(topics['/test/chatter'].types) == ['std_msgs/msg/String']
    assert topics['/test/chatter'].publisher_count >= 1
    # Publish-only topics are visible (/test/in_chatter); denied and unlisted ones not.
    assert '/test/in_chatter' in topics
    for hidden in ['/test/secret', '/rosout', '/parameter_events',
                   '/fib/_action/feedback']:
        assert hidden not in topics, hidden
    services = names(g.services)
    assert '/test_talker/get_parameters' in services
    assert list(services['/test_talker/get_parameters'].types) == [
        'rcl_interfaces/srv/GetParameters']
    assert '/test_talker/set_parameters' not in services   # denied
    assert '/test_talker/set_parameters_atomically' not in services
    # Callable and servable services are visible too (the graph lists
    # /test/served for the math node's client); the math node's others not.
    others = {s for s in services if not s.startswith('/test_talker/')}
    assert others == {'/test/add_two_ints', '/test/slow_add', '/test/served'}, others
    actions = names(g.actions)
    assert list(actions) == ['/fib', '/fibonacci']
    assert list(actions['/fib'].types) == ['action_tutorials_interfaces/action/Fibonacci']


def test_topic_endpoints_and_qos(bridge):
    node, c = bridge
    eps = c.topic_endpoints('/test/latched')
    pubs = [e for e in eps if e.endpoint_type == 'publisher']
    assert len(pubs) == 1
    assert pubs[0].node_name == 'test_talker'
    assert pubs[0].node_namespace == '/'
    assert pubs[0].topic_type == 'std_msgs/msg/String'
    assert pubs[0].reliability == 'reliable'
    assert pubs[0].durability == 'transient_local'
    # rmw_fastrtps_cpp does not report the history of discovered endpoints.
    assert pubs[0].history in ('keep_last', 'unknown')
    if pubs[0].history == 'keep_last':
        assert pubs[0].depth == 1
    # The echo node's subscription of an echo topic: its node is hidden.
    subs = [e for e in c.topic_endpoints('/test/chatter') if e.endpoint_type == 'subscription']
    for e in subs:
        assert e.node_name in ('', 'robotraconteur_ros2_debug_bridge') or e.node_name.startswith(
            'test_'), e.node_name
    for denied in ['/test/secret', '/rosout', '/fib/_action/feedback']:
        with pytest.raises(Exception) as err:
            c.topic_endpoints(denied)
        assert 'not available' in str(err.value)


def test_entities_appear_and_disappear(bridge):
    node, c = bridge
    w = c.graph_changes.Connect()
    bridge_host.wait_for(lambda: w.InValueValid, timeout=5, what='first graph value')
    start = w.InValue.version
    assert '/dynamic_node' not in w.InValue.nodes

    pub = c.get_publishers(c.publish('/test/control', 'std_msgs/String'))
    string_type = node.GetStructureType('rosmsg_std_msgs__String.String', pub)

    def send(text):
        m = string_type()
        m.data = text
        pub.publish(m)

    def has_dynamic():
        g = w.InValue
        return '/dynamic_node' in g.nodes and '/test/dynamic_topic' in names(g.topics)

    bridge_host.wait_for(lambda: send('add') or has_dynamic(), what='dynamic node to appear')
    assert w.InValue.version > start
    added = w.InValue.version
    bridge_host.wait_for(
        lambda: send('remove') or not ('/dynamic_node' in w.InValue.nodes
                                       or '/test/dynamic_topic' in names(w.InValue.topics)),
        what='dynamic node to disappear')
    assert w.InValue.version > added
    # The property agrees with the wire.
    assert '/dynamic_node' not in c.graph.nodes
    w.Close()
