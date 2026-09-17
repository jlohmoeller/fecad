package lattigo

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

// Whole consent path on a predicate that does real FHE work

func TestConsentPipeline_RealPredicate(t *testing.T) {
	b, resDir, prvDir := setupProviderAndResearcher(t)
	proxyDir := t.TempDir()
	kskPath := filepath.Join(t.TempDir(), "ksk.bin")
	if err := b.GenerateKSK(resDir, prvDir, kskPath); err != nil {
		t.Fatal(err)
	}
	const rows = 8
	vals := []int{0, 10, 15, 20, 100, 255, 14, 16}
	writeIntColumn(t, prvDir, "creatinine_q", vals)
	if err := b.EncryptData(prvDir, ""); err != nil {
		t.Fatal(err)
	}
	if err := b.EvaluatePredicate(prvDir, "0,15,GE,NONE,255"); err != nil {
		t.Fatal(err)
	}

	// raw evaluation result first
	rawAgg := filepath.Join(t.TempDir(), "raw_agg.bin")
	if err := b.Aggregate(kskPath, filepath.Join(prvDir, "result_query.bin"), rawAgg); err != nil {
		t.Fatal(err)
	}
	t.Logf("after evaluate:  %v", decryptToSlice(t, b, resDir, rawAgg, rows)[:rows])

	patients := map[string][]int{}
	for i := 0; i < rows; i++ {
		patients[patientID(i)] = []int{i}
	}
	if err := b.SetupAllPatientsBlinding(prvDir, proxyDir, patients); err != nil {
		t.Fatal(err)
	}
	cancDir := filepath.Join(proxyDir, cancellationSubdir)
	rtp := make([]string, rows)
	for i := range rtp {
		rtp[i] = patientID(i)
	}
	raw, _ := json.Marshal(rtp)
	if err := os.WriteFile(filepath.Join(cancDir, "row_to_patient.json"), raw, 0o644); err != nil {
		t.Fatal(err)
	}

	blinded := filepath.Join(t.TempDir(), "blinded.bin")
	if err := b.ApplyBlinding(filepath.Join(prvDir, "result_query.bin"),
		filepath.Join(prvDir, blindingSubdir), blinded); err != nil {
		t.Fatal(err)
	}
	blindAgg := filepath.Join(t.TempDir(), "blind_agg.bin")
	if err := b.Aggregate(kskPath, blinded, blindAgg); err != nil {
		t.Fatal(err)
	}
	t.Logf("after blinding:  %v", decryptToSlice(t, b, resDir, blindAgg, rows)[:rows])

	all := make([]string, rows)
	for i := range all {
		all[i] = patientID(i)
	}
	cancelled := filepath.Join(t.TempDir(), "cancelled.bin")
	if err := b.ApplyCancellation(blinded, cancDir, all, cancelled); err != nil {
		t.Fatal(err)
	}
	finalAgg := filepath.Join(t.TempDir(), "final.bin")
	if err := b.Aggregate(kskPath, cancelled, finalAgg); err != nil {
		t.Fatal(err)
	}
	got := decryptToSlice(t, b, resDir, finalAgg, rows)
	for i, v := range vals {
		want := uint64(0)
		if v >= 15 {
			want = 1
		}
		if got[i] != want {
			t.Errorf("row %d (creatinine_q=%d): got %d, want %d", i, v, got[i], want)
		}
	}
}

func patientID(i int) string { return "p" + string(rune('a'+i)) }

// fresh researcher + provider keys
func setupProviderAndResearcher(t *testing.T) (b *Backend, resDir, prvDir string) {
	t.Helper()
	b = New()
	resDir, prvDir = t.TempDir(), t.TempDir()
	if err := b.GenerateResearcherKey(resDir); err != nil {
		t.Fatalf("GenerateResearcherKey: %v", err)
	}
	if err := b.GenerateProviderKeys(prvDir); err != nil {
		t.Fatalf("GenerateProviderKeys: %v", err)
	}
	return
}

// single-column data.json
func writeIntColumn(t *testing.T, dir, colName string, vals []int) {
	t.Helper()
	rows := make([]map[string]any, len(vals))
	for i, v := range vals {
		rows[i] = map[string]any{colName: v}
	}
	raw, _ := json.Marshal(rows)
	if err := os.WriteFile(filepath.Join(dir, "data.json"), raw, 0o644); err != nil {
		t.Fatal(err)
	}
}

// decrypt first chunk to slots
func decryptToSlice(t *testing.T, b *Backend, resDir, ctPath string, rows int) []uint64 {
	t.Helper()
	decPath := filepath.Join(t.TempDir(), "dec.json")
	if err := b.DecryptResult(resDir, ctPath, decPath, rows); err != nil {
		t.Fatalf("DecryptResult: %v", err)
	}
	raw, _ := os.ReadFile(decPath)
	var results [][]uint64
	if err := json.Unmarshal(raw, &results); err != nil || len(results) == 0 {
		t.Fatalf("parse results: %v", err)
	}
	return results[0]
}
