# USB Storage Mode

USB Storage mode lets you access the CrossPoint Reader's SD card directly from your computer over USB-C, without physically removing the card.

## How to Use

1. Plug the device into your computer via USB-C.
2. On the device, open the **File Transfer** menu from the home screen.
3. Select **USB Storage** from the list.
4. The device displays a holding screen: *"SD card connected to computer."*
5. On your computer, the SD card should appear as a removable drive within a few seconds.
6. Copy, move, or delete files as needed.
7. **Safely eject** the drive on your computer before disconnecting.
8. Press **Back** on the device (or unplug the cable) to exit USB Storage mode.

The device automatically returns to the home screen once the connection ends.

## Technical Details

| Property | Value |
|---|---|
| USB class | Mass Storage Class (MSC), Bulk-Only Transport |
| Block size | 512 bytes |
| Coexistence | CDC serial (for flashing/logging) and MSC run simultaneously via TinyUSB composite mode |
| Block access | Raw SdFat card-layer reads/writes (`sd.card()->readBlocks` / `writeBlocks`) |

### SD Card Access Conflict

The firmware and the USB host **cannot both use the SD card simultaneously**. When USB Storage mode is active:

- The SdFat filesystem is unmounted (`Storage.end()`).
- All firmware SD card operations are suspended.
- The USB host receives exclusive raw block access.
- On exit, the filesystem is remounted (`Storage.begin()`).

Do **not** reset or power-cycle the device while the SD card is mounted on the host computer, as this may corrupt the filesystem.

## Caveats

- **Write safety**: Always eject the drive on your computer before pressing Back or unplugging. Ejecting flushes the host's write cache and prevents data loss.
- **Filesystem format**: The SD card uses a standard FAT32 filesystem and is compatible with all major operating systems (Windows, macOS, Linux).
- **Large files**: Copying large files (e.g., many EPUBs at once) may take some time; the e-ink display remains static during the transfer.
- **Auto-sleep**: The device will not go to sleep while USB Storage mode is active.
