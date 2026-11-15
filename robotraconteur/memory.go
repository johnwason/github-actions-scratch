package robotraconteur

import (
	"sync"
	"unsafe"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Memory members. The same interfaces are implemented by client references
// (returned by generated stubs) and by service objects (usually with the
// slice backed implementations returned by NewArrayMemory and friends).

// ArrayMemory is a numeric array memory (T[] memory).
type ArrayMemory[T Primitive] interface {
	Length() (uint64, error)
	// Read reads count elements at memorypos into buffer[bufferpos:].
	Read(memorypos uint64, buffer []T, bufferpos, count uint64) error
	// Write writes buffer[bufferpos:bufferpos+count] at memorypos.
	Write(memorypos uint64, buffer []T, bufferpos, count uint64) error
}

// MultiDimArrayMemory is a numeric multi-dimensional array memory (T[*] memory).
type MultiDimArrayMemory[T Primitive] interface {
	Dimensions() ([]uint64, error)
	DimCount() (uint64, error)
	Read(memorypos []uint64, buffer *MultiDimArray[T], bufferpos, count []uint64) error
	Write(memorypos []uint64, buffer *MultiDimArray[T], bufferpos, count []uint64) error
}

// PodArrayMemory is a pod array memory.
type PodArrayMemory[T Pod] interface {
	Length() (uint64, error)
	Read(memorypos uint64, buffer []T, bufferpos, count uint64) error
	Write(memorypos uint64, buffer []T, bufferpos, count uint64) error
}

// PodMultiDimArrayMemory is a pod multi-dimensional array memory.
type PodMultiDimArrayMemory[T Pod] interface {
	Dimensions() ([]uint64, error)
	DimCount() (uint64, error)
	Read(memorypos []uint64, buffer *PodMultiDimArray[T], bufferpos, count []uint64) error
	Write(memorypos []uint64, buffer *PodMultiDimArray[T], bufferpos, count []uint64) error
}

// NamedArrayMemory is a namedarray array memory.
type NamedArrayMemory[T any] interface {
	Length() (uint64, error)
	Read(memorypos uint64, buffer []T, bufferpos, count uint64) error
	Write(memorypos uint64, buffer []T, bufferpos, count uint64) error
}

// NamedMultiDimArrayMemory is a namedarray multi-dimensional array memory.
type NamedMultiDimArrayMemory[T any] interface {
	Dimensions() ([]uint64, error)
	DimCount() (uint64, error)
	Read(memorypos []uint64, buffer *NamedMultiDimArray[T], bufferpos, count []uint64) error
	Write(memorypos []uint64, buffer *NamedMultiDimArray[T], bufferpos, count []uint64) error
}

// ---------------------------------------------------------------- helpers

func checkRange(n, pos, count uint64) error {
	if pos > n || count > n-pos {
		return NewOutOfRange("memory access out of range")
	}
	return nil
}

func u64sToNative(v []uint64) swig.Vector_uint64_t {
	o := swig.NewVector_uint64_t__SWIG_0()
	for _, x := range v {
		o.Add(x)
	}
	return o
}

func u64sFromNative(v swig.Vector_uint64_t) []uint64 {
	n := int(v.Size())
	o := make([]uint64, n)
	for i := range o {
		o[i] = v.Get(i)
	}
	return o
}

func dims64(d []uint32) []uint64 {
	o := make([]uint64, len(d))
	for i, x := range d {
		o[i] = uint64(x)
	}
	return o
}

func dims32(d []uint64) []uint32 {
	o := make([]uint32, len(d))
	for i, x := range d {
		o[i] = uint32(x)
	}
	return o
}

// mdCopy copies a count shaped block between two column-major arrays.
func mdCopy[T any](src []T, srcDims, srcPos []uint64, dst []T, dstDims, dstPos, count []uint64) error {
	nd := len(count)
	if len(srcDims) != nd || len(srcPos) != nd || len(dstDims) != nd || len(dstPos) != nd {
		return NewInvalidArgument("dimension count mismatch")
	}
	total := uint64(1)
	for i := 0; i < nd; i++ {
		if srcPos[i]+count[i] > srcDims[i] || dstPos[i]+count[i] > dstDims[i] {
			return NewOutOfRange("multidimarray block out of range")
		}
		total *= count[i]
	}
	if total == 0 {
		return nil
	}
	idx := make([]uint64, nd)
	for {
		var so, do uint64
		sm, dm := uint64(1), uint64(1)
		for i := 0; i < nd; i++ {
			so += (srcPos[i] + idx[i]) * sm
			do += (dstPos[i] + idx[i]) * dm
			sm *= srcDims[i]
			dm *= dstDims[i]
		}
		// copy along the first (fastest) dimension
		copy(dst[do:do+count[0]], src[so:so+count[0]])
		k := 1
		for ; k < nd; k++ {
			idx[k]++
			if idx[k] < count[k] {
				break
			}
			idx[k] = 0
		}
		if k >= nd {
			return nil
		}
	}
}

// rrArrayFromSlice creates a native RRBaseArray from a primitive slice.
func rrArrayFromSlice[T Primitive](v []T) (swig.RRBaseArray, error) {
	data := toBaseSlice(v)
	n, src, size := sliceBytes(data)
	arr := swig.AllocateRRArrayByType(swig.RobotRaconteurDataTypes(DataTypeOf[T]()), int64(n))
	if arr == nil || arr.Swigcptr() == 0 {
		return nil, NewInternalError("could not allocate array")
	}
	if n > 0 {
		dst := unsafe.Slice((*byte)(unsafe.Pointer(arr.Void_ptr())), n*size)
		if b, ok := data.([]bool); ok {
			for i, x := range b {
				dst[i] = 0
				if x {
					dst[i] = 1
				}
			}
		} else {
			copy(dst, unsafe.Slice((*byte)(src), n*size))
		}
	}
	return arr, nil
}

// sliceFromRRArray copies a native RRBaseArray into a new []T.
func sliceFromRRArray[T Primitive](arr swig.RRBaseArray) ([]T, error) {
	if arr == nil || arr.Swigcptr() == 0 {
		return nil, NewNullValue("null array")
	}
	dt := DataType(arr.GetTypeID())
	if dt != DataTypeOf[T]() {
		return nil, NewDataTypeMismatch("memory array type mismatch")
	}
	n := int(arr.Size())
	var p unsafe.Pointer
	if n > 0 {
		p = unsafe.Pointer(arr.Void_ptr())
	}
	d, err := copyNativeArray(dt, p, n)
	if err != nil {
		return nil, err
	}
	a, _ := fromBaseSlice[T](d)
	return a, nil
}

// nestedFromNative converts a native nested element list.
func nestedFromNative(nl swig.MessageElementNestedElementList) (*NestedList, error) {
	if nl == nil || nl.Swigcptr() == 0 {
		return nil, NewNullValue("null nested list")
	}
	med := nl.SwigGetMessageElementData()
	defer swig.DeleteMessageElementData(med)
	d, err := nativeData(med)
	if err != nil {
		return nil, err
	}
	l, ok := d.(*NestedList)
	if !ok {
		return nil, NewDataTypeError("expected nested list")
	}
	return l, nil
}

// nestedToNative converts a nested list to a new native nested list.
func nestedToNative(l *NestedList) (swig.MessageElementNestedElementList, error) {
	m := swig.NewMessageElement()
	defer swig.DeleteMessageElement(m)
	if err := setNativeData(m, l); err != nil {
		return nil, err
	}
	d := m.GetData()
	defer swig.DeleteMessageElementData(d)
	return swig.MessageElementDataUtilToMessageElementNestedElementList(d), nil
}

// ---------------------------------------------------------------- numeric array memory client

type arrayMemoryClient[T Primitive] struct {
	native swig.ArrayMemoryBase
}

// NewArrayMemoryClient creates the client side of a numeric array memory. Used by generated stubs.
func NewArrayMemoryClient[T Primitive](b *StubBase, name string) (ArrayMemory[T], error) {
	m, err := call(func() swig.ArrayMemoryBase { return b.inner.GetArrayMemory(name) })
	if err != nil {
		return nil, err
	}
	c := &arrayMemoryClient[T]{native: m}
	addCleanup(c, m, swig.DeleteArrayMemoryBase)
	return c, nil
}

func (m *arrayMemoryClient[T]) Length() (uint64, error) {
	return call(func() uint64 { return m.native.Length() })
}

func (m *arrayMemoryClient[T]) Read(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	var cerr error
	err := callVoid(func() {
		arr := swig.WrappedArrayMemoryClientUtilRead(m.native, memorypos, count)
		if swig.Rrgo_error_pending() {
			return
		}
		defer swig.DeleteRRBaseArray(arr)
		a, err := sliceFromRRArray[T](arr)
		if err != nil {
			cerr = err
			return
		}
		copy(buffer[bufferpos:bufferpos+count], a)
	})
	if err != nil {
		return err
	}
	return cerr
}

func (m *arrayMemoryClient[T]) Write(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	var cerr error
	err := callVoid(func() {
		arr, err := rrArrayFromSlice(buffer[bufferpos : bufferpos+count])
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteRRBaseArray(arr)
		swig.WrappedArrayMemoryClientUtilWrite(m.native, memorypos, arr, 0, count)
	})
	if err != nil {
		return err
	}
	return cerr
}

// ---------------------------------------------------------------- numeric multidim memory client

type multiDimArrayMemoryClient[T Primitive] struct {
	native swig.MultiDimArrayMemoryBase
}

// NewMultiDimArrayMemoryClient creates the client side of a numeric multidim memory.
func NewMultiDimArrayMemoryClient[T Primitive](b *StubBase, name string) (MultiDimArrayMemory[T], error) {
	m, err := call(func() swig.MultiDimArrayMemoryBase { return b.inner.GetMultiDimArrayMemory(name) })
	if err != nil {
		return nil, err
	}
	c := &multiDimArrayMemoryClient[T]{native: m}
	addCleanup(c, m, swig.DeleteMultiDimArrayMemoryBase)
	return c, nil
}

func (m *multiDimArrayMemoryClient[T]) Dimensions() ([]uint64, error) {
	return call(func() []uint64 {
		v := m.native.Dimensions()
		defer swig.DeleteVector_uint64_t(v)
		return u64sFromNative(v)
	})
}

func (m *multiDimArrayMemoryClient[T]) DimCount() (uint64, error) {
	return call(func() uint64 { return m.native.DimCount() })
}

func (m *multiDimArrayMemoryClient[T]) Read(memorypos []uint64, buffer *MultiDimArray[T], bufferpos, count []uint64) error {
	if buffer == nil {
		return NewNullValue("null buffer")
	}
	var cerr error
	err := callVoid(func() {
		mp, c := u64sToNative(memorypos), u64sToNative(count)
		defer swig.DeleteVector_uint64_t(mp)
		defer swig.DeleteVector_uint64_t(c)
		res := swig.WrappedMultiDimArrayMemoryClientUtilRead(m.native, mp, c)
		if swig.Rrgo_error_pending() {
			return
		}
		defer swig.DeleteRRMultiDimArrayUntyped(res)
		arr := res.GetArray()
		defer swig.DeleteRRBaseArray(arr)
		a, err := sliceFromRRArray[T](arr)
		if err != nil {
			cerr = err
			return
		}
		cerr = mdCopy(a, count, make([]uint64, len(count)), buffer.Array, dims64(buffer.Dims), bufferpos, count)
	})
	if err != nil {
		return err
	}
	return cerr
}

func (m *multiDimArrayMemoryClient[T]) Write(memorypos []uint64, buffer *MultiDimArray[T], bufferpos, count []uint64) error {
	if buffer == nil {
		return NewNullValue("null buffer")
	}
	n := uint64(1)
	for _, c := range count {
		n *= c
	}
	block := make([]T, n)
	if err := mdCopy(buffer.Array, dims64(buffer.Dims), bufferpos, block, count, make([]uint64, len(count)), count); err != nil {
		return err
	}
	var cerr error
	err := callVoid(func() {
		buf := swig.NewRRMultiDimArrayUntyped()
		defer swig.DeleteRRMultiDimArrayUntyped(buf)
		d, err := rrArrayFromSlice(dims32(count))
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteRRBaseArray(d)
		a, err := rrArrayFromSlice(block)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteRRBaseArray(a)
		buf.SetDims(d)
		buf.SetArray(a)
		mp, c, bp := u64sToNative(memorypos), u64sToNative(count), u64sToNative(make([]uint64, len(count)))
		defer swig.DeleteVector_uint64_t(mp)
		defer swig.DeleteVector_uint64_t(c)
		defer swig.DeleteVector_uint64_t(bp)
		swig.WrappedMultiDimArrayMemoryClientUtilWrite(m.native, mp, buf, bp, c)
	})
	if err != nil {
		return err
	}
	return cerr
}

// ---------------------------------------------------------------- server side default implementations

// SliceArrayMemory is an ArrayMemory backed by a slice (service side).
type SliceArrayMemory[T Primitive] struct {
	mu   sync.Mutex
	data []T
}

// NewArrayMemory returns an ArrayMemory backed by data (shared, not copied).
func NewArrayMemory[T Primitive](data []T) *SliceArrayMemory[T] {
	return &SliceArrayMemory[T]{data: data}
}

// Data returns the backing slice.
func (m *SliceArrayMemory[T]) Data() []T { return m.data }

func (m *SliceArrayMemory[T]) Length() (uint64, error) { return uint64(len(m.data)), nil }

func (m *SliceArrayMemory[T]) Read(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if err := checkRange(uint64(len(m.data)), memorypos, count); err != nil {
		return err
	}
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	copy(buffer[bufferpos:bufferpos+count], m.data[memorypos:memorypos+count])
	return nil
}

func (m *SliceArrayMemory[T]) Write(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if err := checkRange(uint64(len(m.data)), memorypos, count); err != nil {
		return err
	}
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	copy(m.data[memorypos:memorypos+count], buffer[bufferpos:bufferpos+count])
	return nil
}

// SliceMultiDimArrayMemory is a MultiDimArrayMemory backed by a MultiDimArray.
type SliceMultiDimArrayMemory[T Primitive] struct {
	mu   sync.Mutex
	data *MultiDimArray[T]
}

// NewMultiDimArrayMemory returns a MultiDimArrayMemory backed by data.
func NewMultiDimArrayMemory[T Primitive](data *MultiDimArray[T]) *SliceMultiDimArrayMemory[T] {
	return &SliceMultiDimArrayMemory[T]{data: data}
}

// Data returns the backing array.
func (m *SliceMultiDimArrayMemory[T]) Data() *MultiDimArray[T] { return m.data }

func (m *SliceMultiDimArrayMemory[T]) Dimensions() ([]uint64, error) { return dims64(m.data.Dims), nil }
func (m *SliceMultiDimArrayMemory[T]) DimCount() (uint64, error) {
	return uint64(len(m.data.Dims)), nil
}

func (m *SliceMultiDimArrayMemory[T]) Read(memorypos []uint64, buffer *MultiDimArray[T], bufferpos, count []uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return mdCopy(m.data.Array, dims64(m.data.Dims), memorypos, buffer.Array, dims64(buffer.Dims), bufferpos, count)
}

func (m *SliceMultiDimArrayMemory[T]) Write(memorypos []uint64, buffer *MultiDimArray[T], bufferpos, count []uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return mdCopy(buffer.Array, dims64(buffer.Dims), bufferpos, m.data.Array, dims64(m.data.Dims), memorypos, count)
}

// SlicePodArrayMemory is a PodArrayMemory (or NamedArrayMemory) backed by a slice.
type SlicePodArrayMemory[T any] struct {
	mu   sync.Mutex
	data []T
}

// NewPodArrayMemory returns a pod array memory backed by data.
func NewPodArrayMemory[T Pod](data []T) *SlicePodArrayMemory[T] {
	return &SlicePodArrayMemory[T]{data: data}
}

// NewNamedArrayMemory returns a namedarray array memory backed by data.
func NewNamedArrayMemory[T any](data []T) *SlicePodArrayMemory[T] {
	return &SlicePodArrayMemory[T]{data: data}
}

// Data returns the backing slice.
func (m *SlicePodArrayMemory[T]) Data() []T { return m.data }

func (m *SlicePodArrayMemory[T]) Length() (uint64, error) { return uint64(len(m.data)), nil }

func (m *SlicePodArrayMemory[T]) Read(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if err := checkRange(uint64(len(m.data)), memorypos, count); err != nil {
		return err
	}
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	copy(buffer[bufferpos:bufferpos+count], m.data[memorypos:memorypos+count])
	return nil
}

func (m *SlicePodArrayMemory[T]) Write(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if err := checkRange(uint64(len(m.data)), memorypos, count); err != nil {
		return err
	}
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	copy(m.data[memorypos:memorypos+count], buffer[bufferpos:bufferpos+count])
	return nil
}

// SlicePodMultiDimArrayMemory is a pod multidim memory backed by a PodMultiDimArray.
type SlicePodMultiDimArrayMemory[T Pod] struct {
	mu   sync.Mutex
	data *PodMultiDimArray[T]
}

// NewPodMultiDimArrayMemory returns a pod multidim memory backed by data.
func NewPodMultiDimArrayMemory[T Pod](data *PodMultiDimArray[T]) *SlicePodMultiDimArrayMemory[T] {
	return &SlicePodMultiDimArrayMemory[T]{data: data}
}

func (m *SlicePodMultiDimArrayMemory[T]) Data() *PodMultiDimArray[T] { return m.data }
func (m *SlicePodMultiDimArrayMemory[T]) Dimensions() ([]uint64, error) {
	return dims64(m.data.Dims), nil
}
func (m *SlicePodMultiDimArrayMemory[T]) DimCount() (uint64, error) {
	return uint64(len(m.data.Dims)), nil
}

func (m *SlicePodMultiDimArrayMemory[T]) Read(memorypos []uint64, buffer *PodMultiDimArray[T], bufferpos, count []uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return mdCopy(m.data.PodArray, dims64(m.data.Dims), memorypos, buffer.PodArray, dims64(buffer.Dims), bufferpos, count)
}

func (m *SlicePodMultiDimArrayMemory[T]) Write(memorypos []uint64, buffer *PodMultiDimArray[T], bufferpos, count []uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return mdCopy(buffer.PodArray, dims64(buffer.Dims), bufferpos, m.data.PodArray, dims64(m.data.Dims), memorypos, count)
}

// SliceNamedMultiDimArrayMemory is a namedarray multidim memory backed by a NamedMultiDimArray.
type SliceNamedMultiDimArrayMemory[T any] struct {
	mu   sync.Mutex
	data *NamedMultiDimArray[T]
}

// NewNamedMultiDimArrayMemory returns a namedarray multidim memory backed by data.
func NewNamedMultiDimArrayMemory[T any](data *NamedMultiDimArray[T]) *SliceNamedMultiDimArrayMemory[T] {
	return &SliceNamedMultiDimArrayMemory[T]{data: data}
}

func (m *SliceNamedMultiDimArrayMemory[T]) Data() *NamedMultiDimArray[T] { return m.data }
func (m *SliceNamedMultiDimArrayMemory[T]) Dimensions() ([]uint64, error) {
	return dims64(m.data.Dims), nil
}
func (m *SliceNamedMultiDimArrayMemory[T]) DimCount() (uint64, error) {
	return uint64(len(m.data.Dims)), nil
}

func (m *SliceNamedMultiDimArrayMemory[T]) Read(memorypos []uint64, buffer *NamedMultiDimArray[T], bufferpos, count []uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return mdCopy(m.data.NamedArrayArray, dims64(m.data.Dims), memorypos, buffer.NamedArrayArray, dims64(buffer.Dims), bufferpos, count)
}

func (m *SliceNamedMultiDimArrayMemory[T]) Write(memorypos []uint64, buffer *NamedMultiDimArray[T], bufferpos, count []uint64) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return mdCopy(buffer.NamedArrayArray, dims64(buffer.Dims), bufferpos, m.data.NamedArrayArray, dims64(m.data.Dims), memorypos, count)
}

