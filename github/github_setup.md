# GitHub Setup Notes

## Suggested repository name

| Option | Notes |
|---|---|
| `esp32-plane-radar-desk-display` | Clear and descriptive |
| `plane-radar-pro-esp32` | Shorter |
| `muc-plane-radar-desk-display` | Munich/Freising-specific flavor |

## Initial local setup

```bash
git init
git add .
git commit -m "Initial ESP32-C3 plane radar desk display project export"
```

## Optional GitHub remote

```bash
git branch -M main
git remote add origin git@github.com:<your-user>/<repo-name>.git
git push -u origin main
```

## Suggested GitHub issues

| Issue | Description |
|---|---|
| `Bring up WS2812B LED ring demo` | Compile and flash `PlaneRadar_LEDStatus_Demo.ino` |
| `Verify LED orientation` | Confirm LED 0 is top/North or set offset |
| `Bring up GC9A01 display` | Test display with pin map |
| `Create page manager` | Add Radar/Closest/MUC/Stats/System pages |
| `Integrate ADS-B API state` | Fill `LedContext` from aircraft data |
| `Add MODE button handling` | Short press page, double press range, long press quiet/theme |
| `Design enclosure v1` | White upright USB-C-powered enclosure |

## Suggested `.gitignore`

```gitignore
.pio/
.vscode/
build/
*.bin
*.elf
*.map
.DS_Store
```
