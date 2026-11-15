// Package testvectors reads the shared Robot Raconteur test vector blob
// (robotraconteur/test/data/test_vectors.bin, see TEST_VECTORS_FORMAT.md).
// scripts/gen_test.sh copies the blob next to this file so it can be embedded.
package testvectors

import (
	_ "embed"
	"encoding/binary"
	"fmt"
	"math"
	"unsafe"
)

//go:embed test_vectors.bin
var blob []byte

type entry struct {
	typ    uint32
	count  uint32
	offset uint32
}

var index map[string]entry

func init() {
	if err := parse(); err != nil {
		panic("testvectors: " + err.Error())
	}
}

func parse() error {
	if len(blob) < 24 || string(blob[:8]) != "RRTVECTS" {
		return fmt.Errorf("invalid magic")
	}
	le := binary.LittleEndian
	if le.Uint32(blob[8:]) != 1 || le.Uint32(blob[12:]) != 64 {
		return fmt.Errorf("unsupported version or entry size")
	}
	n := uint64(le.Uint32(blob[16:]))
	if uint64(le.Uint32(blob[20:])) != uint64(len(blob)) || 24+64*n > uint64(len(blob)) {
		return fmt.Errorf("invalid file size")
	}
	index = make(map[string]entry, n)
	for i := uint64(0); i < n; i++ {
		e := blob[24+64*i : 24+64*(i+1)]
		name := e[:48]
		for j, c := range name {
			if c == 0 {
				name = name[:j]
				break
			}
		}
		index[string(name)] = entry{le.Uint32(e[48:]), le.Uint32(e[52:]), le.Uint32(e[56:])}
	}
	return nil
}

type number interface {
	~float64 | ~float32 | ~int8 | ~uint8 | ~int16 | ~uint16 | ~int32 | ~uint32 | ~int64 | ~uint64
}

func typeCode[T number]() uint32 {
	var z T
	switch any(z).(type) {
	case float64:
		return 1
	case float32:
		return 2
	case int8:
		return 3
	case uint8:
		return 4
	case int16:
		return 5
	case uint16:
		return 6
	case int32:
		return 7
	case uint32:
		return 8
	case int64:
		return 9
	case uint64:
		return 10
	}
	panic("testvectors: unsupported type")
}

// Get returns a copy of the named vector, which must have element type T.
func Get[T number](name string) ([]T, error) {
	e, ok := index[name]
	if !ok {
		return nil, fmt.Errorf("test vector %q not found", name)
	}
	if e.typ != typeCode[T]() {
		return nil, fmt.Errorf("test vector %q has type code %d, requested %d", name, e.typ, typeCode[T]())
	}
	var z T
	size := uint64(unsafe.Sizeof(z))
	end := uint64(e.offset) + uint64(e.count)*size
	if end > uint64(len(blob)) || end > math.MaxUint32 {
		return nil, fmt.Errorf("test vector %q out of bounds", name)
	}
	o := make([]T, e.count)
	raw := blob[e.offset:end]
	// little-endian host assumed (as by the format)
	copy(unsafe.Slice((*byte)(unsafe.Pointer(unsafe.SliceData(o))), len(raw)), raw)
	return o, nil
}

// MustGet is Get that panics on error (for tests).
func MustGet[T number](name string) []T {
	v, err := Get[T](name)
	if err != nil {
		panic(err)
	}
	return v
}