// ---------------------------------------------------------------- server directors

// MemoryServer is an opaque native memory adapter returned by generated
// skeletons for memory members.
type MemoryServer struct {
	array    swig.WrappedArrayMemoryDirector
	multidim swig.WrappedMultiDimArrayMemoryDirector
	pod      swig.WrappedPodArrayMemoryDirector
	podmd    swig.WrappedPodMultiDimArrayMemoryDirector
	named    swig.WrappedNamedArrayMemoryDirector
	namedmd  swig.WrappedNamedMultiDimArrayMemoryDirector
}

type arrayMemoryDirector[T Primitive] struct{ mem ArrayMemory[T] }

func (d *arrayMemoryDirector[T]) Length() uint64 {
	defer recoverDirector()
	n, err := d.mem.Length()
	if err != nil {
		setDirectorError(err)
	}
	return n
}

func (d *arrayMemoryDirector[T]) Read(memorypos uint64, buffer swig.RRBaseArray, bufferpos, count uint64) {
	defer recoverDirector()
	if buffer != nil && buffer.Swigcptr() != 0 {
		defer swig.DeleteRRBaseArray(buffer)
	}
	tmp := make([]T, count)
	if err := d.mem.Read(memorypos, tmp, 0, count); err != nil {
		setDirectorError(err)
		return
	}
	if uint64(buffer.Size()) < bufferpos+count {
		setDirectorError(NewOutOfRange("buffer too small"))
		return
	}
	if count == 0 {
		return
	}
	size := buffer.ElementSize()
	dst := unsafe.Slice((*byte)(unsafe.Pointer(buffer.Void_ptr()+uintptr(bufferpos*uint64(size)))), count*uint64(size))
	data := toBaseSlice(tmp)
	if b, ok := data.([]bool); ok {
		for i, x := range b {
			dst[i] = 0
			if x {
				dst[i] = 1
			}
		}
		return
	}
	_, src, _ := sliceBytes(data)
	copy(dst, unsafe.Slice((*byte)(src), len(dst)))
}

