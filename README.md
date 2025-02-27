# PVInfoDisplay
Shows the current status of a PV system on a "Cheap Yellow Display" ( ESP32-2432S028R)

The PV Status Display is an application which shows periodical information about the status of your PV-system provided 
by SolarEdge (https://www.solaredge.com)  

Running on a board board with ESP32 and 2.8-inch and TFT display with backlight and resistive touch-screen. 
Tested on several boards of type ESP32-2432S028R

  + The GUI of this project is based on https://RandomNerdTutorials.com/esp32-cyd-lvgl-display-bme280-data-table/ by Rui Santos & Sara Santos - Random Nerd Tutorials
  + The data is collected from the SolarEdge Monitoring platform using the "SolarEdge Monitoring Server API" https://knowledge-center.solaredge.com/sites/kc/files/se_monitoring_api.pdf. 
    Using this API 
    1)  Requires an API-key
    2)  Acceptance of the terms of license published by SolarEdge: https://monitoring.solaredge.com/solaredge-web/p/license

  + Three API calls are made at start-up
    1)  "dataPeriod.json?"            -     If the "endDate" is not equal the actual date, the the communication between your PV-system and the server is disrupted
    2)  "storageData.json?", to see if batteries are equipped and in case they are to get
        + The number of batteries     -     typically "1", as multiple batteries are assembled as one block
        + The rated capacity          -     the one you can find on the name plate of your device(s)
        +  The actual capacity        -     determined (measured) by the system; a certain degradation will appear throughout lifespan
                                            As "startTime" the last full hour before Power-on is taken,  "endTime" equals to "startTime" + 5 minutes.
    3)  "currentPowerFlow.json?", following information is taken:
          +  "unit"
          +  "Connections"[]
             +  "from" GRID     "to"  Load
             +  "from" PV       "to"  Load
             +  "from" PV       "to"  Storage
             +  "from" LOAD     "to"  Grid
             +  "from" STORAGE  "to"  Load
             + "from"      "to"
             + "from"      "to"
          +  "GRID"
             +  "status"
             +  "currentPower"
          + "LOAD"
            +  "status"
            +  "currentPower"
          +  "PV"
            +  "status"
            +  "currentPower"
          +  "STORAGE"
            +  "status"
            +  "currentPower"
            +  "chargeLevel"
            +  "critical"
              +  From the "chargeLevel" given in percents, the available energy of the storage is calculated in "unit" hours, typically kWh
              +  From the available energy, the current power and the status, the time to fully charged / completely discharged is calculated 
