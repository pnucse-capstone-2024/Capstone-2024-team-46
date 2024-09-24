# AnomalySimulations
AnomalySimulations for Capstone course

## Model
* 1D CNN based U-Net
* Transfomer model


## Data

The data located in `data` folder.

#### sensor_data_b

sampling rate : 70Hz

```
vibration data          : Dataset contains a ndarray of dimension (acc_val, n_channels, anomaly_type). (n_channels : acceleration axis: 0: X-axis, 1: Y-axis, 2: Z-axis) (anomaly_type : 0: normal, 1: foreign object jam, 2: low power)
```
