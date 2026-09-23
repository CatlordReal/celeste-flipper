#include "windows_launch.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb_hid.h>

#include "windows_payload.h"

#define HID_CONNECT_TIMEOUT_MS 10000U
#define HID_KEY_DELAY_MS       6U

/*
 * PowerShell scans only Flipper's VID/PID serial ports, accepts only the CEXE
 * protocol header, bounds the payload to 16 MiB, closes serial before launch,
 * waits for the helper, then removes only its GUID-named temporary directory.
 */
static const char windows_bootstrap[] =
    "$d=Join-Path $env:TEMP ('Celeste-'+[guid]::NewGuid());"
    "New-Item -ItemType Directory $d|Out-Null;"
    "try{$e=(Get-Date).AddSeconds(180);$ok=$false;"
    "do{foreach($n in @(Get-CimInstance Win32_SerialPort|Where-Object{"
    "$_.PNPDeviceID -match 'VID_0483&PID_5740'}|ForEach-Object DeviceID)){"
    "try{$s=[IO.Ports.SerialPort]::new($n,115200,[IO.Ports.Parity]::None,8,"
    "[IO.Ports.StopBits]::One);$s.ReadTimeout=2000;$s.WriteTimeout=700;"
    "$s.DtrEnable=$true;$s.Open();$s.Write(('EXE'+[char]10));"
    "$h=[byte[]]::new(8);$o=0;while($o-lt 8){$o+=$s.Read($h,$o,8-$o)};"
    "if([Text.Encoding]::ASCII.GetString($h,0,4)-ne'CEXE'){throw 'header'};"
    "$z=[BitConverter]::ToUInt32($h,4);if($z-gt 16777216){throw 'size'};"
    "if($z-eq 0){throw 'empty'};"
    "$b=[byte[]]::new($z);$o=0;while($o-lt$z){if((Get-Date)-gt$e){throw 'timeout'};$o+=$s.Read($b,$o,$z-$o)};"
    "$p=Join-Path $d 'Celeste.exe';[IO.File]::WriteAllBytes($p,$b);"
    "if((Get-FileHash -Algorithm SHA256 $p).Hash -ne '" CELESTE_WINDOWS_SHA256
    "'){throw 'hash'};"
    "$s.Close();$s.Dispose();$s=$null;Start-Process $p -Wait;$ok=$true;break}"
    "catch{if($s){if($s.IsOpen){$s.Close()};$s.Dispose();$s=$null}}}"
    "if(-not $ok){Start-Sleep -Milliseconds 300}}"
    "until($ok -or (Get-Date)-gt$e);"
    "if(-not $ok){Write-Error 'Celeste device timed out'}}"
    "finally{if($s){if($s.IsOpen){$s.Close()};$s.Dispose()};"
    "if(Test-Path $d){Remove-Item -LiteralPath $d -Recurse -Force}};"
    "if($ok){exit}";

static bool windows_launch_key(uint16_t key) {
    if(!furi_hal_hid_kb_press(key)) return false;
    furi_delay_ms(HID_KEY_DELAY_MS);
    const bool released = furi_hal_hid_kb_release(key);
    furi_delay_ms(HID_KEY_DELAY_MS);
    return released;
}

static bool windows_launch_text(const char* text) {
    while(*text) {
        const uint16_t key = HID_ASCII_TO_KEY(*text++);
        if(key == HID_KEYBOARD_NONE || !windows_launch_key(key)) return false;
    }
    return true;
}

bool windows_launch(void) {
    /* Replacing another app's locked USB interface would break its ownership. */
    if(furi_hal_usb_is_locked()) return false;

    FuriHalUsbInterface* previous = furi_hal_usb_get_config();
    if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
        furi_hal_usb_set_config(previous, NULL);
        return false;
    }

    const uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(HID_CONNECT_TIMEOUT_MS);
    while(!furi_hal_hid_is_connected()) {
        if((int32_t)(furi_get_tick() - deadline) >= 0) goto fail;
        furi_delay_ms(25U);
    }

    furi_delay_ms(250U);
    if(!windows_launch_key(KEY_MOD_LEFT_GUI | HID_KEYBOARD_R)) goto fail;
    furi_delay_ms(300U);
    if(!windows_launch_text("powershell -NoProfile")) goto fail;
    if(!windows_launch_key(HID_KEYBOARD_RETURN)) goto fail;
    furi_delay_ms(1200U);
    if(!windows_launch_text(windows_bootstrap)) goto fail;
    if(!windows_launch_key(HID_KEYBOARD_RETURN)) goto fail;
    furi_hal_hid_kb_release_all();
    return furi_hal_usb_set_config(previous, NULL);

fail:
    furi_hal_hid_kb_release_all();
    furi_hal_usb_set_config(previous, NULL);
    return false;
}
