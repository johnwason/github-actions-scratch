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
B11 acceptance: with authentication on, a client without valid credentials is rejected.

The password file and its credentials are made here, at test time, and
used only by this test (CLAUDE.md S5).
"""

import hashlib
import os
import secrets
import sys
import tempfile

import pytest
import RobotRaconteur as RR

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bridge_host  # noqa: E402, I100
import rr_test_safety  # noqa: E402, I100

EXE = os.environ['TOPIC_TEST_HOST_EXE']
USER = 'tester'
PASSWORD = secrets.token_hex(12)


@pytest.fixture(scope='module')
def host():
    d = tempfile.mkdtemp()
    users = os.path.join(d, 'users.txt')
    with open(users, 'w') as f:
        f.write('%s %s objectlock\n' % (USER, hashlib.md5(PASSWORD.encode()).hexdigest()))
    extra = ('  authentication:\n    enabled: true\n    password_file: %s\n' % users)
    with bridge_host.running_host(EXE, bridge_host.TOPICS_CONFIG, 'auth',
                                  robotraconteur_extra=extra) as h:
        yield h[0]
    os.unlink(users)


def connect(node_name, *credentials):
    node = RR.RobotRaconteurNode()
    node.Init()
    setup = rr_test_safety.make_node_setup(
        'auth_client_%d' % os.getpid(), rr_test_safety.client_flags(), node=node)
    try:
        return node, setup, node.ConnectService(bridge_host.url(node_name), *credentials)
    except Exception:
        setup.close()
        raise


def test_valid_user_connects(host):
    node, setup, c = bridge_host.wait_for(
        lambda: _try(lambda: connect(host, USER, {'password': RR.VarValue(PASSWORD, 'string')})),
        timeout=30, what='an authenticated connection')
    with setup:
        assert c.bridge_version == '0.0.0'
        node.DisconnectService(c)


def _try(f):
    try:
        return f()
    except Exception as e:
        if 'Authentication' in type(e).__name__ or 'authentic' in str(e).lower():
            raise
        return None


@pytest.mark.parametrize('credentials', [
    (),                                                      # none
    ('nobody', {'password': RR.VarValue(PASSWORD, 'string')}),
    (USER, {'password': RR.VarValue('wrong', 'string')}),
])
def test_invalid_credentials_rejected(host, credentials):
    # The valid case above has connected, so the host is up.
    with pytest.raises(Exception) as e:
        node, setup, c = connect(host, *credentials)
        setup.close()
    text = (type(e.value).__name__ + ' ' + str(e.value)).lower()
    assert 'authenticat' in text or 'permission' in text or 'access' in text, text
