//
//  The PV Status Display is an application which gives periodical information about the status of your PV-system provided by SolarEdge  
//  Running on a board board with ESP32 and 2.8-inch and TFT display with backlight and resistive touch-screen
//  Tested on several boards of type ESP32-2432S028R
//
//  The GUI of this project is based on "LVGL_Table"
//  The data is collected from the SolarEdge Monitoring platform using the "SolarEdge Monitoring Server API"
//  Using this API 
//  1)  Requires an API-key
//  2)  Acceptance of the terms of license published by SolarEdge: https://monitoring.solaredge.com/solaredge-web/p/license
//  
//  Three API calls are made at start-up
//  1)  "dataPeriod.json?"            -     If the "endDate" is not equal the actual date, the the communication between your PV-system and the server is disrupted 
//  2)  "storageData.json?", to see if batteries are equipped and in case they are to get
//      + The number of batteries     -     typically "1", as multiple batteries are assembled as one block
//      + The rated capacity          -     the one you can find on the name plate of your device(s)
//      + The actual capacity         -     determined (measured) by the system; a certain degradation will appear throughout lifespan
//      As "startTime" the last full hour before Power-on is taken,  "endTime" equals to "startTime" + 5 minutes.
//  3)  "currentPowerFlow.json?", following information is taken:
//        + "unit"
//        + "Connections"[]
//        + "from" GRID     "to"  Load
//        + "from" PV       "to"  Load
//        + "from" PV       "to"  Storage
//        + "from" LOAD     "to"  Grid
//        + "from" STORAGE  "to"  Load
//        + "from"      "to"
//        + "from"      "to"
//      + "GRID"
//        + "status"
//        + "currentPower"
//        The "currentPower" is followed by the direction of the power flow ("Import" | "Export")
//      + "LOAD"
//        + "status"
//        + "currentPower"
//      + "PV"
//        + "status"
//        + "currentPower"
//      + "STORAGE"
//        + "status"
//        + "currentPower"
//        + "chargeLevel"
//        + "critical"
//        From the "chargeLevel" given in percents, the available energy of the storage is calculated in "unit" hours, typically kWh 
//        From the available energy, the current power and the status, the time to fully charged / completely discharged is calculated; format is <days>d<hh>:<mm>   


/*  Rui Santos & Sara Santos - Random Nerd Tutorials - https://RandomNerdTutorials.com/esp32-cyd-lvgl-display-bme280-data-table/   |   https://RandomNerdTutorials.com/esp32-tft-lvgl-display-bme280-data-table/
    THIS EXAMPLE WAS TESTED WITH THE FOLLOWING HARDWARE:
    1) ESP32-2432S028R 2.8 inch 240×320 also known as the Cheap Yellow Display (CYD): https://makeradvisor.com/tools/cyd-cheap-yellow-display-esp32-2432s028r/
      SET UP INSTRUCTIONS: https://RandomNerdTutorials.com/cyd-lvgl/
    2) REGULAR ESP32 Dev Board + 2.8 inch 240x320 TFT Display: https://makeradvisor.com/tools/2-8-inch-ili9341-tft-240x320/ and https://makeradvisor.com/tools/esp32-dev-board-wi-fi-bluetooth/
      SET UP INSTRUCTIONS: https://RandomNerdTutorials.com/esp32-tft-lvgl/
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

/*  Install the "lvgl" library version 9.X by kisvegabor to interface with the TFT Display - https://lvgl.io/
    *** IMPORTANT: lv_conf.h available on the internet will probably NOT work with the examples available at Random Nerd Tutorials ***
    *** YOU MUST USE THE lv_conf.h FILE PROVIDED IN THE LINK BELOW IN ORDER TO USE THE EXAMPLES FROM RANDOM NERD TUTORIALS ***
    FULL INSTRUCTIONS AVAILABLE ON HOW CONFIGURE THE LIBRARY: https://RandomNerdTutorials.com/cyd-lvgl/ or https://RandomNerdTutorials.com/esp32-tft-lvgl/   */
#include <lvgl.h>

