package robotraconteur

import (
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// LogLevel is a Robot Raconteur log level.
type LogLevel int32

// Log levels.
const (
	LogLevelTrace   LogLevel = 0
	LogLevelDebug   LogLevel = 1
	LogLevelInfo    LogLevel = 2
	LogLevelWarning LogLevel = 3
	LogLevelError   LogLevel = 4
	LogLevelFatal   LogLevel = 5
	LogLevelDisable LogLevel = 1000
)

// LogRecord is a Robot Raconteur log record.
type LogRecord struct {
	Level             LogLevel
	Component         int32
	ComponentName     string
	ComponentObjectID string
	Endpoint          int64
	ServicePath       string
	Member            string
	Message           string
	Time              time.Time
	SourceFile        string
	SourceLine        uint32
	ThreadID          string
	FiberID           string
	text              string
}

// String formats the record as the core does.
func (r LogRecord) String() string { return r.text }

func logRecordFromNative(r swig.RRLogRecord) LogRecord {
	return LogRecord{
		Level: LogLevel(r.GetLevel()), Component: int32(r.GetComponent()), ComponentName: r.GetComponentName(),
		ComponentObjectID: r.GetComponentObjectID(), Endpoint: r.GetEndpoint(), ServicePath: r.GetServicePath(),
		Member: r.GetMember(), Message: r.GetMessage(), Time: timeFromMicros(r.GetTime()),
		SourceFile: r.GetSourceFile(), SourceLine: r.GetSourceLine(), ThreadID: r.GetThreadID(), FiberID: r.GetFiberID(),
		text: r.ToString(),
	}
}

// LogLevel returns the node log level.
func (n *Node) LogLevel() LogLevel { return LogLevel(n.native.GetLogLevel()) }

// SetLogLevel sets the node log level.
func (n *Node) SetLogLevel(l LogLevel) {
	n.native.SetLogLevel(swig.RobotRaconteurRobotRaconteur_LogLevel(l))
}

// SetLogLevelFromString sets the log level from a name ("DEBUG", "INFO", ...).
func (n *Node) SetLogLevelFromString(level string) (LogLevel, error) {
	l, err := call(func() swig.RobotRaconteurRobotRaconteur_LogLevel { return n.native.SetLogLevelFromString(level) })
	return LogLevel(l), err
}

// SetLogLevelFromEnv sets the log level from ROBOTRACONTEUR_LOG_LEVEL.
func (n *Node) SetLogLevelFromEnv() (LogLevel, error) {
	l, err := call(func() swig.RobotRaconteurRobotRaconteur_LogLevel {
		return swig.SwigcptrRobotRaconteurNode(n.native.Swigcptr()).SetLogLevelFromEnvVariable__SWIG_1()
	})
	return LogLevel(l), err
}

// LogMessage logs a message through the node.
func (n *Node) LogMessage(level LogLevel, message string) {
	n.native.LogMessage(swig.RobotRaconteurRobotRaconteur_LogLevel(level), message)
}

type logHandler struct{ f func(LogRecord) }

func (h *logHandler) HandleLogRecord(r swig.RRLogRecord) {
	defer recoverDirector()
	h.f(logRecordFromNative(r))
}

var _ swig.UserLogRecordHandlerDirectorMethods = (*logHandler)(nil)

var logHandlerMu sync.Mutex

// SetLogRecordHandler sends log records to f instead of stderr. f is called
// on Robot Raconteur threads and must not block. A nil f restores the
// default stderr output.
func (n *Node) SetLogRecordHandler(f func(LogRecord)) error {
	logHandlerMu.Lock()
	defer logHandlerMu.Unlock()
	if f == nil {
		return callVoid(func() { n.native.SetLogRecordHandler(swig.SwigcptrLogRecordHandler(0)) })
	}
	return callVoid(func() {
		h := swig.NewUserLogRecordHandlerBase()
		defer swig.DeleteUserLogRecordHandlerBase(h)
		impl := &logHandler{f: f}
		d := swig.NewDirectorUserLogRecordHandlerDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorUserLogRecordHandlerDirector(d) })
		swig.SwigcptrUserLogRecordHandlerBase(h.Swigcptr()).X_SetHandler(d, id)
		lh := h.SwigGetLogRecordHandler()
		defer swig.DeleteLogRecordHandler(lh)
		n.native.SetLogRecordHandler(lh)
	})
}

// OpenLogFile writes log records to a file instead of stderr.
func (n *Node) OpenLogFile(filename string, append bool) error {
	return callVoid(func() { swig.SetFileLogRecordHandler(n.native, filename, append) })
}