func (d *arrayMemoryDirector[T]) Write(memorypos uint64, buffer swig.RRBaseArray, bufferpos, count uint64) {
	defer recoverDirector()
	if buffer != nil && buffer.Swigcptr() != 0 {
		defer swig.DeleteRRBaseArray(buffer)
	}
	a, err := sliceFromRRArray[T](buffer)
	if err != nil {
		setDirectorError(err)
		return
	}
	if err := d.mem.Write(memorypos, a, bufferpos, count); err != nil {
		setDirectorError(err)
	}
}

// NewArrayMemoryServer adapts a service ArrayMemory for its skeleton.
func NewArrayMemoryServer[T Primitive](mem ArrayMemory[T]) *MemoryServer {
	if mem == nil {
		return nil
	}
	impl := &arrayMemoryDirector[T]{mem: mem}
	d := swig.NewDirectorWrappedArrayMemoryDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedArrayMemoryDirector(d) })
	d.SetObjectheapid(id)
	return &MemoryServer{array: d}
}

type multiDimArrayMemoryDirector[T Primitive] struct{ mem MultiDimArrayMemory[T] }

func (d *multiDimArrayMemoryDirector[T]) Dimensions() swig.Vector_uint64_t {
	defer recoverDirector()
	v, err := d.mem.Dimensions()
	if err != nil {
		setDirectorError(err)
		v = nil
	}
	return u64sToNative(v) // transferred to C++
}