/*  Install the "TFT_eSPI" library by Bodmer to interface with the TFT Display - https://github.com/Bodmer/TFT_eSPI
    *** IMPORTANT: User_Setup.h available on the internet will probably NOT work with the examples available at Random Nerd Tutorials ***
    *** YOU MUST USE THE User_Setup.h FILE PROVIDED IN THE LINK BELOW IN ORDER TO USE THE EXAMPLES FROM RANDOM NERD TUTORIALS ***
    FULL INSTRUCTIONS AVAILABLE ON HOW CONFIGURE THE LIBRARY: https://RandomNerdTutorials.com/cyd-lvgl/ or https://RandomNerdTutorials.com/esp32-tft-lvgl/   */
#include <TFT_eSPI.h>

// Install the "XPT2046_Touchscreen" library by Paul Stoffregen to use the Touchscreen - https://github.com/PaulStoffregen/XPT2046_Touchscreen - Note: this library doesn't require further configuration
#include <XPT2046_Touchscreen.h>


#include <ezTime.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <WiFiManager.h>
#include <Preferences.h>

// "test" suppresses the periodical API-calls
// Useful when developing/testing other features
// as number of API-calls per day is limited
bool test = false;

// CYD Pins on connectors
// Connector P3
#define GPIO35  35        // No pull-up                 (blue)
#define GPIO22  22        // Pull-up, used on CN1 too   (yellow)
#define GPIO21  21        // Controls backlight         (red)

// Connector CN1, shall be used for
#define GPIO22  22        // Pull-up, used on P3 too    (blue)
#define GPIO27  27        // Pull-up                    (yellow)

//The pin actually used in this project
#define PIN     GPIO22

// Touchscreen pins
#define XPT2046_IRQ 36    // T_IRQ
#define XPT2046_MOSI 32   // T_DIN
#define XPT2046_MISO 39   // T_OUT
#define XPT2046_CLK 25    // T_CLK
#define XPT2046_CS 33     // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH  TFT_WIDTH
#define SCREEN_HEIGHT TFT_HEIGHT

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];
bool longTouch = false;
time_t touchBegin; 
time_t touchEnd; 

static lv_obj_t * table;

// My Public declarations
WiFiManager wm;
Timezone my_TZ;

const JsonDocument doc;
lv_obj_t * arc;

// Connections
const char * dirFrom;
const char * dirTo;

String version = "2.2.15";

bool goloop = true;
bool apiCallFailed = false;
const char * measureUnit;
// Grid values
const char * gridState;
double gridPower;
bool toGrid;

// Load values
const char * loadState;
double loadPower;

// PV values
const char * pvState;
double pvPower;

// Battery values
const char * battState;
double battPower;
double battCapacity;     // Degraded from 9.2
double battChargeLevel;
bool battCritical;
int noOfBatteries;
bool batteryProblems = false;

char batt[50]={0};
char grid[50]={0};
char load[50]={0};
char pv[50]={0};
char battlabel[15]={0};
char startEndDate[60] = {0};

String siteID;
String apiKey;

String address = "https://monitoringapi.solaredge.com//site//";

String siteIdAndApiKey;

struct messages
{
  char * message_en;
  char * message_de;
};

messages myMsgs[] =
{
  {"Idle",     "Leerlauf"},
  {"Active",   "Aktiv"},
  {"Disabled", "Passiv"},
  {"Charging", "Laden"},
  {"Discharging", "Entladen"},
  {"Inactive", "Inaktiv"}
};

int formerMinute;

//
// Special flags to implement the state-machine 
//
bool updated;               // needed to send exactly one call only when minute switches to "5"
bool bl_on = false;         // 



// If logging is enabled, it will inform the user about what is happening in the library
void log_print(lv_log_level_t level, const char * buf) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}

// Translates the messages from the API from english to german
// Returns the original message if no translation is defined
const char * f_translateStatus (const char * state)
{
  int i;
  for (i=0 ; i<5 ; i++)
  {
    if (strcmp(state, myMsgs[i].message_en) == 0)
    {
      return myMsgs[i].message_de;
    }
  }
  return state;
}

// TFT backlight handling
// Function names should be self-explaining
void switchBacklightOn()
{
  digitalWrite(TFT_BL, HIGH);
  bl_on = true;
}

void switchBacklightOff()
{
  digitalWrite(TFT_BL, LOW);
  bl_on = false;
}


void toggleBacklight()
{
  if (bl_on == false)
  {
    switchBacklightOn();
  }
  else
  {
    switchBacklightOff();
  }
}

