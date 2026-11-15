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
B0 acceptance: a Python Robot Raconteur client reaches a C++ service.

The service (rr_smoke_service, C++) runs as a separate process; the client
connects over LocalTransport (a Unix socket in the container; no network) and
uses dynamic types (no generated code on the client side).
"""

import os
import subprocess
import sys

import RobotRaconteur as RR

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rr_test_safety  # noqa: E402

SERVICE_EXE = os.environ['RR_SMOKE_SERVICE_EXE']


def test_python_client_reaches_cpp_service_over_local_transport():
    node_name = 'rr_bridge_smoke_%d' % os.getpid()
    service = subprocess.Popen(
        [SERVICE_EXE, node_name], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True)
    try:
        ready = service.stdout.readline().strip()
        assert ready.startswith('ready'), ready
        print('service:', ready)

        node = RR.RobotRaconteurNode()
        node.Init()
        with rr_test_safety.make_node_setup(
                'rr_bridge_smoke_client_%d' % os.getpid(),
                rr_test_safety.client_flags(), node=node) as setup:
            assert setup.tcp_transport is None
            url = 'rr+local:///?nodename=%s&service=smoke' % node_name
            obj = node.ConnectService(url)
            try:
                assert obj.greeting == 'hello from C++'
                assert obj.add(40, 2) == 42
                assert obj.add(-7, 7) == 0
                print('Robot Raconteur', node.RobotRaconteurVersion)
            finally:
                node.DisconnectService(obj)
    finally:
        service.stdin.close()  # the service exits when stdin closes
        assert service.wait(timeout=30) == 0


def test_helper_rejects_unsafe_flags():
    for flags in [
        RR.RobotRaconteurNodeSetupFlags_CLIENT_DEFAULT,
        RR.RobotRaconteurNodeSetupFlags_SERVER_DEFAULT,
        rr_test_safety.client_flags() | RR.RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE,
        rr_test_safety.client_flags() | RR.RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT,
    ]:
        try:
            rr_test_safety.check_flags(flags)
        except rr_test_safety.UnsafeSetupError:
            continue
        raise AssertionError('flags 0x%x accepted' % flags)
    tcp_server = (RR.RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT
                  | RR.RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER)
    try:
        rr_test_safety.check_flags(tcp_server, allow_loopback_tcp=True)
        raise AssertionError('non-localhost TCP server accepted')
    except rr_test_safety.UnsafeSetupError:
        pass
    rr_test_safety.check_flags(
        tcp_server | RR.RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_LISTEN_LOCALHOST,
        allow_loopback_tcp=True)
