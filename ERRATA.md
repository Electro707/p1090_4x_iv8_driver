# Purpose
This document describes the hardware erratas and any potential fix using said hardware.

# E1091 Rev 1: Driver Board

## Op-Amp
The op-amp chosen cannot drive all 4 filaments of the IV-8 VFD tubes, and the initial 1k potentiometer value is too high

### Fix
- Use AD8532AR for the op-amp (U4)
- Use 3266Y-1-101LF for the pot RV2. You may have to bend the leads to get them to fit, but fit they will

The above changes are reflected in the BOM, but not the schematic PDF.

## High Pitch Boost Converter Noise
The boost converter generates audible-frequency high-pitch noise, which is unpleasant to have around.

### Cause Theory
- My guess is the boost converter goes in discontinuous output due to the low current draw (I think ~7mA if the datasheet for the IN-8 tubes are correct)
    - For now I replaced the inductor with a 47uH and added a 2.2kOhm load resistor in order to have the converter not go in discontinuous mode. Still ringing
- The capacitors are singing, downconverting any higher-frequency oscillation to baseband audio. Need to try out replacing the capacitors with non-mlcc if possible

### Fix
Not available, WIP