// Get the Touchscreen state, the whole screen is used as a single button only
// A short touch toggles the backlight
// A long touch > 5 seconds deletes the network ssid and password
// Site ID and API key are kept
void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  //Checks if Touchscreen was touched
  if(touchscreen.tirqTouched() && touchscreen.touched())
  {
    if (!longTouch)
    {
      longTouch = true;
      touchBegin = my_TZ.now();
      Serial.println(touchBegin);
      //toggleBacklight();
      //Basic Touchscreen calibration points with map function to the correct width and height
      TS_Point p = touchscreen.getPoint();
      int x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
      int y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);

      char points[50];
      sprintf (points, "x: %d, y: %d", x, y);
      Serial.println (points);
    }
    Serial.println(longTouch);
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else 
  {
    data->state = LV_INDEV_STATE_RELEASED;
    if (longTouch)
    {
      longTouch = false;
      touchEnd = my_TZ.now();
      if ((touchEnd-touchBegin) >= 5 )
      {
        wm.resetSettings();
        ESP.restart();
      }
    }
  }
}

// Table update is called periodically in the main loop
static void update_table_values(void) 
{
  // Make an API-call every five minutes
  int act_minute = my_TZ.minute(TIME_NOW, LOCAL_TIME);
  if (formerMinute != act_minute)
  {
    updated = false;
    formerMinute = act_minute;
  }
  else
    updated = true;
    
  int i = act_minute % 5;

  // Every five minutes, an API call is made to fetch the recent data from the server
  switch (i)
  {
    case 0:
    {
      if (!updated)         // but only once!
      {
        Serial.println(my_TZ.dateTime().c_str());
        if (!test)
        {
          Serial.println("API call");
          apiCallFailed = SolarEdgeAPICall(2);
          if (!apiCallFailed)
            lv_arc_set_angles(arc, 0, 360); // Call was successful
        }
        else
        {
          Serial.println("NO API CALL");
          lv_arc_set_angles(arc, 90, 270);
        }
        updated = true;     // suppress further calls within same minute
      }
      else
      {
        updated = false;
      }
      break;
    }

    case 1:
    {
      if (!updated)         // but only once!
      {
        lv_arc_set_angles(arc, 90, 360);
        updated = true;     // suppress further calls within same minute        
        Serial.println("(1)");
        if (apiCallFailed) 
        {
          apiCallFailed = SolarEdgeAPICall(2);
          if (!apiCallFailed)
            lv_arc_set_angles(arc, 0, 360); // Call was successful
        }        
      } 
      else
      {
        updated = false;
      } 
      break;
    }

    case 2:
    {
      if (!updated)         // but only once!
      {
        lv_arc_set_angles(arc, 180, 360);
        updated = true;     // suppress further calls within same minute
        if (apiCallFailed) 
        {
          apiCallFailed = SolarEdgeAPICall(2);
          if (!apiCallFailed)
            lv_arc_set_angles(arc, 0, 360); // Call was successful
        }        
      } 
      else
      {
        updated = false;
      } 
      break;
    }

    case 3:
    {
      if (!updated)         // but only once!
      {
        lv_arc_set_angles(arc, 270, 360);
        updated = true;     // suppress further calls within same minute
        if (apiCallFailed) 
        {
          apiCallFailed = SolarEdgeAPICall(2);
          if (!apiCallFailed)
            lv_arc_set_angles(arc, 0, 360); // Call was successful
        }       
      } 
      else
      {
        updated = false;
      } 
      break;
    }

    case 4:
    {
      if (!updated)         // but only once!
      {
        lv_arc_set_angles(arc, 360, 360);
        updated = true;     // suppress further calls within same minute
        if (apiCallFailed) 
        {
          apiCallFailed = SolarEdgeAPICall(2);
          if (!apiCallFailed)
            lv_arc_set_angles(arc, 0, 360); // Call was successful
        }
      } 
      else
      {
        updated = false;
      } 
      break;
    }

    default:
    {
      break;
    }
  }
  // Fill the first column
  lv_table_set_cell_value(table, 0, 0, "");
  lv_table_set_cell_value(table, 2, 0, "PV"); 
  lv_table_set_cell_value(table, 1, 0, "Netz");
  lv_table_set_cell_value(table, 3, 0, "Haus");
  lv_table_set_cell_value(table, 4, 0, battlabel);   
  // String firstRow = wm.getWiFiSSID(true) + "\n" + my_TZ.dateTime();
  lv_table_set_cell_value(table, 0, 1, my_TZ.dateTime().c_str());
  lv_table_set_cell_value(table, 1, 1, grid);   
  lv_table_set_cell_value(table, 2, 1, pv);   
  lv_table_set_cell_value(table, 3, 1, load);   
  lv_table_set_cell_value(table, 4, 1, batt);   

}

