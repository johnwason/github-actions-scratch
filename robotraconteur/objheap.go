package robotraconteur

import (
	"sync"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// The object heap keeps Go director objects reachable while C++ holds a
// reference to them (the Go equivalent of the C# RRObjectHeap). C++ passes
// the heap id back through RRNativeObjectHeapSupport::DeleteObject when it
// releases a director; the entry is removed and its native director deleted.

type heapEntry struct {
	obj     any
	release func()
}

var objHeap = struct {
	mu      sync.Mutex
	next    int32
	entries map[int32]heapEntry
}{entries: map[int32]heapEntry{}}

// heapAdd stores obj and returns its id. release (may be nil) is called when
// C++ releases the object; it normally deletes the SWIG director.
func heapAdd(obj any, release func()) int32 {
	objHeap.mu.Lock()
	defer objHeap.mu.Unlock()
	for {
		objHeap.next++
		if objHeap.next <= 0 {
			objHeap.next = 1
		}
		if _, used := objHeap.entries[objHeap.next]; !used {
			break
		}
	}
	objHeap.entries[objHeap.next] = heapEntry{obj, release}
	return objHeap.next
}

func heapGet(id int32) (any, bool) {
	objHeap.mu.Lock()
	defer objHeap.mu.Unlock()
	e, ok := objHeap.entries[id]
	return e.obj, ok
}

func heapRemove(id int32) {
	objHeap.mu.Lock()
	e, ok := objHeap.entries[id]
	delete(objHeap.entries, id)
	objHeap.mu.Unlock()
	if ok && e.release != nil {
		e.release()
	}
}

func heapCount() int {
	objHeap.mu.Lock()
	defer objHeap.mu.Unlock()
	return len(objHeap.entries)
}

type heapSupport struct{}

func (heapSupport) DeleteObject_i(id int32) { heapRemove(id) }

var heapSupportDirector swig.RRNativeObjectHeapSupport

func init() {
	heapSupportDirector = swig.NewDirectorRRNativeObjectHeapSupport(heapSupport{})
	swig.RRNativeObjectHeapSupportSet_Support(heapSupportDirector)
}
