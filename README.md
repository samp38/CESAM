<p align="center"><img src="doc/logoCESAM.png" height="300"></p>
Simple, connected and reliable indoor Door Opening Device.

## Features
* indoor door open and close action with motor
* brake that can be released to allow manual door operation
* full features BlueTooth LowEnergy app to control door(s)
* automatic breake release on door manual movement
* door speed and current monitoring to avoid motor overheating


## Firmware
### States & transitions

```
┌─────────────┐
│   Startup   │
└──────┬──────┘
       │ Init IMU, BLE, Storage
       ▼
┌─────────────┐
│   Unknown   │
└──────┬──────┘
       │
       │              ┌─────────────┐
       ├─────────────►│   Opening   │
       │ BLE: '0'     └──────┬──────┘
       │                     │ Timeout (1s no movement)
       │                     ▼
       │              ┌─────────────┐
       │              │    Open     │
       │              └──────┬──────┘
       │                     │ BLE: '1'
       │                     ▼
       │              ┌─────────────┐
       └─────────────►│   Closing   │
         BLE: '1'     └──────┬──────┘
                             │ Timeout (1s no movement)
                             ▼
                      ┌─────────────┐
                      │   Closed    │
                      └─────────────┘

BLE: '3' from Opening or Closing ────► Paused
```

`Unknown`, `Open`, `Closed` and `Paused` are the door at rest, and say why it stopped.
See [firmware/README.md](firmware/README.md) for the details, the BLE characteristics
and the end-of-travel detection.

## Hardware
* motor with reducer and wheel [example here](https://www.amazon.fr/Gebildet-DC3V-6V-motrices-robotique-Plastique/dp/B08D39MFN1/ref=asc_df_B08D39MFN1/?tag=googshopfr-21&linkCode=df0&hvadid=454935615577&hvpos=&hvnetw=g&hvrand=12643294058659340367&hvpone=&hvptwo=&hvqmt=&hvdev=c&hvdvcmdl=&hvlocint=&hvlocphy=9055351&hvtargid=pla-937905506568&psc=1&mcid=ba72aca812863cf9a3a4d8b4893a39b7)
* arduino nano 33 ble
* H-Bridge (L293D or DRV8838 or anything else)
* Voltage regulator

![schema-elec](doc/schema-elec.png)