static void draw_event_cb(lv_event_t * e) {
  lv_draw_task_t * draw_task = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t * base_dsc = (lv_draw_dsc_base_t*) draw_task->draw_dsc;
  // If the cells are drawn
  if(base_dsc->part == LV_PART_ITEMS) {
    uint32_t row = base_dsc->id1;
    uint32_t col = base_dsc->id2;

    // Align texts in the first row to left
     lv_draw_label_dsc_t * label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);

    if(row == 0)
    {
//      lv_draw_label_dsc_t * label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
//      if(label_draw_dsc)
//      {
//        label_draw_dsc->align = LV_TEXT_ALIGN_LEFT;
//      }
      lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
      if(fill_draw_dsc)
      {
        fill_draw_dsc->color = lv_color_mix(lv_palette_main(LV_PALETTE_BLUE), fill_draw_dsc->color, LV_OPA_50);
        fill_draw_dsc->opa = LV_OPA_COVER;
      }
    }
    else if(col == 0)
    {
//      lv_draw_label_dsc_t * label_draw_dsc = lv_draw_task_get_label_dsc(draw_task);
//      if(label_draw_dsc)
//      {
//        label_draw_dsc->align = LV_TEXT_ALIGN_LEFT;
//      }
    }
    if(label_draw_dsc) {
      label_draw_dsc->align = LV_TEXT_ALIGN_LEFT;
    }
    /*
    if (toGrid && row == 1)
    {
      lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
      fill_draw_dsc->color = lv_color_mix(lv_palette_main(LV_PALETTE_GREEN), fill_draw_dsc->color, LV_OPA_50);
      fill_draw_dsc->opa = LV_OPA_COVER;
    }
    else
    {
      lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
      fill_draw_dsc->color = lv_color_mix(lv_palette_main(LV_PALETTE_BLUE), fill_draw_dsc->color, LV_OPA_50);
      fill_draw_dsc->opa = LV_OPA_COVER;
    }
    */
    // Make every 2nd row gray color
    if((row != 0 && row % 2) == 0) {
      lv_draw_fill_dsc_t * fill_draw_dsc = lv_draw_task_get_fill_dsc(draw_task);
      if(fill_draw_dsc)
      {
        // Battery problems are indicated by setting the background of the last row to a light yellow
        if (batteryProblems && row == 4)
        {
          fill_draw_dsc->color = lv_color_mix(lv_palette_main(LV_PALETTE_YELLOW), fill_draw_dsc->color, LV_OPA_50);
          fill_draw_dsc->opa = LV_OPA_COVER;
        }
        else
        {
          fill_draw_dsc->color = lv_color_mix(lv_palette_main(LV_PALETTE_GREY), fill_draw_dsc->color, LV_OPA_10);
          fill_draw_dsc->opa = LV_OPA_COVER;
        }
      }
    }
  }
}

void lv_create_main_gui(void) {
  table = lv_table_create(lv_screen_active());
  lv_table_set_row_cnt(table, 5);
  lv_table_set_col_cnt(table, 2);
  lv_table_set_col_width(table, 0, 70);
  lv_table_set_col_width(table, 1, 230);
  

  // Inserts or updates all table values
  update_table_values();

  // Table with five rows, w/o a scroll bar
  // A scroll bar appears automatically if the table exceeds the screen size
  // This may happen if the information in one row needs a third line
  lv_obj_set_size(table, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_center(table);
  // Add an event callback to apply some custom drawing
  lv_obj_add_event_cb(table, draw_event_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
  lv_obj_add_flag(table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

  // Arc to show status of API call
  // A full arc shows that the information on the screen is up-to-date i.e. not older than a minute 
  arc = lv_arc_create(table);                                   // Bind arc to table rather than to lv_screen_active()
                                                                // Advantage: even if a row is enhanced and thus the 
                                                                // first row gos up, the arc remains properly placed
  lv_obj_set_size(arc, 38, 38);                                 // Shall fit into the first row
  lv_obj_align(arc, LV_ALIGN_TOP_LEFT, 8, 2);
  lv_arc_set_rotation(arc, 270);                                // Starting point of the indicator (0°) shall be at the top
  lv_arc_set_bg_angles(arc, 0, 360);                            // Full circle
  lv_arc_set_angles(arc, 0, 0);                                 // No indicator shown
                                                                // Indicator span shall be handled during update of table only
  lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);  // Flat ends of the indicator
  lv_obj_set_style_arc_width(arc, 8, 0);                        
  lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);                 // The knob at the end of the arc is not needed and thus not displayed
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);               // The arc shall serve as a status display only, shall not be adjustable
}


