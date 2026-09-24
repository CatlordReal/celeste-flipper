# Windows Defender verification

A user reported that Windows detected Wacatac in the released `Celeste-Windows.exe` while copying it to the Flipper's SD card. The full detection suffix and the user's Defender version are unavailable. The warning is not dismissed as a confirmed false positive.

## Exact-file scan

On 2026-09-24, [GitHub Actions run 36030073005](https://github.com/CatlordReal/celeste-flipper/actions/runs/36030073005) downloaded the published v0.1.1 ZIP, extracted the Windows helper, checked its SHA-256, updated Defender signatures, and scanned the file without executing it. The workflow succeeded; Defender returned **0**, with no recorded threat detection.

- File: `Celeste-Windows.exe`
- SHA-256: `c757023bc93a658b9aef98d1787f8525687a1068809b239147068f67eb662975`
- Engine: `1.1.26080.3`
- Security intelligence: `1.459.378.0`
- Intelligence timestamp: `2026-09-24T11:06:03Z`
- Scan environment: GitHub-hosted `windows-latest`, not the user's PC
- Command: `MpCmdRun.exe -Scan -ScanType 3 -File <exact helper> -DisableRemediation`

`-DisableRemediation` requests a report-only custom scan; it does not disable Defender protection. The workflow never executes the helper. The `defender-report` run artifact contains the transcript, hash, engine versions and result. The helper in v0.1.2 remains byte-identical to this scanned file.

The source audit found a loopback-only browser/serial bridge, an explicit permanent-game installation action, and its Start-menu shortcut creation. The embedded upstream game archive matches the official ccleste v1.4.0 archive (`73f07ddf101fb8274e3341f701bde62a83477b695cff42b172b48680995f1ec7`). This source review and single scan are limited evidence, not a proof of safety. The unsigned executable has no code-signing certificate or Microsoft analyst clearance.

## If Windows still blocks it

Keep Defender enabled and leave a blocked file quarantined. [Update security intelligence](https://support.microsoft.com/en-us/windows/security/threat-malware-protection/virus-and-threat-protection-in-the-windows-security-app), download a fresh copy, and scan it. If the warning persists, use the standalone Flipper FAP while the exact detection is reviewed through [Microsoft's submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission). No exclusions or protection bypasses are part of this project.

Go documents that [compiled Go programs can trigger incorrect detections](https://go.dev/doc/faq#virus), but that general observation does not establish the cause of this user's warning. No new binary has been generated merely to change a hash or evade detection.
