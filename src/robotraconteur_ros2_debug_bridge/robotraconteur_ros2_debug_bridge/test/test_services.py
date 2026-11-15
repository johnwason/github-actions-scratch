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
B6 acceptance: services from a Python Robot Raconteur client.

add-two-ints through a rosclient; wait_for_service and unavailable
services; denial; a stress test of slow calls during which other requests
keep being answered (no Robot Raconteur thread waits for ROS); a client
serving a ROS service through its servicefunction callback. Registered once
per RMW.
"""

import os
import sys
import threading
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']
ADD = 'rosservice_example_interfaces__AddTwoInts'


@pytest.fixture(scope='module')
def host():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'services') as h:
        yield h


@pytest.fixture
def bridge(host):
    with bridge_host.client(host[0], 'services') as c:
        yield c


def request(node, obj, a, b):
    r = node.GetStructureType(ADD + '.AddTwoIntsRequest', obj)()
    r.a = a
    r.b = b
    return r


def test_add_two_ints(bridge):
    node, c = bridge
    cl = c.get_clients(c.client('/test/add_two_ints', 'example_interfaces/AddTwoInts'))
    assert cl.wait_for_service(10)
    res = cl.call(request(node, cl, 2, 40))
    assert res.sum == 42
    # The type from the graph, and the long form.
    cl2 = c.get_clients(c.client('/test/add_two_ints', ''))
    assert cl2.call(request(node, cl2, -5, 3)).sum == -2
    cl3 = c.get_clients(c.client('/test/add_two_ints', 'example_interfaces/srv/AddTwoInts'))
    assert cl3.call(request(node, cl3, 1, 1)).sum == 2


def test_unavailable_and_denied(bridge):
    node, c = bridge
    missing = c.get_clients(c.client('/test/missing', 'example_interfaces/AddTwoInts'))
    t0 = time.monotonic()
    assert missing.wait_for_service(0.5) is False
    assert 0.4 < time.monotonic() - t0 < 5
    with pytest.raises(Exception) as e:
        missing.call(request(node, missing, 1, 2))
    assert 'not available' in str(e.value)
    for denied in ['/test/denied_svc', '/test/not_listed', '/test_talker/get_parameters']:
        with pytest.raises(Exception) as e:
            c.client(denied, 'example_interfaces/AddTwoInts')
        assert 'is not available' in str(e.value), denied
    with pytest.raises(Exception) as e:
        c.register_service('/test/add_two_ints', 'example_interfaces/AddTwoInts')
    assert 'not available for serving' in str(e.value)
    with pytest.raises(Exception):
        c.get_clients(424242)


def test_no_robotraconteur_thread_waits(bridge):
    """
    Long calls in flight at once; the bridge keeps answering other requests.

    40 wait_for_service(2.0) on a missing service (more than Robot
    Raconteur's thread pool) and 8 slow calls (within the ROS service's
    request queue depth of 10: ROS drops requests beyond it).
    """
    node, c = bridge
    slow = c.get_clients(c.client('/test/slow_add', 'example_interfaces/AddTwoInts'))
    assert slow.wait_for_service(10)
    missing = c.get_clients(c.client('/test/missing', 'example_interfaces/AddTwoInts'))
    results = []
    waits = []
    errors = []

    def caller(i):
        try:
            results.append((i, slow.call(request(node, slow, i, 1000)).sum))
        except Exception as e:  # pragma: no cover - reported below
            errors.append(repr(e))

    def waiter():
        try:
            t0 = time.monotonic()
            waits.append((missing.wait_for_service(2.0), time.monotonic() - t0))
        except Exception as e:  # pragma: no cover - reported below
            errors.append(repr(e))

    threads = [threading.Thread(target=waiter) for _ in range(40)]
    threads += [threading.Thread(target=caller, args=(i,)) for i in range(8)]
    start = time.monotonic()
    for t in threads:
        t.start()
    worst = 0.0
    while any(t.is_alive() for t in threads):
        t0 = time.monotonic()
        assert c.bridge_version == '0.0.0'
        worst = max(worst, time.monotonic() - t0)
        time.sleep(0.02)
    for t in threads:
        t.join()
    assert not errors, errors
    assert sorted(results) == [(i, i + 1000) for i in range(8)]
    assert len(waits) == 40 and all(ok is False and 1.9 < dt < 6 for ok, dt in waits), waits
    # All 40 waits ran at the same time, not one pool thread each in turn.
    assert time.monotonic() - start < 8
    assert worst < 1.0, worst


def test_client_serves_a_ros_service(bridge):
    node, c = bridge
    calls = []
    srv = c.get_services(c.register_service('/test/served', 'example_interfaces/AddTwoInts'))
    response_type = node.GetStructureType(ADD + '.AddTwoIntsResponse', srv)

    def servicefunction(req):
        calls.append((req.a, req.b))
        r = response_type()
        r.sum = req.a * 10 + req.b
        return r

    srv.servicefunction.Function = servicefunction
    result = c.get_subscribers(c.subscribe('/test/served_result', 'std_msgs/String'))
    ep = result.subscriberpipe.Connect(-1)
    go = c.get_publishers(c.publish('/test/call_served', 'std_msgs/String'))
    go_msg = node.GetStructureType('rosmsg_std_msgs__String.String', go)()
    go_msg.data = 'go'

    def answered():
        go.publish(go_msg)
        time.sleep(0.2)
        got = []
        while ep.Available > 0:
            got.append(ep.ReceivePacket().data)
        return 'sum 23' in got

    bridge_host.wait_for(answered, timeout=20, what='the ROS client to get the served response')
    assert calls and calls[0] == (2, 3)