bool DataPeriod()
{
  Serial.println("Entering DataPeriod()");
  HTTPClient http;
  String url = address + siteID + "//" + "dataPeriod.json?api_key=" + apiKey;
  
  bool problems = false;

  const char * startDate;
  const char * endDate;
  
  http.begin(url);
  int httpCode = http.GET(); // Make the GET request
  if (httpCode > 0) 
  {
    // Check for the response
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      //Parse the JSON 
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload.c_str());
      
      //-----------------------------------
      // Each call needs its own parser!
      //-----------------------------------
      if (!error)
      {
          my_TZ.setLocation("Europe/Berlin");
           startDate =  doc["dataPeriod"]["startDate"];
           endDate =  doc["dataPeriod"]["endDate"];
           if (strcmp(endDate, my_TZ.dateTime("Y-m-d").c_str()))        // This needs some refinement as at midnight wrong decision is taken
           {
              problems = true;
              sprintf (startEndDate, "Server date: %s\nToday's date: %s", endDate, my_TZ.dateTime("Y-m-d"));
              Serial.println(startEndDate);
           }
           else
           {
              sprintf (startEndDate, "Server date: %s\nToday's date: %s", endDate, my_TZ.dateTime("Y-m-d"));
              Serial.println(startEndDate);
              Serial.println("Communication seems to be ok");
           }
      }
      else
      {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
      }
    }
  }
  else
  {
    Serial.printf("GET request failed (%x), error: %s\n", httpCode, http.errorToString(httpCode).c_str());
  }
  Serial.println("Leaving DataPeriod()");
  return (problems);
}

void SolarEdgeDailyEnergy()
{
  Serial.println("Entering SolarEdgeDailyEnergy()");
  HTTPClient http;
  String url;

  String startTime = "&startTime=";
  String endTime   = "&endTime=";
  String api_key   = "&api_key=";

  bool problems = false;

  String timeParms;
  if (test)
  {
    timeParms = "timeUnit=DAY";
  }
  else
  {
    timeParms = "timeUnit=HOUR";
  }

  String meterParms = "&meters=Consumption,Production,SelfConsumption,FeedIn,Purchased";

  time_t toDay = now();
  time_t lastDay = toDay - 86400;
  String lastday = my_TZ.dateTime(lastDay, "Y-m-d%2000:00:00"); 
  String today = my_TZ.dateTime(toDay, "Y-m-d%2000:00:00");
  url = address + siteID + "//" + "energyDetails.json?" + timeParms + startTime + lastday + endTime + today + meterParms + api_key + apiKey + "\0";
  http.begin(url);
  int httpCode = http.GET(); // Make the GET request
  if (httpCode > 0) 
  {
    // Check for the response
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      Serial.println(payload);
    }
  }
  else
  {
    Serial.printf("GET request failed (%x), error: %s\n", httpCode, http.errorToString(httpCode).c_str());
  }
  Serial.println("Leaving SolarEdgeDailyEnergy()");
}