func (d *multiDimArrayMemoryDirector[T]) DimCount() uint64 {
	defer recoverDirector()
	n, err := d.mem.DimCount()
	if err != nil {
		setDirectorError(err)
	}
	return n
}

func (d *multiDimArrayMemoryDirector[T]) Read(p swig.WrappedMultiDimArrayMemoryParams) {
	defer recoverDirector()
	memorypos := u64sFromNative(p.GetMemorypos())
	bufferpos := u64sFromNative(p.GetBufferpos())
	count := u64sFromNative(p.GetCount())
	buf := p.GetBuffer()
	defer swig.DeleteRRMultiDimArrayUntyped(buf)
	da := buf.GetDims()
	defer swig.DeleteRRBaseArray(da)
	dims, err := sliceFromRRArray[uint32](da)
	if err != nil {
		setDirectorError(err)
		return
	}
	tmp := &MultiDimArray[T]{Dims: dims32(count), Array: make([]T, dimsCount(dims32(count)))}
	if err := d.mem.Read(memorypos, tmp, make([]uint64, len(count)), count); err != nil {
		setDirectorError(err)
		return
	}
	arr := buf.GetArray()
	defer swig.DeleteRRBaseArray(arr)
	cur, err := sliceFromRRArray[T](arr)
	if err != nil {
		setDirectorError(err)
		return
	}
	if err := mdCopy(tmp.Array, count, make([]uint64, len(count)), cur, dims64(dims), bufferpos, count); err != nil {
		setDirectorError(err)
		return
	}
	n, src, size := sliceBytes(toBaseSlice(cur))
	if n > 0 {
		if _, ok := toBaseSlice(cur).([]bool); ok {
			dst := unsafe.Slice((*byte)(unsafe.Pointer(arr.Void_ptr())), n)
			for i, x := range toBaseSlice(cur).([]bool) {
				dst[i] = 0
				if x {
					dst[i] = 1
				}
			}
		} else {
			copy(unsafe.Slice((*byte)(unsafe.Pointer(arr.Void_ptr())), n*size), unsafe.Slice((*byte)(src), n*size))
		}
	}
}

