# TODO

## Firmware
- [ ] Flash updated firmware (double-tap reset to enter DFU mode, then re-run `arduino-cli upload`)

## Hardware
- [ ] Replace battery with higher capacity unit
- [ ] Add transistor to NTC voltage divider circuit to cut it during sleep (saves ~30µA)

## Testing
- [ ] Verify sleep current draw after firmware flash (~35µA expected vs 50mA before)