bool SolarEdgeStorageData()
{
  Serial.println("Entering SolarEdgeStorageData()");
  HTTPClient http;
  String url;

  String startTime = "&startTime=";
  String endTime   = "&endTime=";
  String api_key   = "&api_key=";

  bool problems = false;
  time_t request = now();
  url = address + siteID + "//" + "storageData.json?" + startTime + my_TZ.dateTime(request, "Y-m-d%20H:00:00") + endTime + my_TZ.dateTime(request, "Y-m-d%20H:05:00") + api_key + apiKey + "\0";
  http.begin(url);
  int httpCode = http.GET(); // Make the GET request
  if (httpCode > 0) 
  {
    // Check for the response
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      //Parse the JSON 
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload.c_str());
      
      //-----------------------------------
      // Each call needs its own parser!
      //-----------------------------------
      if (!error)
      {
        double pBatt = 0.0;
                        
        noOfBatteries = doc["storageData"]["batteryCount"];
        int i;
        for (i = 0; i < noOfBatteries; i++)
        {
           pBatt =  doc["storageData"]["batteries"][i]["telemetries"][0]["fullPackEnergyAvailable"];
           double ratedCap = doc["storageData"]["batteries"][i]["nameplate"];
           if (pBatt == 0.0)
           {
              pBatt = ratedCap;
              problems = true;
           } 
           battCapacity += pBatt;
        }
        battCapacity /= 1000;
        sprintf (battlabel, "Batt.%d\n[%3.2f]", noOfBatteries, battCapacity);
      }
      else
      {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
      }
    }
  }
  else
  {
    Serial.printf("GET request failed (%x), error: %s\n", httpCode, http.errorToString(httpCode).c_str());
  }
  Serial.println("Leaving SolarEdgeStorageData()");
  return (problems);
}

bool SolarEdgeAPICall(int call) //, String date, String time - needed if calls [0], [1] are implemented
{
  //------------------------------------------------------------
  // Following section is customized for use with SolarEdge only
  //------------------------------------------------------------
  Serial.println("Entering SolarEdgeAPICall()");
  HTTPClient http;
  String url = address + siteID + "//" + "currentPowerFlow.json?api_key=" + apiKey;
  if ((WiFi.status() == WL_CONNECTED))
  {
    http.begin(url);
    int httpCode = http.GET(); // Make the GET request
    http.setTimeout(5000);
    // Check for the response
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      
      //Parse the JSON 
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload.c_str());

      //-----------------------------------
      // Each call needs its own parser!
      //-----------------------------------
      if (!error)
      {
        // Connections
        int i = 0;
        toGrid = false;
        //char fromTo[100];
        JsonArray connections = doc["siteCurrentPowerFlow"]["connections"].as<JsonArray>();;
        
        for (JsonObject a : connections)
        {
          for (JsonPair kv : a)
          {
            if (!strcmp(kv.key().c_str(), "to") && !strcmp(kv.value().as<const char*>(), "Grid")) 
            {
              toGrid = true;
            }
            else
            {
              ;
            }
            Serial.print(kv.key().c_str());
            Serial.print(": ");
            Serial.println(kv.value().as<const char*>());
          }
          i++;
        }
        //sprintf (fromTo, "Connections = %d", i);
        //Serial.println(fromTo);

        // Values
        measureUnit = doc["siteCurrentPowerFlow"]["unit"];
        
        // Grid values
        gridState = doc["siteCurrentPowerFlow"]["GRID"]["status"];
        gridPower = doc["siteCurrentPowerFlow"]["GRID"]["currentPower"];

        // Load values
        loadState = doc["siteCurrentPowerFlow"]["LOAD"]["status"];
        loadPower = doc["siteCurrentPowerFlow"]["LOAD"]["currentPower"];

        // PV values
        pvState = doc["siteCurrentPowerFlow"]["PV"]["status"];
        pvPower = doc["siteCurrentPowerFlow"]["PV"]["currentPower"];

        // Battery values
        battState = doc["siteCurrentPowerFlow"]["STORAGE"]["status"];
        battPower = doc["siteCurrentPowerFlow"]["STORAGE"]["currentPower"];
        battChargeLevel =	doc["siteCurrentPowerFlow"]["STORAGE"]["chargeLevel"];
        battCritical = doc["siteCurrentPowerFlow"]["STORAGE"]["critical"];          
        
        gridState = f_translateStatus(gridState);
        sprintf (grid, "%s: %5.2f %s %s", gridState, gridPower, measureUnit, toGrid ? "(Export)" : "(Import)");  
        
        pvState = f_translateStatus(pvState);
        sprintf (pv, "%s: %5.2f %s", pvState, pvPower, measureUnit);  
        
        loadState = f_translateStatus(loadState);
        sprintf (load, "%s: %5.2f %s", loadState, loadPower, measureUnit);

        battState = f_translateStatus(battState);
        
        double battCap = battCapacity * battChargeLevel /100;
        double rTime = 0.0;
        if (battPower > 0.0)
        {
          if (strcmp(battState, "Entladen") == 0)
          {
            rTime = battCap / battPower * 60;
          }
          else
          {
            if (strcmp(battState, "Laden") == 0)
            {
              rTime = (100.0-battChargeLevel) * battCapacity / battPower * 6 / 10 ;
            }
            else
            {
              rTime = 0.0;
            }
          }         
        }
        else
        {
          rTime = 0.0;
        }
        
        int rhour   = (int) rTime / 60;
        int rminute = (int) rTime % 60;
        int rday    = rhour / 24;
        rhour %= 24;

        sprintf (batt, "Ladung: %2.0f%%, %3.2f kWh \n%s: %3.2f %s, %dd%02d:%02dh", battChargeLevel, battCap, battState, battPower, measureUnit, rday, rhour, rminute);
        apiCallFailed = false;  
      }
      else
      {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
      }
    }
    else
    {
      Serial.printf("GET request failed (%x), error: %s\n", httpCode, http.errorToString(httpCode).c_str());
      //goloop = false;
      apiCallFailed = true;
    }
  }
  Serial.println("Leaving SolarEdgeAPICall()");
  return (apiCallFailed);
}

