package main

import (
	"github.com/tuneinsight/lattigo/v6/circuits/ckks/minimax"
)

func main() {
	minimax.GenMinimaxCompositePolynomialForSign(256, 16, 35, []int{5, 5, 5, 5, 5, 7, 7, 15})
}
