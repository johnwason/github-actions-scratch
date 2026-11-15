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
B8 acceptance: the Fibonacci action from a Python client.

A goal through the send_goal generator (accepted, feedback, result),
cancelling mid-goal with Abort, a rejected goal, the type from the graph,
and denial. Registered once per RMW.
"""

import os
import sys
import time

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']
FIB = 'rosaction_action_tutorials_interfaces__Fibonacci'
ACCEPTED, EXECUTING, SUCCEEDED = 1, 2, 4


@pytest.fixture(scope='module')
def host():
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'actions') as h:
        yield h


@pytest.fixture
def bridge(host):
    with bridge_host.client(host[0], 'actions') as c:
        yield c


def fibonacci(node, c, actiontype='action_tutorials_interfaces/Fibonacci'):
    ac = c.get_action_clients(c.action_client('/fibonacci', actiontype))
    assert ac.wait_for_server(10)
    return ac, node.GetStructureType(FIB + '.FibonacciGoal', ac)


def goal(goal_type, order):
    g = goal_type()
    g.order = order
    return g


def drain(gen):
    """Every status the generator gives, until it ends."""
    out = []
    while True:
        try:
            out.append(gen.Next())
        except Exception as e:
            if 'StopIteration' in type(e).__name__:
                return out
            raise


def test_goal_feedback_result(bridge):
    node, c = bridge
    ac, goal_type = fibonacci(node, c)
    statuses = drain(ac.send_goal(goal(goal_type, 10)))
    assert statuses[0].status == ACCEPTED
    assert statuses[0].feedback is None and statuses[0].result is None
    feedback = [s for s in statuses if s.feedback is not None]
    assert feedback and all(s.status == EXECUTING for s in feedback)
    assert len(feedback[-1].feedback.partial_sequence) > len(feedback[0].feedback.partial_sequence)
    final = statuses[-1]
    assert final.status == SUCCEEDED
    assert list(final.result.sequence) == [0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55]
    # One goal throughout.
    assert len({bytes(s.goal_id) for s in statuses}) == 1


def test_cancel_mid_goal(bridge):
    node, c = bridge
    events = c.get_subscribers(c.subscribe('/test/fibonacci_events', 'std_msgs/String'))
    ep = events.subscriberpipe.Connect(-1)
    ac, goal_type = fibonacci(node, c)
    gen = ac.send_goal(goal(goal_type, 40))  # about 2 s
    seen = 0
    while seen < 3:
        s = gen.Next()
        if s.feedback is not None:
            seen += 1
    gen.Abort()

    def outcome():
        got = []
        while ep.Available > 0:
            got.append(ep.ReceivePacket().data)
        return [g for g in got if g.endswith(' 40')]

    got = bridge_host.wait_for(outcome, timeout=10, what='the server to cancel the goal')
    assert got == ['canceled 40']


def test_rejected_and_type_from_graph(bridge):
    node, c = bridge
    ac, goal_type = fibonacci(node, c, actiontype='')
    gen = ac.send_goal(goal(goal_type, -1))
    with pytest.raises(Exception) as e:
        gen.Next()
    assert 'goal rejected' in str(e.value)
    # Another goal on the same client still works.
    assert drain(ac.send_goal(goal(goal_type, 3)))[-1].status == SUCCEEDED


def test_denied(bridge):
    node, c = bridge
    for denied in ['/fib', '/nonexistent_action']:
        with pytest.raises(Exception) as e:
            c.action_client(denied, 'action_tutorials_interfaces/Fibonacci')
        assert 'is not available' in str(e.value), denied
    with pytest.raises(Exception):
        c.get_action_clients(987654)
    time.sleep(0)  # keep the module's host alive until here
