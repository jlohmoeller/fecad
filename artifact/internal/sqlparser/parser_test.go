package sqlparser

import (
	"strings"
	"testing"

	"github.com/fecad/internal/schema"
)

func TestParse_QuantizedLiteralNotRescaled(t *testing.T) {
	sc, err := schema.Load("../../schemas/mimic_iv.json")
	if err != nil {
		t.Fatalf("load schema: %v", err)
	}
	got, err := Parse("SELECT * FROM patients WHERE creatinine_q >= 15 AND age >= 60", sc)
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}
	want := "6,15,GE,NONE,255;0,60,GE,AND,127"
	if got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestParse_LiteralOutsideDomain(t *testing.T) {
	sc, err := schema.Load("../../schemas/mimic_iv.json")
	if err != nil {
		t.Fatalf("load schema: %v", err)
	}
	_, err = Parse("SELECT * FROM patients WHERE creatinine_q >= 1500", sc)
	if err == nil || !strings.Contains(err.Error(), "outside domain") {
		t.Fatalf("want domain error, got %v", err)
	}
}