func (d *multiDimArrayMemoryDirector[T]) Write(p swig.WrappedMultiDimArrayMemoryParams) {
	defer recoverDirector()
	memorypos := u64sFromNative(p.GetMemorypos())
	bufferpos := u64sFromNative(p.GetBufferpos())
	count := u64sFromNative(p.GetCount())
	buf := p.GetBuffer()
	defer swig.DeleteRRMultiDimArrayUntyped(buf)
	da := buf.GetDims()
	defer swig.DeleteRRBaseArray(da)
	dims, err := sliceFromRRArray[uint32](da)
	if err != nil {
		setDirectorError(err)
		return
	}
	arr := buf.GetArray()
	defer swig.DeleteRRBaseArray(arr)
	a, err := sliceFromRRArray[T](arr)
	if err != nil {
		setDirectorError(err)
		return
	}
	if err := d.mem.Write(memorypos, &MultiDimArray[T]{Dims: dims, Array: a}, bufferpos, count); err != nil {
		setDirectorError(err)
	}
}

// NewMultiDimArrayMemoryServer adapts a service MultiDimArrayMemory for its skeleton.
func NewMultiDimArrayMemoryServer[T Primitive](mem MultiDimArrayMemory[T]) *MemoryServer {
	if mem == nil {
		return nil
	}
	impl := &multiDimArrayMemoryDirector[T]{mem: mem}
	d := swig.NewDirectorWrappedMultiDimArrayMemoryDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedMultiDimArrayMemoryDirector(d) })
	d.SetObjectheapid(id)
	return &MemoryServer{multidim: d}
}

// ---- pod and namedarray memories (elements packed as nested lists) ----

// listCodec packs/unpacks slices of T as pod_array_t / namedarray_array_t lists.
type listCodec[T any] struct {
	pack   func(v []T) (*NestedList, error)
	unpack func(l *NestedList) ([]T, error)
}

func podListCodec[T Pod, P PodPtr[T]]() listCodec[T] {
	return listCodec[T]{pack: packPodArrayList[T], unpack: unpackPodArrayList[T, P]}
}

func namedListCodec[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]]() listCodec[T] {
	return listCodec[T]{
		pack:   func(v []T) (*NestedList, error) { return packNamedArrayList[T, E](v), nil },
		unpack: unpackNamedArrayList[T, E, P],
	}
}

type elementArrayMemoryDirector[T any] struct {
	mem interface {
		Length() (uint64, error)
		Read(uint64, []T, uint64, uint64) error
		Write(uint64, []T, uint64, uint64) error
	}
	codec listCodec[T]
}

func (d *elementArrayMemoryDirector[T]) Length() uint64 {
	defer recoverDirector()
	n, err := d.mem.Length()
	if err != nil {
		setDirectorError(err)
	}
	return n
}

func (d *elementArrayMemoryDirector[T]) Read(memorypos, bufferpos, count uint64) swig.MessageElementNestedElementList {
	defer recoverDirector()
	tmp := make([]T, count)
	if err := d.mem.Read(memorypos, tmp, 0, count); err != nil {
		setDirectorError(err)
		return nil
	}
	l, err := d.codec.pack(tmp)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	nl, err := nestedToNative(l)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return nl // transferred to C++
}

func (d *elementArrayMemoryDirector[T]) Write(memorypos uint64, buffer swig.MessageElementNestedElementList, bufferpos, count uint64) {
	defer recoverDirector()
	if buffer != nil && buffer.Swigcptr() != 0 {
		defer swig.DeleteMessageElementNestedElementList(buffer)
	}
	l, err := nestedFromNative(buffer)
	if err != nil {
		setDirectorError(err)
		return
	}
	a, err := d.codec.unpack(l)
	if err != nil {
		setDirectorError(err)
		return
	}
	if err := d.mem.Write(memorypos, a, bufferpos, count); err != nil {
		setDirectorError(err)
	}
}

