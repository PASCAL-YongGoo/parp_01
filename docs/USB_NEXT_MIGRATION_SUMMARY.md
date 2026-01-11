# USB NEXT Stack Migration Summary

## Scope
- Target: Zephyr 4.3.99 USB Device Stack (NEXT)
- Constraint: Modify project code only (no Zephyr source changes)

## Key Changes
- Replaced legacy HID APIs with NEXT APIs.
- Added a small USB device init helper to encapsulate descriptors/config/class registration.
- Updated main initialization sequence to include USB stack bring-up and router start.

## API Migration
- Legacy (not available in NEXT):
  - `usbd_hid_set_ops()`
  - `hid_int_ep_write()`
- NEXT APIs used:
  - `hid_device_register()`
  - `hid_device_submit_report()`

## Files Added / Updated
- Added: `src/usb_device.c`
- Added: `src/usb_device.h`
- Updated: `src/usb_hid.c`
- Updated: `src/main.c`
- Updated: `CMakeLists.txt`

## Current Behavior
- HID device registers report descriptor and callbacks before USB init.
- USB stack registers all classes (CDC ACM + HID) and enables device support.
- EPC strings are sent as HID key press/release sequences with Enter appended.

## Known Gaps / Follow-ups
- `hid_ready` is a plain `bool` and can be made atomic.
- Shift modifier (uppercase) is not implemented yet.
- Enter key is always appended; consider making it configurable.
- Key delay is hard-coded to 20ms; consider a config macro.
- USB VID/PID are placeholders and should be replaced for production.

## Notes
- HID device is obtained via `DEVICE_DT_GET_ONE(zephyr_hid_device)`.
- USB stack initialization is centralized in `usb_device_init()`.