static int line = -20;
TFT_eSPI tft = TFT_eSPI();


void printrawln(const char * string, uint8_t font)
{  
  tft.drawString(string, 15, line+=15, font);
}

void pvInfoSetup() 
{
  //-----------------  
  // Initial API call
  //-----------------  
  if (!test)
  {
    batteryProblems = SolarEdgeStorageData();
  }
  else
  {
    Serial.println("SKIPPING INITIAL STORAGE DATA CALL");
  }  
  // SolarEdgeDailyEnergy();    Future use  
  if (!test)
  {
    apiCallFailed = SolarEdgeAPICall(2);
  }
  else
  {
    Serial.println("SKIPPING INITIAL API CALL");
  }  
  formerMinute = my_TZ.minute(TIME_NOW, LOCAL_TIME);
  updated = false;


  //----------------------   
  // Start LVGL
  //----------------------   
  lv_init();
  lv_log_register_print_cb(log_print);              // Register print function for debugging

  //-----------------------------------------------------------
  // Start the SPI for the touchscreen and init the touchscreen
  //-----------------------------------------------------------
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);                     // Screen is in landscape modus, rotation is 90°. 
                                                  // Lower left corner is (0, 0), upper right corner is SCREEN_HEIGHT, SCREEN_WIDTH
  //--------------  
  // Setup Display
  //--------------   
  // Create a display object
  lv_display_t * disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90); // original value _270 shows upside-down
  
  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb(indev, touchscreen_read);
  
  // Function to draw the GUI
  lv_create_main_gui();

  // Switch on backlight if not done yet
  switchBacklightOn();
  if (test)
  {
      lv_arc_set_angles(arc, 270, 90);
      Serial.println("**********************\n\nBOARD is in TEST mode!\n\n**********************");
  }
  else
  {
    lv_arc_set_angles(arc, 0, 360);
  }
}

void setInfoFrame()
{
  int lines = 0;
  int xStart = 260;
  int yStart = 5;
  tft.drawString("PV Info Display V 2.0", 10, 5, 4);  
  tft.drawString("powered by", 15, 40, 2);  
  tft.drawString("SolarEdge", 90, 35, 4);
  tft.drawString("Monitoring Server API (http://solaredge.com)", 15, 60, 2);
  for (lines; lines < 50; lines++)
  {
    tft.drawLine(xStart--, yStart, xStart+60, yStart, TFT_RED);
    yStart++;
  }
  tft.drawLine(0, 75, 319, 75, TFT_RED);
  tft.drawLine(0, 76, 319, 76, TFT_RED);
  tft.drawLine(0, 219, 319, 219, TFT_RED);
  tft.drawLine(0, 220, 319, 220, TFT_RED);
  tft.drawString("2025, by Axel Bergmann", 15, 222, 2);
}

