# Celeste Classic for iPhone

Open `CelesteClassic.xcodeproj` in Xcode. The native UIKit app runs the same C game locally, with an uncompressed full-colour square display and a compact eight-direction touch pad and Jump/Dash controls underneath. The four diagonal buttons press both component directions; held controls combine independently. No Flipper, browser, network connection or theme picker is needed.

The app stores a validated room checkpoint and lifetime statistics locally using UserDefaults. Relaunching resumes the room entrance, not an exact mid-air position. Restart clears the current run while preserving lifetime playtime, successful dashes, strawberries, deaths, completed playthroughs, restarts and XP. XP is a local game statistic: one point per strawberry plus ten per completed playthrough; it does not award Flipper Dolphin XP. Active gameplay time excludes app backgrounding and paused menus.

Music is a synthesized monophonic arrangement and sound effects, not the original four-channel PICO-8 soundtrack. The audio session respects the iPhone silent switch. Touch buttons provide light haptics.

The app adopts the scene lifecycle required by current iOS. The strawberry icon comes from upstream ccleste and is inset by 8%. The signing team and bundle identifier currently target the owner's personal development account; adjust them for another device/account. Personal-team signing follows Apple's expiry limits.

Build example:

```sh
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcodebuild \
  -project ios/CelesteClassic.xcodeproj -scheme CelesteClassic \
  -configuration Debug -destination 'generic/platform=iOS' \
  -derivedDataPath /private/tmp/celeste-ios-build -allowProvisioningUpdates build
```