// NewPodArrayMemoryServer adapts a service PodArrayMemory for its skeleton.
func NewPodArrayMemoryServer[T Pod, P PodPtr[T]](mem PodArrayMemory[T]) *MemoryServer {
	if mem == nil {
		return nil
	}
	impl := &elementArrayMemoryDirector[T]{mem: mem, codec: podListCodec[T, P]()}
	d := swig.NewDirectorWrappedPodArrayMemoryDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedPodArrayMemoryDirector(d) })
	d.SetObjectheapid(id)
	return &MemoryServer{pod: d}
}

// NewNamedArrayMemoryServer adapts a service NamedArrayMemory for its skeleton.
func NewNamedArrayMemoryServer[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](mem NamedArrayMemory[T]) *MemoryServer {
	if mem == nil {
		return nil
	}
	impl := &elementArrayMemoryDirector[T]{mem: mem, codec: namedListCodec[T, E, P]()}
	d := swig.NewDirectorWrappedNamedArrayMemoryDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedNamedArrayMemoryDirector(d) })
	d.SetObjectheapid(id)
	return &MemoryServer{named: d}
}

// multidim pod/namedarray memories: the nested list is a *_multidimarray_t
// with "dims" and "array" elements.

type mdListCodec[T any] struct {
	list     listCodec[T]
	typeName string
	mdType   DataType
}

func (c mdListCodec[T]) pack(dims []uint32, a []T) (*NestedList, error) {
	l, err := c.list.pack(a)
	if err != nil {
		return nil, err
	}
	return &NestedList{Type: c.mdType, TypeName: c.typeName, Elements: []*Element{PackArray("dims", dims), NewElement("array", l)}}, nil
}

func (c mdListCodec[T]) unpack(l *NestedList) ([]uint32, []T, error) {
	de, err := FindElement(l.Elements, "dims")
	if err != nil {
		return nil, nil, err
	}
	dims, err := UnpackArray[uint32](de)
	if err != nil {
		return nil, nil, err
	}
	ae, err := FindElement(l.Elements, "array")
	if err != nil {
		return nil, nil, err
	}
	al, ok := ae.Data.(*NestedList)
	if !ok {
		return nil, nil, NewDataTypeError("invalid multidimarray format")
	}
	a, err := c.list.unpack(al)
	return dims, a, err
}

type elementMultiDimMemoryDirector[T any] struct {
	dims     func() ([]uint64, error)
	dimCount func() (uint64, error)
	read     func(memorypos []uint64, dims []uint32, a []T, bufferpos, count []uint64) error
	write    func(memorypos []uint64, dims []uint32, a []T, bufferpos, count []uint64) error
	codec    mdListCodec[T]
}

func (d *elementMultiDimMemoryDirector[T]) Dimensions() swig.Vector_uint64_t {
	defer recoverDirector()
	v, err := d.dims()
	if err != nil {
		setDirectorError(err)
		v = nil
	}
	return u64sToNative(v)
}

func (d *elementMultiDimMemoryDirector[T]) DimCount() uint64 {
	defer recoverDirector()
	n, err := d.dimCount()
	if err != nil {
		setDirectorError(err)
	}
	return n
}

func (d *elementMultiDimMemoryDirector[T]) Read(memorypos, bufferpos, count swig.Vector_uint64_t) swig.MessageElementNestedElementList {
	defer recoverDirector()
	mp, c := u64sFromNative(memorypos), u64sFromNative(count)
	n := uint64(1)
	for _, x := range c {
		n *= x
	}
	tmp := make([]T, n)
	if err := d.read(mp, dims32(c), tmp, make([]uint64, len(c)), c); err != nil {
		setDirectorError(err)
		return nil
	}
	l, err := d.codec.pack(dims32(c), tmp)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	nl, err := nestedToNative(l)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return nl
}

func (d *elementMultiDimMemoryDirector[T]) Write(memorypos swig.Vector_uint64_t, buffer swig.MessageElementNestedElementList, bufferpos, count swig.Vector_uint64_t) {
	defer recoverDirector()
	if buffer != nil && buffer.Swigcptr() != 0 {
		defer swig.DeleteMessageElementNestedElementList(buffer)
	}
	l, err := nestedFromNative(buffer)
	if err != nil {
		setDirectorError(err)
		return
	}
	dims, a, err := d.codec.unpack(l)
	if err != nil {
		setDirectorError(err)
		return
	}
	if err := d.write(u64sFromNative(memorypos), dims, a, u64sFromNative(bufferpos), u64sFromNative(count)); err != nil {
		setDirectorError(err)
	}
}

// NewPodMultiDimArrayMemoryServer adapts a service PodMultiDimArrayMemory for its skeleton.
func NewPodMultiDimArrayMemoryServer[T Pod, P PodPtr[T]](mem PodMultiDimArrayMemory[T]) *MemoryServer {
	if mem == nil {
		return nil
	}
	var zero T
	impl := &elementMultiDimMemoryDirector[T]{
		dims: mem.Dimensions, dimCount: mem.DimCount,
		read: func(mp []uint64, dims []uint32, a []T, bp, c []uint64) error {
			return mem.Read(mp, &PodMultiDimArray[T]{Dims: dims, PodArray: a}, bp, c)
		},
		write: func(mp []uint64, dims []uint32, a []T, bp, c []uint64) error {
			return mem.Write(mp, &PodMultiDimArray[T]{Dims: dims, PodArray: a}, bp, c)
		},
		codec: mdListCodec[T]{list: podListCodec[T, P](), typeName: zero.RRType(), mdType: DataTypePodMultiDimArray},
	}
	d := swig.NewDirectorWrappedPodMultiDimArrayMemoryDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedPodMultiDimArrayMemoryDirector(d) })
	d.SetObjectheapid(id)
	return &MemoryServer{podmd: d}
}

