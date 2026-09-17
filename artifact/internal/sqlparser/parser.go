// Package sqlparser compiles a WHERE clause into FHE instructions
//
//	col_idx,value,OP,LOGIC,max_val    (conditions joined by ";")
//
// OP ∈ {EQ,LT,GT,LE,GE}, LOGIC ∈ {NONE,AND,OR}, first condition is NONE
//
// AND binds tighter than OR, so "a AND b OR c AND d" is (a∧b)∨(c∧d): each
// backend's eval loop keeps a per-group accumulator and ORs completed groups
// into the result accumulator (see null.evalConds)
//
// subset: SELECT * FROM <t> WHERE col OP literal [AND|OR col OP literal ...]
package sqlparser

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/fecad/internal/schema"
)

var (
	whereRe = regexp.MustCompile(`(?i)\bWHERE\b(.+)$`)
	// ident op numeric-literal
	condRe = regexp.MustCompile(`(\w+)\s*(>=|<=|<>|!=|=|>|<)\s*(-?[\d.]+)`)
	connRe = regexp.MustCompile(`(?i)\b(AND|OR)\b`)
)

type cond struct {
	connective string // NONE | AND | OR
	colName    string
	op         string // EQ | LT | GT | LE | GE
	rawVal     float64
}

// compile SQL to FHE instructions
// a nil schema skips column lookup, for tests only
func Parse(sql string, sc *schema.Schema) (string, error) {
	m := whereRe.FindStringSubmatch(sql)
	if m == nil {
		return "", nil
	}
	whereClause := strings.TrimSpace(m[1])

	conds, err := tokenise(whereClause)
	if err != nil {
		return "", err
	}

	instructions := make([]string, 0, len(conds))
	for _, c := range conds {
		var colIdx int
		var maxVal int64

		if sc != nil {
			col, ok := sc.ColumnByName(c.colName)
			if !ok {
				return "", fmt.Errorf("sqlparser: unknown column %q", c.colName)
			}
			colIdx = col.Index
			maxVal = int64((1 << col.Bits) - 1)
		}

		// literals are already in the column's stored (quantized) units, so
		// creatinine_q at scale 100 takes 150 for 1.5 mg/dL; one outside the
		// domain is an authoring error, not a false predicate, because it
		// short-circuits HomCompare to a constant ciphertext
		val := int64(c.rawVal)
		if sc != nil && (val < 0 || val > maxVal) {
			return "", fmt.Errorf("sqlparser: literal %v outside domain [0,%d] of column %q",
				c.rawVal, maxVal, c.colName)
		}
		instructions = append(instructions,
			fmt.Sprintf("%d,%d,%s,%s,%d", colIdx, val, c.op, c.connective, maxVal))
	}
	return strings.Join(instructions, ";"), nil
}

// split WHERE body into ordered conditions
func tokenise(where string) ([]cond, error) {
	connLocs := connRe.FindAllStringIndex(where, -1)
	connStrings := connRe.FindAllString(where, -1)

	var parts []string
	prev := 0
	for _, loc := range connLocs {
		parts = append(parts, where[prev:loc[0]])
		prev = loc[1]
	}
	parts = append(parts, where[prev:])

	var conds []cond
	for i, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		cm := condRe.FindStringSubmatch(part)
		if cm == nil {
			return nil, fmt.Errorf("sqlparser: cannot parse condition %q", part)
		}
		op, err := mapOp(cm[2])
		if err != nil {
			return nil, err
		}
		val, err := strconv.ParseFloat(cm[3], 64)
		if err != nil {
			return nil, fmt.Errorf("sqlparser: bad literal %q: %w", cm[3], err)
		}
		conn := "NONE"
		if i > 0 && i-1 < len(connStrings) {
			conn = strings.ToUpper(connStrings[i-1])
		}
		conds = append(conds, cond{
			connective: conn,
			colName:    cm[1],
			op:         op,
			rawVal:     val,
		})
	}
	return conds, nil
}

func mapOp(op string) (string, error) {
	switch op {
	case "=":
		return "EQ", nil
	case "<":
		return "LT", nil
	case ">":
		return "GT", nil
	case "<=":
		return "LE", nil
	case ">=":
		return "GE", nil
	default:
		return "", fmt.Errorf("sqlparser: unsupported operator %q", op)
	}
}
