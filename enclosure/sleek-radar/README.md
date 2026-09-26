# Sleek Radar 3D enclosure

This directory contains the final **CAD export** for the compact desk enclosure, sized for the project's ESP32-S3 N16R8 board and 1.28-inch GC9A01 display. The shape is shown in [front](Final_front.png) and [rear](Final_back.png) renders.

| File | Use |
| --- | --- |
| [A1 mini PLA plate](PRINT_THIS_BODY_AND_LED_LID_A1mini_PLA.3mf) | Prepared body and LED-lid print arrangement; the display cap is separate |
| [Body STL](01_POLISHED_BODY.stl) | Individual main-body mesh |
| [LED lid STL](LED_LID_PRINT_THIS.stl) | Individual lid mesh |
| [Display cap STL](CAP_ASSEMBLED.stl) | Separate display-cap mesh, absent from the 3MF plate |
| [STEP assembly](Sleek_Radar_Final.step) | Editable CAD geometry for adaptation and fit inspection |
| [Verification record](Final_verification.json) | Export, clearance and mesh checks |

The record reports valid single solids, watertight meshes and checked clearances for the body and LED lid. Its `physical_fit_and_LED_transmission_tested` and `slicer_preview_verified` fields are **false**. Treat the 3MF as a prepared starting plate and add the display cap STL separately; inspect all parts in your slicer, confirm orientation and settings, then test the display, buttons, USB plug and LED transmission against your specific parts. The nominal outer dimensions are **50 × 98 × 63.8 mm**.

The board's BOOT/MODE button is on GPIO0. Check that the printed button does not stay pressed when the power cable is inserted or the board resets.