// NewNamedMultiDimArrayMemoryServer adapts a service NamedMultiDimArrayMemory for its skeleton.
func NewNamedMultiDimArrayMemoryServer[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](mem NamedMultiDimArrayMemory[T]) *MemoryServer {
	if mem == nil {
		return nil
	}
	var zero T
	impl := &elementMultiDimMemoryDirector[T]{
		dims: mem.Dimensions, dimCount: mem.DimCount,
		read: func(mp []uint64, dims []uint32, a []T, bp, c []uint64) error {
			return mem.Read(mp, &NamedMultiDimArray[T]{Dims: dims, NamedArrayArray: a}, bp, c)
		},
		write: func(mp []uint64, dims []uint32, a []T, bp, c []uint64) error {
			return mem.Write(mp, &NamedMultiDimArray[T]{Dims: dims, NamedArrayArray: a}, bp, c)
		},
		codec: mdListCodec[T]{list: namedListCodec[T, E, P](), typeName: zero.RRType(), mdType: DataTypeNamedArrayMultiDimArray},
	}
	d := swig.NewDirectorWrappedNamedMultiDimArrayMemoryDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedNamedMultiDimArrayMemoryDirector(d) })
	d.SetObjectheapid(id)
	return &MemoryServer{namedmd: d}
}

// ---- pod and namedarray memory clients (buffer directors) ----

type arrayClientBuffer[T any] struct {
	buffer []T
	codec  listCodec[T]
}

func (b *arrayClientBuffer[T]) UnpackReadResult(res swig.MessageElementNestedElementList, bufferpos, count uint64) {
	defer recoverDirector()
	if res != nil && res.Swigcptr() != 0 {
		defer swig.DeleteMessageElementNestedElementList(res)
	}
	l, err := nestedFromNative(res)
	if err != nil {
		setDirectorError(err)
		return
	}
	a, err := b.codec.unpack(l)
	if err != nil {
		setDirectorError(err)
		return
	}
	if uint64(len(a)) != count || bufferpos+count > uint64(len(b.buffer)) {
		setDirectorError(NewOutOfRange("memory read result size mismatch"))
		return
	}
	copy(b.buffer[bufferpos:], a)
}

func (b *arrayClientBuffer[T]) PackWriteRequest(bufferpos, count uint64) swig.MessageElementNestedElementList {
	defer recoverDirector()
	if bufferpos+count > uint64(len(b.buffer)) {
		setDirectorError(NewOutOfRange("buffer too small"))
		return nil
	}
	l, err := b.codec.pack(b.buffer[bufferpos : bufferpos+count])
	if err != nil {
		setDirectorError(err)
		return nil
	}
	nl, err := nestedToNative(l)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return nl
}

func (b *arrayClientBuffer[T]) GetBufferLength() uint64 { return uint64(len(b.buffer)) }

type podArrayMemoryClient[T any] struct {
	pod   swig.WrappedPodArrayMemoryClient
	named swig.WrappedNamedArrayMemoryClient
	codec listCodec[T]
}

// NewPodArrayMemoryClient creates the client side of a pod array memory.
func NewPodArrayMemoryClient[T Pod, P PodPtr[T]](b *StubBase, name string) (PodArrayMemory[T], error) {
	m, err := call(func() swig.WrappedPodArrayMemoryClient { return b.inner.GetPodArrayMemory(name) })
	if err != nil {
		return nil, err
	}
	c := &podArrayMemoryClient[T]{pod: m, codec: podListCodec[T, P]()}
	addCleanup(c, m, swig.DeleteWrappedPodArrayMemoryClient)
	return c, nil
}

// NewNamedArrayMemoryClient creates the client side of a namedarray array memory.
func NewNamedArrayMemoryClient[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](b *StubBase, name string) (NamedArrayMemory[T], error) {
	m, err := call(func() swig.WrappedNamedArrayMemoryClient { return b.inner.GetNamedArrayMemory(name) })
	if err != nil {
		return nil, err
	}
	c := &podArrayMemoryClient[T]{named: m, codec: namedListCodec[T, E, P]()}
	addCleanup(c, m, swig.DeleteWrappedNamedArrayMemoryClient)
	return c, nil
}

func (m *podArrayMemoryClient[T]) Length() (uint64, error) {
	if m.pod != nil {
		return call(func() uint64 { return m.pod.Length() })
	}
	return call(func() uint64 { return m.named.Length() })
}

func (m *podArrayMemoryClient[T]) rw(write bool, memorypos uint64, buffer []T, bufferpos, count uint64) error {
	if err := checkRange(uint64(len(buffer)), bufferpos, count); err != nil {
		return err
	}
	impl := &arrayClientBuffer[T]{buffer: buffer, codec: m.codec}
	return callVoid(func() {
		if m.pod != nil {
			d := swig.NewDirectorWrappedPodArrayMemoryClientBuffer(impl)
			defer swig.DeleteDirectorWrappedPodArrayMemoryClientBuffer(d)
			if write {
				m.pod.Write(memorypos, d, bufferpos, count)
			} else {
				m.pod.Read(memorypos, d, bufferpos, count)
			}
			return
		}
		d := swig.NewDirectorWrappedNamedArrayMemoryClientBuffer(impl)
		defer swig.DeleteDirectorWrappedNamedArrayMemoryClientBuffer(d)
		if write {
			m.named.Write(memorypos, d, bufferpos, count)
		} else {
			m.named.Read(memorypos, d, bufferpos, count)
		}
	})
}

func (m *podArrayMemoryClient[T]) Read(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	return m.rw(false, memorypos, buffer, bufferpos, count)
}

func (m *podArrayMemoryClient[T]) Write(memorypos uint64, buffer []T, bufferpos, count uint64) error {
	return m.rw(true, memorypos, buffer, bufferpos, count)
}

type mdClientBuffer[T any] struct {
	dims   []uint32
	buffer []T
	codec  mdListCodec[T]
}

func (b *mdClientBuffer[T]) UnpackReadResult(res swig.MessageElementNestedElementList, bufferpos, count swig.Vector_uint64_t) {
	defer recoverDirector()
	if res != nil && res.Swigcptr() != 0 {
		defer swig.DeleteMessageElementNestedElementList(res)
	}
	l, err := nestedFromNative(res)
	if err != nil {
		setDirectorError(err)
		return
	}
	dims, a, err := b.codec.unpack(l)
	if err != nil {
		setDirectorError(err)
		return
	}
	c := u64sFromNative(count)
	if err := mdCopy(a, dims64(dims), make([]uint64, len(c)), b.buffer, dims64(b.dims), u64sFromNative(bufferpos), c); err != nil {
		setDirectorError(err)
	}
}

