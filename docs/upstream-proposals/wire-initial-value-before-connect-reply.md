# Wire clients drop a broadcaster's initial value

Found 2026-11-20 while building Lua services (M4) at the pinned RR core commit.

## What happens

`WireServerBase::WireCommand` handles `WireConnectReq` by creating the server-side connection and
calling `fire_WireConnectCallback(con)` before it returns the connect reply. A `WireBroadcaster`
connect callback (`WireBroadcasterBase::ConnectionConnectedBase`) immediately sends the current out
value with `ep->SetOutValueBase(out_value)`. That `WirePacket` goes out before the
`WireConnectRet`.

On the client, `WireClientBase::AsyncConnect_internal1` creates `connection` only when the connect
reply arrives, and `WireClientBase::WirePacketReceived` drops a packet when `connection` is null.
So the initial value is lost, and a client of a broadcaster whose value is set once (a
configuration, a slowly changing state) sees no value until the next `SetOutValue`.

Reproduced with a Lua service and client (`tests/server/test_server.lua`, "wire broadcaster"):
`peekWireInValue` returns the value, `connectWire(...):inValueValid()` stays false, and a later
`setOutValue` arrives.

## Proposed fix

Either send the broadcaster's initial value after the connect reply (post the send from the
connect callback, or have `WireServerBase` fire the connect callback after queueing the reply), or
have the client create its connection before sending `WireConnectReq` and discard it if the
connect fails.

## Workaround in this binding

The Lua client peeks the in value once after connecting and feeds it to the connection with
`WireConnectionBase::WirePacketReceived` (a newer packet wins, because the connection keeps only
newer timestamps). Other clients of a Lua broadcaster still miss the initial value until the next
update.
