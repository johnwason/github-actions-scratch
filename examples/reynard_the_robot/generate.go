// Package reynard_the_robot is the Go version of the Reynard the Robot
// example. The service package is generated from the robdef:
//
//	go generate
//
// (RobotRaconteurGenGo must be on PATH.) See client/ and service/.
package reynard_the_robot

//go:generate mkdir -p gen
//go:generate RobotRaconteurGenGo --thunksource --go-import-prefix=github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/gen --output-dir gen robdef/experimental.reynard_the_robot.robdef
//go:generate gofmt -w gen
