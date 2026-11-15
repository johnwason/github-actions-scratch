package robotraconteur

import (
	"encoding/hex"
	"fmt"
	"strings"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// NodeID is a Robot Raconteur node id (a UUID).
type NodeID [16]byte

// String formats the id as "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".
func (id NodeID) String() string {
	h := hex.EncodeToString(id[:])
	return fmt.Sprintf("{%s-%s-%s-%s-%s}", h[0:8], h[8:12], h[12:16], h[16:20], h[20:32])
}

// IsAny reports whether id is the "any node" id (all zero).
func (id NodeID) IsAny() bool { return id == NodeID{} }

// ParseNodeID parses a node id in "{...}" or plain UUID form.
func ParseNodeID(s string) (NodeID, error) {
	var id NodeID
	t := strings.ReplaceAll(strings.Trim(strings.TrimSpace(s), "{}"), "-", "")
	if len(t) != 32 {
		return id, NewInvalidArgument("invalid node id " + s)
	}
	if _, err := hex.Decode(id[:], []byte(t)); err != nil {
		return id, NewInvalidArgument("invalid node id " + s)
	}
	return id, nil
}

// NewNodeID returns a new random unique node id.
func NewNodeID() NodeID {
	n := swig.NodeIDNewUniqueID()
	defer swig.DeleteNodeID(n)
	return nodeIDFromNative(n)
}

func nodeIDFromNative(n swig.NodeID) NodeID {
	id, _ := ParseNodeID(n.ToString())
	return id
}

func (id NodeID) toNative() swig.NodeID {
	return swig.NewNodeID(id.String())
}
