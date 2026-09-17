//go:build !linux && !darwin

package sysinfo

import "runtime"

// runtime heap stats fallback
func PeakRAMMB() float64 {
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	return float64(m.Sys) / (1024.0 * 1024.0)
}

func ChildrenPeakRAMMB() float64 { return 0 }
