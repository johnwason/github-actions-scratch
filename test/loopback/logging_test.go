package loopback

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

func TestLogging(t *testing.T) {
	n := rr.DefaultNode()
	old := n.LogLevel()
	defer n.SetLogLevel(old)

	l, err := n.SetLogLevelFromString("INFO")
	if err != nil || l != rr.LogLevelInfo || n.LogLevel() != rr.LogLevelInfo {
		t.Fatalf("SetLogLevelFromString: %v %v", l, err)
	}

	recs := make(chan rr.LogRecord, 16)
	if err := n.SetLogRecordHandler(func(r rr.LogRecord) {
		select {
		case recs <- r:
		default:
		}
	}); err != nil {
		t.Fatal(err)
	}
	n.LogMessage(rr.LogLevelDebug, "filtered")
	n.LogMessage(rr.LogLevelWarning, "go logging test")
	select {
	case r := <-recs:
		if r.Message != "go logging test" || r.Level != rr.LogLevelWarning {
			t.Errorf("unexpected record %+v", r)
		}
		// Records from LogMessage carry no timestamp (not_a_date_time).
		if (!r.Time.IsZero() && time.Since(r.Time) > time.Minute) || !strings.Contains(r.String(), "go logging test") {
			t.Errorf("bad record time/string %v %q", r.Time, r.String())
		}
	case <-time.After(5 * time.Second):
		t.Fatal("log record not received")
	}

	f := filepath.Join(t.TempDir(), "rr.log")
	if err := n.OpenLogFile(f, false); err != nil {
		t.Fatal(err)
	}
	n.LogMessage(rr.LogLevelError, "go file log test")
	if err := n.OpenLogFile(filepath.Join(t.TempDir(), "nodir", "x", "rr.log"), false); err == nil {
		t.Log("opening log in missing directory did not fail")
	}
	b, _ := os.ReadFile(f)
	if !strings.Contains(string(b), "go file log test") {
		t.Errorf("log file content %q", b)
	}
	if err := n.SetLogRecordHandler(nil); err != nil {
		t.Error(err)
	}
}
