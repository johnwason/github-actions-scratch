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
Test-only Robot Raconteur node setup that cannot reach the network.

The Python twin of rr_test_safety.hpp (CLAUDE.md S2). Every Python test that
starts a Robot Raconteur node uses make_node_setup(); never import
RobotRaconteur.Client (it creates a default ClientNodeSetup with TCP and
discovery). test_safety_helper_usage.py enforces both.
"""

import RobotRaconteur as RR


def forbidden_flags():
    return (RR.RobotRaconteurNodeSetupFlags_ENABLE_NODE_DISCOVERY_LISTENING
            | RR.RobotRaconteurNodeSetupFlags_ENABLE_NODE_ANNOUNCE
            | RR.RobotRaconteurNodeSetupFlags_ENABLE_HARDWARE_TRANSPORT
            | RR.RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER_PORT_SHARER
            | RR.RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_SERVER_PUBLIC)


def server_flags():
    return (RR.RobotRaconteurNodeSetupFlags_ENABLE_LOCAL_TRANSPORT
            | RR.RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_START_SERVER
            | RR.RobotRaconteurNodeSetupFlags_ENABLE_INTRA_TRANSPORT
            | RR.RobotRaconteurNodeSetupFlags_INTRA_TRANSPORT_START_SERVER)


def client_flags():
    return (RR.RobotRaconteurNodeSetupFlags_ENABLE_LOCAL_TRANSPORT
            | RR.RobotRaconteurNodeSetupFlags_LOCAL_TRANSPORT_START_CLIENT
            | RR.RobotRaconteurNodeSetupFlags_ENABLE_INTRA_TRANSPORT)


class UnsafeSetupError(Exception):
    pass


def check_flags(flags, allow_loopback_tcp=False):
    if flags & forbidden_flags():
        raise UnsafeSetupError(
            'Robot Raconteur test node setup requests announce, discovery, hardware '
            'transport, port sharer or a public local server')
    tcp = (RR.RobotRaconteurNodeSetupFlags_ENABLE_TCP_TRANSPORT
           | RR.RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER)
    if flags & tcp:
        if not allow_loopback_tcp:
            raise UnsafeSetupError('TCP transport in a test without allow_loopback_tcp')
        if (flags & RR.RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_START_SERVER
                and not flags & RR.RobotRaconteurNodeSetupFlags_TCP_TRANSPORT_LISTEN_LOCALHOST):
            raise UnsafeSetupError('a test TCP server must listen on localhost only')


def make_node_setup(node_name, flags, allow_loopback_tcp=False, node=None):
    """
    Create the only kind of node setup tests may use.

    No command-line or environment overrides (allowed_overrides=0, empty
    argv); verifies afterwards that no TCP transport exists unless allowed.
    Use as a context manager; it shuts the node down on exit.
    """
    check_flags(flags, allow_loopback_tcp)
    setup = RR.RobotRaconteurNodeSetup(
        node_name=node_name, tcp_port=0, flags=flags, allowed_overrides=0,
        node=node, argv=[])
    if not allow_loopback_tcp and setup.tcp_transport is not None:
        setup.close()
        raise UnsafeSetupError('a TCP transport exists although none was requested')
    return setup
