package lattigo

import (
	"encoding/binary"
	"fmt"
	"io"
	"os"

	"github.com/tuneinsight/lattigo/v6/core/rlwe"
	"github.com/tuneinsight/lattigo/v6/schemes/bgv"
)

// Key and ciphertext files start with a 4-byte little-endian length prefix

func writeObject(path string, v interface{ MarshalBinary() ([]byte, error) }) (err error) {
	b, mErr := v.MarshalBinary()
	if mErr != nil {
		return fmt.Errorf("marshal: %w", mErr)
	}
	f, cErr := os.Create(path)
	if cErr != nil {
		return cErr
	}
	defer func() {
		if closeErr := f.Close(); err == nil {
			err = closeErr
		}
	}()
	hdr := make([]byte, 4)
	binary.LittleEndian.PutUint32(hdr, uint32(len(b)))
	if _, err = f.Write(hdr); err != nil {
		return err
	}
	if _, err = f.Write(b); err != nil {
		return err
	}
	return f.Sync()
}

// length-prefixed blob
func readBytes(r io.Reader) ([]byte, error) {
	hdr := make([]byte, 4)
	if _, err := io.ReadFull(r, hdr); err != nil {
		return nil, err
	}
	n := binary.LittleEndian.Uint32(hdr)
	buf := make([]byte, n)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}
	return buf, nil
}

func readObject(path string, v interface{ UnmarshalBinary([]byte) error }) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	b, err := readBytes(f)
	if err != nil {
		return err
	}
	return v.UnmarshalBinary(b)
}

// sk -> <dir>/secret_key.bin
func WriteSK(dir string, sk *rlwe.SecretKey) error {
	return writeObject(dir+"/secret_key.bin", sk)
}

// sk <- <dir>/secret_key.bin
func ReadSK(params bgv.Parameters, dir string) (*rlwe.SecretKey, error) {
	sk := rlwe.NewSecretKey(params.Parameters)
	if err := readObject(dir+"/secret_key.bin", sk); err != nil {
		return nil, err
	}
	return sk, nil
}

// pk -> <dir>/public_key.bin; only the role-split deployment needs it, where
// the evaluator has no sk_h and encrypts public constants under pk_h (paper §7.2)
func WritePK(dir string, pk *rlwe.PublicKey) error {
	return writeObject(dir+"/public_key.bin", pk)
}

// pk <- <dir>/public_key.bin
func ReadPK(params bgv.Parameters, dir string) (*rlwe.PublicKey, error) {
	pk := rlwe.NewPublicKey(params.Parameters)
	if err := readObject(dir+"/public_key.bin", pk); err != nil {
		return nil, err
	}
	return pk, nil
}

// eval key (rlk or ksk) -> path
func WriteEvalKey(path string, evk *rlwe.EvaluationKey) error {
	return writeObject(path, evk)
}

// eval key <- path
func ReadEvalKey(params bgv.Parameters, path string) (*rlwe.EvaluationKey, error) {
	evk := new(rlwe.EvaluationKey)
	if err := readObject(path, evk); err != nil {
		return nil, err
	}
	return evk, nil
}

func WriteCT(w io.Writer, ct *rlwe.Ciphertext) error {
	b, err := ct.MarshalBinary()
	if err != nil {
		return err
	}
	hdr := make([]byte, 4)
	binary.LittleEndian.PutUint32(hdr, uint32(len(b)))
	if _, err := w.Write(hdr); err != nil {
		return err
	}
	_, err = w.Write(b)
	return err
}

func ReadCT(params bgv.Parameters, r io.Reader) (*rlwe.Ciphertext, error) {
	b, err := readBytes(r)
	if err != nil {
		return nil, err
	}
	ct := bgv.NewCiphertext(params, 1, params.MaxLevel())
	if err := ct.UnmarshalBinary(b); err != nil {
		return nil, err
	}
	return ct, nil
}

// CT slice -> path, 4-byte count first; fsync as in writeObject
func WriteCTs(path string, cts []*rlwe.Ciphertext) (err error) {
	f, cErr := os.Create(path)
	if cErr != nil {
		return cErr
	}
	defer func() {
		if closeErr := f.Close(); err == nil {
			err = closeErr
		}
	}()
	cnt := make([]byte, 4)
	binary.LittleEndian.PutUint32(cnt, uint32(len(cts)))
	if _, err = f.Write(cnt); err != nil {
		return err
	}
	for _, ct := range cts {
		if err = WriteCT(f, ct); err != nil {
			return err
		}
	}
	return f.Sync()
}

func ReadCTs(params bgv.Parameters, path string) ([]*rlwe.Ciphertext, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	hdr := make([]byte, 4)
	if _, err := io.ReadFull(f, hdr); err != nil {
		return nil, err
	}
	n := int(binary.LittleEndian.Uint32(hdr))
	cts := make([]*rlwe.Ciphertext, n)
	for i := range cts {
		ct, err := ReadCT(params, f)
		if err != nil {
			return nil, fmt.Errorf("ct %d: %w", i, err)
		}
		cts[i] = ct
	}
	return cts, nil
}
