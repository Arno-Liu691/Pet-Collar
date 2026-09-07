# Pet Collar Hardware

This directory records the two PCB stages used in the pet-collar project and the final wearable-board assembly.

## Hardware revisions

### v1 — development PCB

The first PCB was a development-oriented implementation. It used the **STM32WB55CCU6** and retained a dedicated SWD header connected to an external **STLINK-V3MINIE**. The larger board area and direct debug access were intentional: this revision was used to establish the sensor, power, USB, BLE and firmware platform before the final embedded estimator and mechanical constraints were frozen.

### v2 — wearable PCB

The second PCB is the compact wearable implementation used for the final system. The board was reduced in area for collar attachment and the dedicated SWD header was removed. The final assembly uses an **STM32WB55CGU6** in the same VQFN-48 footprint as the earlier CCU6 device. Firmware is loaded by using the physical BOOT0 control and the USB connection with **STM32CubeProgrammer**.

> **Important assembly note:** the exported v2 schematic still displays `STM32WB55CCU6` at U14 because the schematic part text was not updated when the fitted device was changed. The physically assembled v2 board uses **STM32WB55CGU6**.

The development and wearable PCBs implement the same high-level sensing, power and BLE architecture; the second revision is a requirement-driven wearable redesign rather than a separate system concept.

## Final v2 system

| Function | Final component(s) | Role |
|---|---|---|
| Main processor / BLE | STM32WB55CGU6 | Local sensor acquisition, motion-state logic, HR/RR processing and BLE |
| Inertial sensing | LSM6DSOXTR | 3-axis accelerometer + 3-axis gyroscope |
| Temperature | TMP117AIDRVR | Local collar/contact temperature measurement |
| Battery state | MAX17048G+T10 | LiPo state-of-charge / voltage monitoring |
| Battery charging | BQ21040DBVT | Single-cell Li-ion/LiPo charger |
| Power-path control | LTC4412ES6#TRPBF + DMG2305UX-7 | USB/battery source-path control |
| Regulation | TLV75530PDBVR | 3.0 V regulated system rail |
| Load switching | TPS22917DBVR | Switched system supply |
| USB protection | USBLC6-2SC6, SMF5V0A-E3-08, MF-NSMF050-2 | USB data ESD, VBUS transient and over-current protection |
| USB connector | USB4110-GF-A | Charging and USB bootloader programming |
| RF matching/filtering | MLPF-WB-01E3 | STM32WB 2.4 GHz matching / harmonic filtering |
| Antenna | 2450AT18A100E | 2.4 GHz chip antenna |
| HSE clock | NDK NX2016SA-32M-EXS00A-CS06465 | 32 MHz high-speed crystal |
| LSE clock | Abracon ABS07-32.768KHZ-6-T | 32.768 kHz low-speed crystal |
| Status indication | Kingbright APFA2507R9G2C | Red/green status LED |
| Main switch | MSK12C02 | Physical power switch |
| User / boot controls | TS-1088-AR02016 (SW3/SW4) | Physical user / reset-boot functions |
| Battery connector | Molex 53261-0371 | 3-wire battery connection |

### Battery

Final prototype battery:

- Part: **LP-573442-1S-3**
- Nominal voltage: **3.7 V**
- Capacity: **800 mAh**
- Connection: battery positive, ground and thermistor
- Purchase reference: https://cpc.farnell.com/unbranded/lp-573442-1s-3/800mah-37v-lipo-rechargeable-battery/dp/BT06808

The enclosure is designed around the final v2 PCB and this battery geometry; the PCB was not resized to fit a pre-existing enclosure.

## Interfaces

### IMU

The LSM6DSOX is connected to the STM32WB55 through SPI with a hardware interrupt line for sample/event servicing. Exact runtime configuration is defined by the final firmware rather than by this hardware README.

### I2C auxiliary devices

The TMP117 and MAX17048 share the low-rate I2C control bus. These measurements are auxiliary system channels and are not inputs to the HR/RR estimator.

### BLE / RF

The STM32WB55CGU6 provides the 2.4 GHz radio. The RF output is routed through the ST **MLPF-WB-01E3** matching/filter device and then to the **2450AT18A100E** PCB antenna.

### USB programming

The wearable PCB does not use the dedicated SWD header retained on v1. For v2, the MCU is placed into the system bootloader using the physical BOOT0 control and is programmed over USB with STM32CubeProgrammer. The project firmware image is supplied as an ELF file.

## Final-assembly corrections relative to early design records

These corrections are authoritative for the final v2 assembly and should be read before using the exported schematic or early procurement BOM.

| Reference | Final fitted value / part | Earlier record |
|---|---|---|
| U14 | STM32WB55CGU6 | v2 schematic text still shows STM32WB55CCU6 |
| FL1 | MLPF-WB-01E3 | schematic/BOM text may show MLPF-WB55-02E3 |
| Q1 | DMG2305UX-7 | AO3407A |
| LED5 | APFA2507R9G2C | APFA2507B2Y2C |
| R20 | 1.2 kΩ | 1.8 kΩ |
| R35 | 10 kΩ | approximately 5 kΩ |

The exact orderable MPN for the 2.2 µH inductor L1 was not recorded in BOM2 and is therefore deliberately not invented in the final BOM.

## Repository files

```text
hardware/
├── README.md
├── v1_development_pcb/
│   ├── v1_Schematic.pdf
│   └── v1_layout.pdf
├── v2_wearable_pcb/
│   ├── v2_Schematic.pdf
│   ├── v2_layout.pdf
│   ├── BOM_v2_final.xlsx
│   ├── BOM_v2_final.csv
│   └── BOM_v2_assembly_notes.csv
└── photos/
    └── pcb_revisions.jpg
```

For the final reproducibility archive, the native PCB design files, manufacturing Gerbers/drill files, placement file and final PCB STEP model should be stored alongside these exports when they are frozen.

## Report correspondence

The report uses the terms **development PCB** for v1 and **wearable PCB** for v2.

- Section 3.1: overall final hardware architecture.
- Section 3.2: component-selection rationale.
- Section 3.3: circuit implementation, PCB layout and the development-to-wearable redesign.
- Section 3.4: enclosure and collar integration.

The final report should treat the v2 assembly notes above as authoritative where an earlier schematic label or procurement entry conflicts with the physically fitted hardware.
