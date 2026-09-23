package main

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	_ "embed"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"runtime"
	"strings"
)

//go:embed ccleste-win.zip
var cclesteWindowsZip []byte

const cclesteWindowsSHA256 = "73f07ddf101fb8274e3341f701bde62a83477b695cff42b172b48680995f1ec7"
const installMarker = "ccleste-v1.4.0\n"

func installPermanent() (string, error) {
	if runtime.GOOS != "windows" {
		return "", errors.New("Windows installer is unavailable on this platform")
	}
	if fmt.Sprintf("%x", sha256.Sum256(cclesteWindowsZip)) != cclesteWindowsSHA256 {
		return "", errors.New("embedded ccleste archive hash mismatch")
	}
	localAppData := os.Getenv("LOCALAPPDATA")
	if localAppData == "" {
		return "", errors.New("LOCALAPPDATA is unavailable")
	}
	target, err := installArchive(filepath.Join(localAppData, "Programs"), cclesteWindowsZip)
	if err != nil {
		return "", err
	}
	if err := createStartMenuShortcut(target); err != nil {
		return "", fmt.Errorf("game installed at %s; Start menu shortcut failed: %w", target, err)
	}
	return target, nil
}

func installArchive(programsDir string, archive []byte) (string, error) {
	target := filepath.Join(programsDir, "CelesteClassic")
	marker := filepath.Join(target, ".celeste-classic-install")
	if existing, err := os.ReadFile(marker); err == nil && string(existing) == installMarker {
		if info, err := os.Stat(filepath.Join(target, "ccleste.exe")); err != nil || info.IsDir() {
			return "", errors.New("existing install is incomplete; remove it manually before reinstalling")
		}
		return target, nil
	}
	if _, err := os.Lstat(target); err == nil {
		return "", fmt.Errorf("refusing to overwrite existing install: %s", target)
	} else if !errors.Is(err, os.ErrNotExist) {
		return "", err
	}
	if err := os.MkdirAll(programsDir, 0755); err != nil {
		return "", err
	}
	staging, err := os.MkdirTemp(programsDir, ".CelesteClassic-")
	if err != nil {
		return "", err
	}
	success := false
	defer func() {
		if !success {
			_ = os.RemoveAll(staging)
		}
	}()
	if err := extractArchive(archive, staging); err != nil {
		return "", err
	}
	if info, err := os.Stat(filepath.Join(staging, "ccleste.exe")); err != nil || info.IsDir() {
		return "", errors.New("archive does not contain ccleste.exe")
	}
	if err := os.WriteFile(filepath.Join(staging, ".celeste-classic-install"), []byte(installMarker), 0644); err != nil {
		return "", err
	}
	if err := os.Rename(staging, target); err != nil {
		return "", err
	}
	success = true
	return target, nil
}

func extractArchive(archive []byte, destination string) error {
	reader, err := zip.NewReader(bytes.NewReader(archive), int64(len(archive)))
	if err != nil {
		return err
	}
	for _, entry := range reader.File {
		clean := path.Clean(strings.ReplaceAll(entry.Name, "\\", "/"))
		if clean == "." {
			continue
		}
		if strings.HasPrefix(clean, "../") || clean == ".." || strings.HasPrefix(clean, "/") || strings.Contains(clean, ":") {
			return fmt.Errorf("unsafe archive path: %q", entry.Name)
		}
		if entry.Mode()&os.ModeSymlink != 0 {
			return fmt.Errorf("symlink archive entry rejected: %q", entry.Name)
		}
		output := filepath.Join(destination, filepath.FromSlash(clean))
		if entry.FileInfo().IsDir() {
			if err := os.MkdirAll(output, 0755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(output), 0755); err != nil {
			return err
		}
		input, err := entry.Open()
		if err != nil {
			return err
		}
		file, err := os.OpenFile(output, os.O_WRONLY|os.O_CREATE|os.O_EXCL, entry.Mode().Perm())
		if err == nil {
			_, err = io.Copy(file, input)
			closeErr := file.Close()
			if err == nil {
				err = closeErr
			}
		}
		input.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

func createStartMenuShortcut(target string) error {
	if runtime.GOOS != "windows" {
		return errors.New("Windows shortcut unavailable")
	}
	appData := os.Getenv("APPDATA")
	if appData == "" {
		return errors.New("APPDATA is unavailable")
	}
	shortcut := filepath.Join(appData, "Microsoft", "Windows", "Start Menu", "Programs", "Celeste Classic.lnk")
	quote := func(value string) string { return "'" + strings.ReplaceAll(value, "'", "''") + "'" }
	script := "$ws=New-Object -ComObject WScript.Shell;$s=$ws.CreateShortcut(" + quote(shortcut) + ");$s.TargetPath=" + quote(filepath.Join(target, "ccleste.exe")) + ";$s.WorkingDirectory=" + quote(target) + ";$s.Save()"
	return exec.Command("powershell", "-NoProfile", "-NonInteractive", "-Command", script).Run()
}
