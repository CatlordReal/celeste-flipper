// Portable, loopback-only UI and Flipper USB serial bridge. No installation.
package main

import (
	"bytes"
	"crypto/rand"
	"embed"
	"encoding/hex"
	"fmt"
	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
	"io"
	"net"
	"net/http"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

//go:embed celeste.html
var content embed.FS

type bridge struct {
	mu            sync.Mutex
	frame         []byte
	keys          byte
	keyTime, seen time.Time
	paused        bool
	status        string
	stop          chan struct{}
	once          sync.Once
}

func (b *bridge) quit() { b.once.Do(func() { close(b.stop) }) }
func (b *bridge) session(name string) {
	p, e := serial.Open(name, &serial.Mode{BaudRate: 115200})
	if e != nil {
		return
	}
	defer p.Close()
	p.SetReadTimeout(30 * time.Millisecond)
	p.SetDTR(true)
	defer p.SetDTR(false)
	p.ResetInputBuffer()
	pending := []byte{}
	buf := make([]byte, 2048)
	lastFrame := time.Time{}
	lastKeys := time.Time{}
	lastGood := time.Now()
	verified := false
	for {
		select {
		case <-b.stop:
			p.Write([]byte{'K', 0, '\n'})
			return
		default:
		}
		now := time.Now()
		if now.Sub(lastFrame) >= 120*time.Millisecond {
			if _, e = p.Write([]byte("F\n")); e != nil {
				return
			}
			lastFrame = now
		}
		if now.Sub(lastKeys) >= 50*time.Millisecond {
			b.mu.Lock()
			k := b.keys
			if now.Sub(b.keyTime) > 250*time.Millisecond {
				k = 0
			}
			pause := b.paused
			b.paused = false
			b.mu.Unlock()
			if _, e = p.Write([]byte{'K', k, '\n'}); e != nil {
				return
			}
			if pause {
				p.Write([]byte("P\n"))
			}
			lastKeys = now
		}
		n, e := p.Read(buf)
		if e != nil {
			return
		}
		pending = append(pending, buf[:n]...)
		if len(pending) > 32768 {
			pending = nil
		}
		for {
			idx := bytes.Index(pending, []byte("CLST"))
			if idx < 0 {
				if len(pending) > 3 {
					pending = pending[len(pending)-3:]
				}
				break
			}
			pending = pending[idx:]
			if len(pending) < 8196 {
				break
			}
			b.mu.Lock()
			b.frame = append(b.frame[:0], pending[:8196]...)
			b.status = "Connected to " + name
			b.mu.Unlock()
			pending = pending[8196:]
			lastGood = now
			verified = true
		}
		if (!verified && now.Sub(lastGood) > 1200*time.Millisecond) || (verified && now.Sub(lastGood) > 3*time.Second) {
			return
		}
	}
}
func (b *bridge) usb() {
	for {
		select {
		case <-b.stop:
			return
		default:
		}
		ports, e := enumerator.GetDetailedPortsList()
		if e == nil {
			for _, p := range ports {
				if p.IsUSB && strings.EqualFold(p.VID, "0483") && strings.EqualFold(p.PID, "5740") {
					b.session(p.Name)
				}
			}
		}
		b.mu.Lock()
		b.status = "Waiting for Flipper PC mode. Close qFlipper if it holds USB."
		b.mu.Unlock()
		select {
		case <-b.stop:
			return
		case <-time.After(time.Second):
		}
	}
}
func main() {
	b := &bridge{seen: time.Now(), status: "Waiting for Flipper PC mode", stop: make(chan struct{})}
	listener, e := net.Listen("tcp4", "127.0.0.1:0")
	if e != nil {
		return
	}
	secret := make([]byte, 16)
	if _, e = rand.Read(secret); e != nil {
		return
	}
	base := "/" + hex.EncodeToString(secret) + "/"
	origin := "http://" + listener.Addr().String()
	url := origin + base
	mux := http.NewServeMux()
	mux.HandleFunc(base, func(w http.ResponseWriter, r *http.Request) {
		if r.Host != listener.Addr().String() {
			http.Error(w, "Invalid host", 403)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'")
		route := strings.TrimPrefix(r.URL.Path, base)
		if r.Method == "POST" && r.Header.Get("Origin") != origin {
			http.Error(w, "Invalid origin", 403)
			return
		}
		switch route {
		case "":
			if r.Method != "GET" {
				http.Error(w, "Method", 405)
				return
			}
			html, _ := content.ReadFile("celeste.html")
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.Write(html)
		case "frame":
			if r.Method != "GET" {
				http.Error(w, "Method", 405)
				return
			}
			b.mu.Lock()
			b.seen = time.Now()
			f := append([]byte(nil), b.frame...)
			s := b.status
			b.mu.Unlock()
			if len(f) == 0 {
				http.Error(w, s, 503)
				return
			}
			w.Header().Set("Content-Type", "application/octet-stream")
			w.Write(f)
		case "status":
			b.mu.Lock()
			s := b.status
			b.mu.Unlock()
			fmt.Fprint(w, s)
		case "keys":
			if r.Method != "POST" {
				http.Error(w, "Method", 405)
				return
			}
			v, _ := io.ReadAll(io.LimitReader(r.Body, 8))
			n, e := strconv.Atoi(strings.TrimSpace(string(v)))
			if e != nil || n < 0 || n > 63 {
				http.Error(w, "Keys", 400)
				return
			}
			b.mu.Lock()
			b.keys = byte(n)
			b.keyTime = time.Now()
			b.mu.Unlock()
			w.WriteHeader(204)
		case "pause":
			if r.Method != "POST" {
				http.Error(w, "Method", 405)
				return
			}
			b.mu.Lock()
			b.paused = true
			b.mu.Unlock()
			w.WriteHeader(204)
		case "install":
			if r.Method != "POST" {
				http.Error(w, "Method", 405)
				return
			}
			target, err := installPermanent()
			if err != nil {
				http.Error(w, err.Error(), 400)
				return
			}
			fmt.Fprint(w, "Installed independent game: "+target)
		case "quit":
			if r.Method != "POST" {
				http.Error(w, "Method", 405)
				return
			}
			w.WriteHeader(204)
			go func() { time.Sleep(150 * time.Millisecond); b.quit() }()
		default:
			http.NotFound(w, r)
		}
	})
	server := &http.Server{Handler: mux, ReadHeaderTimeout: 3 * time.Second, ReadTimeout: 5 * time.Second, WriteTimeout: 5 * time.Second}
	go server.Serve(listener)
	go b.usb()
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	if e = cmd.Start(); e != nil {
		fmt.Println("Open", url)
	} else {
		go cmd.Wait()
	}
	go func() {
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-b.stop:
				return
			case <-ticker.C:
				b.mu.Lock()
				idle := time.Since(b.seen)
				b.mu.Unlock()
				if idle > 60*time.Second {
					b.quit()
					return
				}
			}
		}
	}()
	<-b.stop
	server.Close()
	time.Sleep(200 * time.Millisecond)
}
