# Pet Collar Physiological Monitoring Project

This repository contains the main datasets, hardware records, algorithm-development stages, embedded implementation, validation material and PC host application for the pet-collar physiological monitoring project.

The project investigates whether neck-mounted inertial sensing can support non-invasive estimation of canine heart rate (HR) and respiration rate (RR), and how the resulting processing pipeline can be transferred to a resource-constrained STM32WB55 wearable platform.

## Repository structure

```text
.
├── Dataset/
├── hardware/
├── 01_Human_IMU_Feasibility_MCU_Prototypes.zip
├── 02_Human_Causal_Baseline_ReleaseAware.zip
├── 03_Canine_Offline_Physiological_Reference.zip
├── 04_LRMAP_FullC_Runtime_Core.zip
├── 05_LRMAP_STM32WB55_Integrated_Monitor.zip
├── 06_LRMAP_MCU_Final_Validation.zip
└── PetCollar_PC_BLE_Receiver.zip
```

## Contents

### `Dataset/`

Supporting datasets used throughout the study.

The data are separated according to their role in the research workflow. Different datasets are used for feasibility work, offline physiological-reference construction, LRMAP development and final validation. The README supplied with each dataset should be used to determine its intended role.

### `hardware/`

Hardware design and final wearable-integration material.

This folder contains the development and wearable PCB records, component/BOM information, assembly notes, PCB layouts and the final 3D enclosure files. It documents the progression from the initial development board to the compact STM32WB55-based collar prototype.

### `01_Human_IMU_Feasibility_MCU_Prototypes.zip`

Early MCU feasibility work using human IMU recordings.

This stage was used to investigate whether physiological information could be extracted from inertial signals and whether the required processing could be implemented within the constraints of the target embedded platform. It represents an early prototype stage rather than the final canine estimator.

### `02_Human_Causal_Baseline_ReleaseAware.zip`

Human-data causal baseline and online-output development.

This package records the transition from offline signal analysis toward a causal processing pipeline suitable for real-time operation, including the logic governing when an estimate is considered ready for release. It provided a baseline before the canine-specific LRMAP pipeline was finalised.

### `03_Canine_Offline_Physiological_Reference.zip`

Canine data prepared for **offline physiological-reference estimation**.

The recordings in this package have passed the first low-motion selection stage and are used to construct evidence-supported offline HR/RR references. This package is not the dataset used to train or validate LRMAP; a further screened dataset is used for that later stage.

### `04_LRMAP_Estimator_Runtime_Core.zip`

Platform-independent implementation of the LRMAP runtime core.

This package represents the algorithmic stage between offline development and complete MCU integration. It is used to verify that the final processing logic can operate in C with embedded-compatible memory and computation requirements before being incorporated into the full STM32 project.

### `05_LRMAP_STM32WB55_Integrated_Monitor.zip`

Final integrated STM32WB55 wearable-monitor firmware.

This package combines the LRMAP physiological estimator with the complete embedded monitoring system, including sensor acquisition, motion-state handling, HR/RR processing, confidence reporting, temperature and battery monitoring, and BLE communication.

This is the main firmware implementation used by the final wearable prototype.

### `06_LRMAP_MCU_Final_Validation.zip`

Final MCU validation material.

This package contains the material used to verify the behaviour and outputs of the embedded implementation against the corresponding offline/reference processing. It represents the final validation stage after LRMAP had been integrated into the STM32WB55 system.

### `PetCollar_PC_BLE_Receiver.zip`

Final Windows host application for the wearable monitor.

The application receives BLE data from the collar and displays the reported HR, RR, confidence, temperature, battery and operating state. It also provides the final HR and RR time-series visualisation.

The archive contains the final runnable Windows application and its packaged runtime dependencies. The host-application source and build environment are not included.

## Research workflow

At a high level, the repository follows the development path:

**Human feasibility → causal baseline → canine offline reference construction → LRMAP C implementation → STM32WB55 integration → final MCU validation → BLE host display**

The numbered archives therefore represent major development and verification stages rather than independent software products.

## Notes

- Detailed technical information is provided in the README files inside the corresponding folders or archives.
- The numbered packages are retained to make the development sequence clear.
- The final embedded system is represented primarily by `05_LRMAP_STM32WB55_Integrated_Monitor.zip`, with `06_LRMAP_MCU_Final_Validation.zip` providing its final verification material.
- This repository is intended as a research and prototype record rather than a medical diagnostic product.
