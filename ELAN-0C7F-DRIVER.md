# ELAN 04f3:0c7f Fingerprint Reader — libfprint Driver

This documents the work done to get the **ELAN Microelectronics 04f3:0c7f**
fingerprint sensor working end-to-end on Linux with GNOME.

---

## What was done

### 1. New libfprint driver (`libfprint/drivers/elan0c7f.c`)

A new image-based driver for the ELAN 0c7f sensor, following the swipe-style
capture model used by other ELAN drivers in libfprint. The driver handles USB bulk
transfers for frame capture, assembles partial frames into a composite image, and
submits it to libfprint's NBIS minutiae pipeline for enrollment and verification.

Notable: `FP_DEVICE_FEATURE_IDENTIFY` is explicitly disabled because the device's
firmware does not support 1:N identification — only 1:1 verification. Leaving it
enabled caused fprintd to crash during first enrollment.

### 2. Use-after-free fix on deactivation

`dev_deactivate` was freeing `frame_buf` and `best_img` synchronously even when an
SSM (state machine) with live USB transfers was still running. Incoming USB callbacks
would then write into freed memory, causing crashes during fprintd shutdown.

**Fix:** Added a `deactivating` flag. When set, USB callbacks (`frame_read_cb`,
`lift_frame_read_cb`) complete the SSM without submitting new work. Actual cleanup
is deferred to `capture_done`/`reinit_done`, which only run after all USB transfers
have completed.

### 3. NBIS improvements for narrow sensors

The ELAN 0c7f sensor produces 52x150 px images — much narrower than the sensors
NBIS was tuned for. Several fixes were needed to get reliable minutiae extraction:

- **fp-image.c**: tune NBIS parameters for images narrower than 80 px
  (`inv_block_margin`, `trans_dir_pix`, `side_half_contour`, `max_hook_len`)
- **nbis/mindtct/remove.c**: skip `remove_or_adjust_side_minutiae_V2` when
  `side_half_contour == 0` (short contours always fail on narrow images)
- **nbis/mindtct/remove.c**: fix a double-free in `remove_malformations` by
  resetting contour pointers at the start of each loop iteration
- **nbis/include/bozorth.h**: lower `MIN_COMPUTABLE_BOZORTH_MINUTIAE` from 10 to 5
- **fpi-print.c**: reject scans with fewer than 3 minutiae with a
  `FP_DEVICE_RETRY_CENTER_FINGER` error so the caller retries rather than recording
  a useless template

### 4. GNOME Settings fingerprint dialog — two bugs fixed

Both bugs only manifested with **0 enrolled fingerprints** (i.e., first-time setup
from GNOME Settings). They are bugs in gnome-control-center, not libfprint, but
were discovered while testing the driver. Reported upstream:
https://gitlab.gnome.org/GNOME/gnome-control-center/-/issues/3208

#### Bug A — UI race condition

`on_stack_child_changed` calls `update_prints_store` then `claim_device`.
`claim_device` sets the prints manager widget insensitive. `list_enrolled_cb` fires
almost immediately for the `NoEnrolledPrints` case but was gated on
`DIALOG_STATE_DEVICE_CLAIMED` before restoring sensitivity — a state not set until
the slower `claim_device_cb` fires. Result: the "Scan new fingerprint" button
accepted no clicks.

Fix: remove the `DIALOG_STATE_DEVICE_CLAIMED` guard in `list_enrolled_cb` so the
UI is always re-enabled after listing completes.

#### Bug B — Wayland popup height overflow

With 0 enrolled prints all 10 finger-selection buttons are shown in
`add_print_popover`. The popover's natural height (~410px) exceeds the
`AdwDialog`'s `content-height: 400`. On Wayland, `xdg_popup` surfaces cannot
exceed their parent surface bounds — the compositor immediately dismisses the
popup. This explains why clicking "Scan new fingerprint" appeared to do nothing:
the popup opened and closed within a single frame.

Fix: wrap `add_print_popover_box` in a `GtkScrolledWindow` with
`max-content-height: 300` and `propagate-natural-height: true`. The scrollbar
appears only when all 10 fingers are listed; with fewer fingers the popover
remains compact.

---

## Files changed

| File | Change |
|---|---|
| `libfprint/drivers/elan0c7f.c` | New driver + use-after-free fix |
| `libfprint/meson.build` | Register new driver |
| `meson.build` | Register new driver |
| `libfprint/fp-image.c` | NBIS parameter tuning for narrow sensors |
| `libfprint/fpi-print.c` | Minutiae rejection guard + debug logging |
| `libfprint/nbis/include/bozorth.h` | Lower MIN_COMPUTABLE_BOZORTH_MINUTIAE |
| `libfprint/nbis/mindtct/remove.c` | Double-free fix + skip side minutiae for narrow images |
| `gcc_src/panels/system/users/cc-fingerprint-dialog.c` | Fix race condition (Bug A) |
| `gcc_src/panels/system/users/cc-fingerprint-dialog.blp` | Fix Wayland popup overflow (Bug B) |

---

## Testing

Tested on Fedora 43, Wayland, with fprintd 1.94.5:

- `fprintd-enroll` — enrollment succeeds end-to-end
- GNOME Settings → Users → fingerprint enrollment — works from a clean state
  (0 enrolled prints) and with existing prints
- fprintd shutdown after enrollment — no crash
- PAM authentication via fingerprint — works

---

## Applying the driver patch

Add `elan0c7f.c` to `libfprint/drivers/` and register it in `libfprint/meson.build`.
The NBIS fixes in the other files are recommended for any narrow sensor but are
otherwise independent of the driver itself.