func (b *mdClientBuffer[T]) PackWriteRequest(bufferpos, count swig.Vector_uint64_t) swig.MessageElementNestedElementList {
	defer recoverDirector()
	c := u64sFromNative(count)
	n := uint64(1)
	for _, x := range c {
		n *= x
	}
	block := make([]T, n)
	if err := mdCopy(b.buffer, dims64(b.dims), u64sFromNative(bufferpos), block, c, make([]uint64, len(c)), c); err != nil {
		setDirectorError(err)
		return nil
	}
	l, err := b.codec.pack(dims32(c), block)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	nl, err := nestedToNative(l)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return nl
}

type elementMultiDimMemoryClient[T any] struct {
	pod   swig.WrappedPodMultiDimArrayMemoryClient
	named swig.WrappedNamedMultiDimArrayMemoryClient
	codec mdListCodec[T]
}

func (m *elementMultiDimMemoryClient[T]) Dimensions() ([]uint64, error) {
	return call(func() []uint64 {
		var v swig.Vector_uint64_t
		if m.pod != nil {
			v = m.pod.Dimensions()
		} else {
			v = m.named.Dimensions()
		}
		defer swig.DeleteVector_uint64_t(v)
		return u64sFromNative(v)
	})
}

func (m *elementMultiDimMemoryClient[T]) DimCount() (uint64, error) {
	if m.pod != nil {
		return call(func() uint64 { return m.pod.DimCount() })
	}
	return call(func() uint64 { return m.named.DimCount() })
}

func (m *elementMultiDimMemoryClient[T]) rw(write bool, memorypos []uint64, dims []uint32, buffer []T, bufferpos, count []uint64) error {
	impl := &mdClientBuffer[T]{dims: dims, buffer: buffer, codec: m.codec}
	return callVoid(func() {
		mp, bp, c := u64sToNative(memorypos), u64sToNative(bufferpos), u64sToNative(count)
		defer swig.DeleteVector_uint64_t(mp)
		defer swig.DeleteVector_uint64_t(bp)
		defer swig.DeleteVector_uint64_t(c)
		if m.pod != nil {
			d := swig.NewDirectorWrappedPodMultiDimArrayMemoryClientBuffer(impl)
			defer swig.DeleteDirectorWrappedPodMultiDimArrayMemoryClientBuffer(d)
			if write {
				m.pod.Write(mp, d, bp, c)
			} else {
				m.pod.Read(mp, d, bp, c)
			}
			return
		}
		d := swig.NewDirectorWrappedNamedMultiDimArrayMemoryClientBuffer(impl)
		defer swig.DeleteDirectorWrappedNamedMultiDimArrayMemoryClientBuffer(d)
		if write {
			m.named.Write(mp, d, bp, c)
		} else {
			m.named.Read(mp, d, bp, c)
		}
	})
}

type podMultiDimArrayMemoryClient[T Pod] struct{ elementMultiDimMemoryClient[T] }

func (m *podMultiDimArrayMemoryClient[T]) Read(memorypos []uint64, buffer *PodMultiDimArray[T], bufferpos, count []uint64) error {
	return m.rw(false, memorypos, buffer.Dims, buffer.PodArray, bufferpos, count)
}

func (m *podMultiDimArrayMemoryClient[T]) Write(memorypos []uint64, buffer *PodMultiDimArray[T], bufferpos, count []uint64) error {
	return m.rw(true, memorypos, buffer.Dims, buffer.PodArray, bufferpos, count)
}

// NewPodMultiDimArrayMemoryClient creates the client side of a pod multidim memory.
func NewPodMultiDimArrayMemoryClient[T Pod, P PodPtr[T]](b *StubBase, name string) (PodMultiDimArrayMemory[T], error) {
	m, err := call(func() swig.WrappedPodMultiDimArrayMemoryClient { return b.inner.GetPodMultiDimArrayMemory(name) })
	if err != nil {
		return nil, err
	}
	var zero T
	c := &podMultiDimArrayMemoryClient[T]{elementMultiDimMemoryClient[T]{pod: m,
		codec: mdListCodec[T]{list: podListCodec[T, P](), typeName: zero.RRType(), mdType: DataTypePodMultiDimArray}}}
	addCleanup(c, m, swig.DeleteWrappedPodMultiDimArrayMemoryClient)
	return c, nil
}

type namedMultiDimArrayMemoryClient[T any] struct{ elementMultiDimMemoryClient[T] }

func (m *namedMultiDimArrayMemoryClient[T]) Read(memorypos []uint64, buffer *NamedMultiDimArray[T], bufferpos, count []uint64) error {
	return m.rw(false, memorypos, buffer.Dims, buffer.NamedArrayArray, bufferpos, count)
}

func (m *namedMultiDimArrayMemoryClient[T]) Write(memorypos []uint64, buffer *NamedMultiDimArray[T], bufferpos, count []uint64) error {
	return m.rw(true, memorypos, buffer.Dims, buffer.NamedArrayArray, bufferpos, count)
}

// NewNamedMultiDimArrayMemoryClient creates the client side of a namedarray multidim memory.
func NewNamedMultiDimArrayMemoryClient[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](b *StubBase, name string) (NamedMultiDimArrayMemory[T], error) {
	m, err := call(func() swig.WrappedNamedMultiDimArrayMemoryClient { return b.inner.GetNamedMultiDimArrayMemory(name) })
	if err != nil {
		return nil, err
	}
	var zero T
	c := &namedMultiDimArrayMemoryClient[T]{elementMultiDimMemoryClient[T]{named: m,
		codec: mdListCodec[T]{list: namedListCodec[T, E, P](), typeName: zero.RRType(), mdType: DataTypeNamedArrayMultiDimArray}}}
	addCleanup(c, m, swig.DeleteWrappedNamedMultiDimArrayMemoryClient)
	return c, nil
}
