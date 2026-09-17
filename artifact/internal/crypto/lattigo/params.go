// Package lattigo implements crypto.Backend using Lattigo v6 BGV

package lattigo

import (
	"sync"

	"github.com/tuneinsight/lattigo/v6/schemes/bgv"
)

// BGV literal, all entities
func protoParams() bgv.ParametersLiteral {
	return bgv.ParametersLiteral{
		LogN: 15,
		LogQ: []int{
			50, // head prime
			30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
			30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
			30, 30, 30, 30, // 24 levels
		},
		LogP:             []int{55, 55},
		PlaintextModulus: 65537,
	}
}

// cached parameter set
var (
	defaultParams     bgv.Parameters
	defaultParamsErr  error
	defaultParamsOnce sync.Once
)

func DefaultParams() (bgv.Parameters, error) {
	defaultParamsOnce.Do(func() {
		defaultParams, defaultParamsErr = bgv.NewParametersFromLiteral(protoParams())
	})
	return defaultParams, defaultParamsErr
}
