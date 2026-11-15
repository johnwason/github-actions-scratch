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
B4 acceptance: topics from a Python Robot Raconteur client (dynamic types).

Subscribes to sensor_msgs/JointState and std_msgs/String through the wire
and the pipe at the same time; publishes through publish(), the pipe and
the wire; denied topics behave like ones that do not exist; a late wire
connection gets a transient-local value. Registered once per RMW.
"""

import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']


@pytest.fixture(scope='module')
def host():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'topics') as h:
        yield h


@pytest.fixture
def bridge(host):
    with bridge_host.client(host[0], 'topics') as c:
        yield c


def drain(ep):
    out = []
    while ep.Available > 0:
        out.append(ep.ReceivePacket())
    return out


def test_subscribe_wire_and_pipe_together(bridge):
    node, c = bridge
    for topic, msgtype in [('/test/chatter', 'std_msgs/String'),
                           ('/test/joint_states', 'sensor_msgs/msg/JointState')]:
        sub = c.get_subscribers(c.subscribe(topic, msgtype))
        ep = sub.subscriberpipe.Connect(-1)
        w = sub.subscriberwire.Connect()
        packets = bridge_host.wait_for(lambda: drain(ep), what=topic + ' pipe packets')
        bridge_host.wait_for(lambda: w.InValueValid, what=topic + ' wire value')
        value = w.InValue
        if msgtype == 'std_msgs/String':
            assert packets[0].data.startswith('hello ')
            assert value.data.startswith('hello ')
        else:
            assert list(packets[0].name) == ['j1', 'j2']
            assert list(value.name) == ['j1', 'j2']
            assert isinstance(value.position, np.ndarray) and value.position.shape == (2,)
            assert value.header.frame_id == ''
        # Both keep coming.
        bridge_host.wait_for(lambda: drain(ep), what='more packets')
        ep.Close()
        w.Close()
        sub.unsubscribe()


def test_msgtype_from_graph(bridge):
    node, c = bridge
    sub = c.get_subscribers(c.subscribe('/test/chatter', ''))
    w = sub.subscriberwire.Connect()
    bridge_host.wait_for(lambda: w.InValueValid, what='wire value')
    assert w.InValue.data.startswith('hello ')
    w.Close()
    sub.unsubscribe()


def _echo_receiver(c, topic, msgtype):
    sub = c.get_subscribers(c.subscribe(topic, msgtype))
    return sub, sub.subscriberpipe.Connect(-1)


def test_publish_three_ways(bridge):
    node, c = bridge
    sub, echo = _echo_receiver(c, '/test/echo_chatter', 'std_msgs/String')
    pub = c.get_publishers(c.publish('/test/in_chatter', 'std_msgs/String'))
    string_type = node.GetStructureType('rosmsg_std_msgs__String.String', pub)

    def msg(text):
        m = string_type()
        m.data = text
        return m

    seen = set()

    def received(wanted):
        seen.update(p.data for p in drain(echo))
        return wanted in seen

    # The subscription in the echo node may need a moment to match.
    bridge_host.wait_for(
        lambda: pub.publish(msg('via-function')) or received('via-function'),
        what='publish() echo')
    ep = pub.publisherpipe.Connect(-1)
    bridge_host.wait_for(
        lambda: (ep.SendPacket(msg('via-pipe')), received('via-pipe'))[1],
        what='pipe echo')
    w = pub.publisherwire.Connect()
    w.OutValue = msg('via-wire')
    bridge_host.wait_for(lambda: received('via-wire'), what='wire echo')
    ep.Close()
    w.Close()
    sub.unsubscribe()


def test_publish_joint_state(bridge):
    node, c = bridge
    sub, echo = _echo_receiver(c, '/test/echo_joint_states', 'sensor_msgs/JointState')
    pub = c.get_publishers(c.publish('/test/in_joint_states', 'sensor_msgs/JointState'))
    js_type = node.GetStructureType('rosmsg_sensor_msgs__JointState.JointState', pub)
    m = js_type()
    m.name = ['a', 'b', 'c']
    m.position = np.array([1.0, 2.0, 3.0])
    m.velocity = np.array([], dtype=np.float64)
    m.effort = np.array([], dtype=np.float64)
    got = []

    def received():
        got.extend(drain(echo))
        return any(list(g.name) == ['a', 'b', 'c'] for g in got)

    bridge_host.wait_for(lambda: pub.publish(m) or received(), what='JointState echo')
    g = [g for g in got if list(g.name) == ['a', 'b', 'c']][0]
    assert list(g.position) == [1.0, 2.0, 3.0]
    sub.unsubscribe()


def test_denied_topics_are_unavailable(bridge):
    node, c = bridge
    errors = {}
    for topic in ['/test/secret', '/test/does_not_exist_anywhere']:
        with pytest.raises(Exception) as e:
            c.subscribe(topic, 'std_msgs/String')
        errors[topic] = str(e.value).replace(topic, '<topic>')
    # Denied and nonexistent look the same.
    assert errors['/test/secret'] == errors['/test/does_not_exist_anywhere']
    assert 'not available' in errors['/test/secret']
    # Allowed for reading is not allowed for publishing.
    with pytest.raises(Exception) as e:
        c.publish('/test/chatter', 'std_msgs/String')
    assert 'not available for publishing' in str(e.value)
    with pytest.raises(Exception):
        c.publish('/test/secret', 'std_msgs/String')
    with pytest.raises(Exception):
        c.get_subscribers(987654)


def test_late_wire_connection_gets_transient_local_value(bridge):
    import time
    node, c = bridge
    sub = c.get_subscribers(c.subscribe('/test/latched', 'std_msgs/String'))
    time.sleep(1.0)  # the latched value has arrived; nothing new is published
    # Without a connection, then with a late one.
    assert sub.subscriberwire.PeekInValue()[0].data == 'latched value'
    w = sub.subscriberwire.Connect()
    bridge_host.wait_for(lambda: w.InValueValid, timeout=5, what='latched value')
    assert w.InValue.data == 'latched value'
    w.Close()
    sub.unsubscribe()


def test_unsubscribe_and_client_disconnect_release(host):
    node_name = host[0]
    with bridge_host.client(node_name, 'owner') as (node, c):
        h = c.subscribe('/test/chatter', 'std_msgs/String')
        sub = c.get_subscribers(h)
        sub.unsubscribe()
        # The client drops its cached proxy when the release arrives.
        bridge_host.wait_for(lambda: _gone(c, h), what='unsubscribed handle released')
        kept = c.subscribe('/test/chatter', 'std_msgs/String')
        c.get_subscribers(kept)
    # The owner left: its handle is gone for everyone.
    with bridge_host.client(node_name, 'other') as (node, c):
        bridge_host.wait_for(lambda: _gone(c, kept), what='handle released')


def _gone(c, h):
    try:
        c.get_subscribers(h)
        return False
    except Exception:
        return True


def _subscribe_lines(output, topic):
    return [line for line in list(output) if 'Subscribed ' + topic + ' (' in line]


def _receive_one(c, topic, msgtype):
    sub = c.get_subscribers(c.subscribe(topic, msgtype))
    ep = sub.subscriberpipe.Connect(-1)
    packets = bridge_host.wait_for(lambda: drain(ep), what=topic + ' pipe packets')
    ep.Close()
    sub.unsubscribe()
    return packets


def test_fast_path_with_intraprocess_tap():
    """
    B12: with fast_path on, rmw_intraprocess_cpp's tap carries volatile topics.

    Not with other RMWs. A transient-local topic never uses it (a tap does
    not see the latched value). Messages arrive either way.
    """
    intraprocess = os.environ.get('RMW_IMPLEMENTATION') == 'rmw_intraprocess_cpp'
    with bridge_host.running_host(
            EXE, bridge_host.TOPICS_CONFIG + 'fast_path: true\n', 'fast') as (name, output):
        with bridge_host.client(name, 'fast') as (node, c):
            packets = _receive_one(c, '/test/chatter', 'std_msgs/String')
            assert packets[0].data.startswith('hello ')
            sub = c.get_subscribers(c.subscribe('/test/latched', 'std_msgs/String'))
            w = sub.subscriberwire.Connect()
            bridge_host.wait_for(lambda: w.InValueValid, what='latched value')
            assert w.InValue.data == 'latched value'
            w.Close()
            sub.unsubscribe()
        chatter = bridge_host.wait_for(
            lambda: _subscribe_lines(output, '/test/chatter'), what='subscribe log')
        latched = _subscribe_lines(output, '/test/latched')
        assert latched and all('fast path' not in line for line in latched), latched
        assert ('fast path' in chatter[-1]) == intraprocess, chatter


def test_fast_path_off_by_default(host):
    """B12: by default (docs/benchmark.md) subscriptions never use the tap."""
    node_name, output = host
    with bridge_host.client(node_name, 'nofast') as (node, c):
        packets = _receive_one(c, '/test/chatter', 'std_msgs/String')
        assert packets[0].data.startswith('hello ')
    lines = bridge_host.wait_for(
        lambda: _subscribe_lines(output, '/test/chatter'), what='subscribe log')
    assert all('fast path' not in line for line in lines), lines
