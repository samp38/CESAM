# CESAM
Simple, connected and reliable indoor Door Opening Device.

participants
Porteur projet : Vincent Pour son fils Hugo

Gabriel  
Lucie  
Clotilde  
Koji  
Samuel
Antoine

## Features
* indoor door open and close action with motor
* brake that can be released to allow manual door operation
* full features BlueTooth LowEnergy app to control door(s)
* automatic breake release on door manual movement
* door speed and current monitoring to avoid motor overheating


## Firmware
### States & transitions
![stateMachine](doc/stateMachine.drawio.svg)

## Hardware
* motor with reducer and wheel
* arduino nano 33 ble
* H-Bridge (L293D or DRV8838 or anything else)
* Voltage regulator
