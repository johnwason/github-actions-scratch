# Pinned dependency versions. Sourced by the scripts; also hashed for the CI cache key.
# Keep in sync with the "Pinned dependencies" table in CLAUDE.md.

LUA54_VERSION=5.4.9
LUA54_SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
LUA54_URL="https://www.lua.org/ftp/lua-${LUA54_VERSION}.tar.gz"

LUAJIT_REPO=https://github.com/LuaJIT/LuaJIT
LUAJIT_COMMIT=63c8f1a9b479ba5518cf6a170ff7729178bc3cfd   # v2.1 branch

COMPAT53_REPO=https://github.com/lunarmodules/lua-compat-5.3
COMPAT53_COMMIT=2d3c70b95bf076816d974664d62070abe7c6e7fc

RR_REPO=https://github.com/johnwason/robotraconteur
RR_COMMIT=25209812da9867530981afe26fd5547ac40c48c1   # EXPERIMENT: test-service-p1-async-send on master f86d0b5

PY_ROBOTRACONTEUR_VERSION=1.2.9
PY_NUMPY_VERSION=2.5.4   # required by the robotraconteur wheel; pinned for reproducibility
# Example test peers (loopback robot and camera services, from the Racket client's tests):
PY_RR_COMPANION_VERSION=0.4.0
PY_RR_ABSTRACT_ROBOT_VERSION=0.2.4

# Reference implementations: name repo commit
REFS="
robotraconteur_typescript https://github.com/johnwason/robotraconteur_typescript 3ce12fe421cd55a3b254d2a4ca8ba7416cdc21c5
robotraconteur_typescript_node https://github.com/johnwason/robotraconteur_typescript_node 666494b26a0fb7749f150a2b8159ef1cad0cbf24
robotraconteur_racket https://github.com/johnwason/robotraconteur_racket 7e879775464d3fe91ae37ead0d007dd56ceefc9b
"
