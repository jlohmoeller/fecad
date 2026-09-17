//go:build darwin

package sysinfo

import "syscall"

// peak RSS of this process, MiB
// macOS reports ru_maxrss in bytes
func PeakRAMMB() float64 {
	var r syscall.Rusage
	if err := syscall.Getrusage(syscall.RUSAGE_SELF, &r); err != nil {
		return 0
	}
	return float64(r.Maxrss) / (1024.0 * 1024.0)
}

// peak RSS of waited-for children
func ChildrenPeakRAMMB() float64 {
	var r syscall.Rusage
	if err := syscall.Getrusage(syscall.RUSAGE_CHILDREN, &r); err != nil {
		return 0
	}
	return float64(r.Maxrss) / (1024.0 * 1024.0)
}
