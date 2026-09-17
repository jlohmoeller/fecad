// Package schema loads dataset schemas
package schema

import (
	"cmp"
	"encoding/json"
	"fmt"
	"os"
	"slices"
)

// one attribute column
type Column struct {
	Index int    `json:"index"`
	Name  string `json:"name"`
	Type  string `json:"type"`
	Bits  int    `json:"bits"`
	// multiplier from float literal to stored integer
	// creatinine_q has scale=100, so 1.5 mg/dL is stored as 150
	// zero or absent means 1
	Scale int `json:"scale"`
}

// parsed dataset schema
type Schema struct {
	Name        string   `json:"name"`
	Description string   `json:"description"`
	Columns     []Column `json:"columns"`
	byName      map[string]Column
}

// parse schema JSON file
func Load(path string) (*Schema, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("schema: read %s: %w", path, err)
	}
	var s Schema
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, fmt.Errorf("schema: parse %s: %w", path, err)
	}
	s.byName = make(map[string]Column, len(s.Columns))
	for _, c := range s.Columns {
		s.byName[c.Name] = c
	}
	return &s, nil
}

func (s *Schema) ColumnByName(name string) (Column, bool) {
	c, ok := s.byName[name]
	return c, ok
}

// column names in declared-index order
// every backend must encrypt in this order: predicate instructions carry
// schema indices, so any other order evaluates against the wrong column
func (s *Schema) ColumnOrder() []string {
	ordered := slices.Clone(s.Columns)
	slices.SortFunc(ordered, func(a, b Column) int { return cmp.Compare(a.Index, b.Index) })
	names := make([]string, len(ordered))
	for i, c := range ordered {
		names[i] = c.Name
	}
	return names
}

// scale, defaulting to 1
func (c Column) EffectiveScale() int {
	if c.Scale <= 0 {
		return 1
	}
	return c.Scale
}
