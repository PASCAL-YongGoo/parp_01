# USB NEXT Stack Migration Plan

## Goal
Make the project fully functional on Zephyr 4.3.99 USB Device Stack (NEXT) using only project code changes.

## Phase 1 - Core Bring-up (Done)
1. Confirm NEXT API usage from Zephyr samples/headers.
2. Replace legacy HID API calls with NEXT API equivalents.
3. Add USB device init helper to register descriptors/config/classes.
4. Update main init flow: HID init -> USB init -> enable -> router start.

## Phase 2 - Stability Improvements (Planned)
1. Make `hid_ready` atomic or guarded to avoid race conditions.
2. Add configurable key delay and optional Enter key behavior.
3. Clarify return contract for `-EAGAIN` from `usb_hid_send_epc()`.

## Phase 3 - Functionality Enhancements (Planned)
1. Add Shift modifier support for uppercase output.
2. Add frame reassembly for split E310 responses.
3. Optional: Support full A-Z range, not just hex.

## Test Plan
- USB enumeration: confirm VID/PID and interfaces on host.
- CDC ACM: verify `/dev/ttyACM*` appears and log output works.
- HID: confirm key events (EPC + Enter) in a text editor.
- Inventory mode: verify EPCs are routed through HID reliably.

## Risks
- USB VID/PID placeholders may cause collisions on host systems.
- HID timing (fixed 20ms delay) might be slow for high tag rates.
- Frame boundary assumptions in router can drop tags under heavy load.
