# ESP32 Water Quality Helper PCB — Rev A

## Mechanical
- Outline: 63 x 77 mm
- 4 x M3 mounting holes, 3.2 mm drill, hole centers 3.5 mm from each edge
- ESP32 interface: 2 x 15 pins, 2.54 mm pitch, 22.86 mm row-to-row center spacing

## Electrical
- ADC1: GPIO36, GPIO39, GPIO34, GPIO35, GPIO32, GPIO33
- Each ADC: 1 kOhm series + 100 nF to GND, 0–3.3 V maximum at ESP32 pin
- Digital input: GPIO13 with 10 kOhm pull-up to 3V3
- Relay module signals: GPIO4,5,18,19,21,22,23,25,26,27
- Relay outputs are logic-level module connections; do NOT connect bare relay coils directly to ESP32 GPIOs
- Main supply: 5 V input, 100 uF bulk capacitor
- 5 V branches: SENSOR and RELAY; keep switching-current routing away from ADC return paths
- 3.3 V is taken from the ESP32 DevKit 3V3 pin; do not parallel a second regulator onto that rail
- Calibration test point: VREF_CAL + GND, nominal 2.5 V precision reference can be connected during calibration

## Important ESP32 notes
- GPIO34/35/36/39 are input-only and are therefore used only for ADC inputs.
- GPIO5 is a strapping pin; the attached relay module must not force it to an incorrect level during reset/boot.
- GPIO6–11 are reserved for flash and are not used.
- The board should be powered through the ESP32 DevKit 5V/VIN input only; do not simultaneously feed USB and an external 5V supply during normal operation.

## Status
This is an engineering Rev A placement/net-definition package, not yet a manufacturing release. Before fabrication, verify the exact ESP32 board body/USB connector clearance and the exact terminal-block part numbers. Then route, run ERC/DRC, add copper pours, and generate Gerbers/drill files.
