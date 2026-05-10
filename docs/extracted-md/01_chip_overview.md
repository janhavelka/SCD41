# Chip Overview

SCD41 is a miniature CO2 sensor in Sensirion's SCD4x family. It uses photoacoustic NDIR/PASens technology, includes an on-chip SHT4x humidity and temperature sensor for signal compensation, and communicates over I2C. Source: datasheet, p. 1.

## Driver-Relevant Facts

| Area | SCD41 fact | Source |
|---|---|---|
| Output channels | CO2 concentration, temperature, and relative humidity. | Datasheet, pp. 1, 9 |
| CO2 output range | 0 to 40000 ppm output range. | Datasheet, p. 3 |
| SCD41 specified CO2 range | 400 to 5000 ppm. | Datasheet, p. 1 |
| SCD41 CO2 accuracy | 400-1000 ppm: +/-(50 ppm + 2.5% reading); 1001-2000 ppm: +/-(50 ppm + 3% reading); 2001-5000 ppm: +/-(40 ppm + 5% reading). | Datasheet, p. 3 |
| Repeatability | Typ. +/-10 ppm CO2. | Datasheet, p. 3 |
| Humidity output | 0 to 100 %RH; typ. +/-6 %RH over 15-35 degC and 20-65 %RH. | Datasheet, p. 3 |
| Temperature output | -10 to 60 degC; typ. +/-0.8 degC over 15-35 degC. | Datasheet, p. 3 |
| Package | 10.1 mm x 10.1 mm x 6.5 mm3 SMD package. | Datasheet, p. 1 |

## Measurement Modes

- Periodic measurement updates every 5 seconds. Source: datasheet, p. 9.
- Low-power periodic measurement updates approximately every 30 seconds. Source: datasheet, p. 14.
- SCD41 adds single-shot measurement mode for on-demand CO2/RH/T and RH/T-only measurements. Source: datasheet, pp. 17-18.
