package main

import (
	"archive/zip"
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func zipBytes(t *testing.T, entries map[string]string) []byte {
	t.Helper()
	var output bytes.Buffer
	writer := zip.NewWriter(&output)
	for name, body := range entries {
		file, err := writer.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := file.Write([]byte(body)); err != nil {
			t.Fatal(err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	return output.Bytes()
}

func TestInstallArchiveExtractsWithoutOverwriting(t *testing.T) {
	programs := t.TempDir()
	installed, err := installArchive(programs, zipBytes(t, map[string]string{"ccleste.exe": "game", "data/save.txt": "asset"}))
	if err != nil {
		t.Fatal(err)
	}
	if got, err := os.ReadFile(filepath.Join(installed, "data", "save.txt")); err != nil || string(got) != "asset" {
		t.Fatalf("asset: %q, %v", got, err)
	}
	again, err := installArchive(programs, []byte("not a zip"))
	if err != nil || again != installed {
		t.Fatalf("marker install: %q, %v", again, err)
	}
}

func TestInstallArchiveRejectsTraversal(t *testing.T) {
	programs := t.TempDir()
	_, err := installArchive(programs, zipBytes(t, map[string]string{"../outside.txt": "bad", "ccleste.exe": "game"}))
	if err == nil {
		t.Fatal("expected traversal rejection")
	}
	if _, statErr := os.Stat(filepath.Join(filepath.Dir(programs), "outside.txt")); !os.IsNotExist(statErr) {
		t.Fatalf("outside file: %v", statErr)
	}
}