void setup()
{
  // pinMode(PIN, INPUT_PULLUP);
  // Setup TFT
  tft.init();
  tft.setRotation(1); //This is the display in landscape
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  
  // Keep API specific data in flash
  Preferences preferences;

  // Access the preferences read-only
  // Copy them to public variables                     
  preferences.begin("pvInfo", true);
  siteID = preferences.getUInt("siteID");         
  apiKey = preferences.getString("apiKey");
  preferences.end();

  // Connect to Wi-Fi     https://github.com/tzapu/WiFiManager
  WiFi.mode(WIFI_STA); 
  Serial.begin(115200);
  
  //WiFiManager wm;
  wm.setConnectTimeout(2);
  wm.setConnectRetries(5);

  // reset settings - wipe stored credentials for testing
  // these are stored by the esp library

  // Text box (String) - 50 characters maximum

  char key[40] = {0};
  int site;
  strcpy(key, apiKey.c_str());
  WiFiManagerParameter custom_text_box_num("key_num", "Enter your site-ID", siteID.c_str(), 8); 
  WiFiManagerParameter custom_text_box("key_text", "Enter your API-key", key, 50);
  
  wm.addParameter(&custom_text_box_num);
  wm.addParameter(&custom_text_box);

  setInfoFrame();
 
  tft.drawString("Connect to network", 10, 90, 4);
  tft.drawString("\"PVInfoDisplay\"", 10, 120, 4);
  tft.drawString("[Address: 192.168.4.1]", 10, 150, 4);
  tft.drawString("Setup your parameters ", 10, 180, 4);

  if(!wm.autoConnect("PVInfoDisplay","solarEdge"))
  {
    Serial.println("Failed to connect");
    delay(1000);
    ESP.restart();
  } 
  else
  {
      //if you get here you have connected to the WiFi    
      Serial.println("Connected");
  }

  // Did information change in between?
  bool infoChanged = false;

  site = atoi(custom_text_box_num.getValue());
  sprintf (key, "%s", custom_text_box.getValue());
 
  if (site != atoi(siteID.c_str()) || strcmp(key, apiKey.c_str()) != 0)
    infoChanged = true;
  
  if (infoChanged)
  {
    Serial.println("Site information changed");
    preferences.begin("pvInfo", false);
    site = atoi(custom_text_box_num.getValue());
    preferences.putUInt("siteID", site);
    preferences.putString("apiKey", key);
    preferences.end();
  }
  else
  {
    Serial.println("Site information unchanged");
  }
 
  preferences.begin("pvInfo", true);
  siteID = preferences.getUInt("siteID");
  apiKey = preferences.getString("apiKey");
  preferences.end();

  tft.fillScreen(TFT_WHITE);
  setInfoFrame();

  line = 62;
  String network = "Connected to network \"" + wm.getWiFiSSID(true) + "\"";
  printrawln(network.c_str(), 2);
  network = "Address = " + WiFi.localIP().toString();
  printrawln(network.c_str(), 2);
  printrawln("Waiting for time sync - may take a minute...", 2);  //  95
  waitForSync();
  Serial.println("UTC:             " + UTC.dateTime());
  my_TZ.setLocation("Europe/Berlin");
  Serial.println(my_TZ.dateTime().c_str());     
  printrawln("Sync'ed - Timezone is Europe/Berlin", 2);           // 110
  printrawln(my_TZ.dateTime().c_str(), 2);
  tft.drawString("Credits:", 15, 158, 2);
  tft.drawString("Uses: TFT_eSPI, LVGL, ezTime, ArduinoJson", 20, 173, 2);
  tft.drawString("XPT2046_Touchscreen, WifiManager", 56, 188, 2);
  tft.drawString("Special thanks to \"Random Nerd Tutorials\"", 20, 203, 2);
  delay(5000);

  /*if (DataPeriod())       // DataPeriod needs some refinement
  {
    tft.fillScreen(TFT_WHITE);
    setInfoFrame();
    tft.drawString("The PV has no connection", 10, 90, 4);
    tft.drawString("to the monitoring server", 10, 120, 4);
    tft.drawString("Please switch off display", 10, 150, 4);
    tft.drawString("and resolve the problem", 10, 180, 4);
    goloop = false;    
  }
  else
  {
    
  }*/
  pvInfoSetup();
}

void loop()
{
  if (goloop)
  {
    lv_task_handler();  // let the GUI do its work
    lv_tick_inc(5);     // tell LVGL how much time has passed
    update_table_values();
    delay(5);           // let this time pass
  }
  else
  {

  }
}
