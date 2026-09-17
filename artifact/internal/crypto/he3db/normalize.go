package he3db

import (
	"cmp"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"slices"

	"github.com/fecad/internal/schema"
)

// Coerce every schema column in data.json to an int, missing ones to 0, so the
// bridge can read them as uint32
// Undeclared fields stay: the harness reads its own "consented" flag back out of
// this file, and projecting them away left every he3db run unscored
func normalizeDataJSON(dataDir, schemaPath string) error {
	sch, err := schema.Load(schemaPath)
	if err != nil {
		return fmt.Errorf("load schema: %w", err)
	}

	cols := slices.Clone(sch.Columns)
	slices.SortFunc(cols, func(a, b schema.Column) int { return cmp.Compare(a.Index, b.Index) })

	dataPath := filepath.Join(dataDir, "data.json")
	raw, err := os.ReadFile(dataPath)
	if err != nil {
		return err
	}
	var rows []map[string]any
	if err := json.Unmarshal(raw, &rows); err != nil {
		return fmt.Errorf("parse data.json: %w", err)
	}
	if len(rows) == 0 {
		return fmt.Errorf("data.json has no rows")
	}

	mapped := make([]map[string]any, len(rows))
	for i, row := range rows {
		out := make(map[string]any, len(row))
		for k, v := range row {
			out[k] = v
		}
		for _, col := range cols {
			val, ok := row[col.Name]
			if !ok {
				out[col.Name] = 0
				continue
			}
			v, err := coerceInt(val)
			if err != nil {
				return fmt.Errorf("data.json col %q: %w", col.Name, err)
			}
			out[col.Name] = v
		}
		mapped[i] = out
	}

	outRaw, err := json.Marshal(mapped)
	if err != nil {
		return fmt.Errorf("marshal he3db data: %w", err)
	}
	return os.WriteFile(dataPath, outRaw, 0o644)
}

func coerceInt(v any) (int, error) {
	switch t := v.(type) {
	case float64:
		return int(t), nil
	case int:
		return t, nil
	case int64:
		return int(t), nil
	case json.Number:
		iv, err := t.Int64()
		return int(iv), err
	case bool:
		// boolean columns (e.g. mimic_iv flags) coerce to 0/1
		if t {
			return 1, nil
		}
		return 0, nil
	default:
		return 0, fmt.Errorf("unsupported type %T", v)
	}
}
