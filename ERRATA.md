# Purpose
This document describes the hardware erratas and any potential fix using said hardware.

# E1091 Rev 1: Driver Board

## Op-Amp
The op-amp chosen cannot drive all 4 filaments of the IV-8 VFD tubes, and the initial 1k potentiometer value is too high

### Fix
- Use AD8532AR for the op-amp (U4)
- Use 3266Y-1-101LF for the pot RV2. You may have to bend the leads to get them to fit, but fit they will

The above changes are reflected in the BOM, but not the schematic PDF.
