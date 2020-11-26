# Mitsubishi Outlander PHEV remote app ESP32 TTGO firmware 

https://banggood.app.link/AtArm9Qal1

Uses SIM800 GPRS on the board to send car information to an MQTT broker

Download the ESP IDF framework and toolchain 

https://github.com/espressif/esp-idf

Follow the hello world example to ensure everthing is up and running.

To checkout the firmware 
```
git clone --recursive https://github.com/phev-remote/phev-ttgo

cd phev-ttgo

idf.py menuconfig
```
Configure the PPP config and WiFi connection for your car.  Also you can use a different MQTT broker if you don't want to use a public one.
```
idf.py -p <COM> flash monitor
```
An ID is created based on the ESP MAC address which is logged at start up.

The device will try and register itself, so put the car into registration mode and make sure the device is in WiFi range (you should see in the logs if it connects ok).

Once registered it will start to broadcast to the MQTT topic 
```
/ttgo/devices/<mac address>/events
```  
You can then send updates to 
```
/ttgo/devices/<mac address>/commands
```
topic in the following JSON format.
  
 ```
 {
        "requests" : [
          {
            "updateRegister" : {
              "register" : 2,
              "value" : [0,0,255,255,255,255,1,255,255,255,255,255,255,255,255]
            }
          },
          {  
            "operation" : { 
              "airCon" : "on"
            } 
          }
        ]
      }
```
The command above sets the aircon to ten minute cool.

Happy PHEV Hacking!


# Docker build instructions
```
docker build <docker_username>/phev-ttgo .

docker run -v ~/phev-ttgo:/workspace phev-ttgo "idf.py menuconfig"

docker run -v ~/phev-ttgo:/workspace phev-ttgo "idf.py build"
```
