#include <Stepper.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
//#include <HTTPClientCB.h>
#include "HTTPClientCB.h"
#define TRIGGER_PIN 24
#include <ArduinoJson.h>
#include <string>

const char* ssid = "*******";
const char* password = "*******";


const int stepsPerRevolution = 2048;  // change this to fit the number of steps per revolution
String token = "";  //API Token
bool wm_nonblocking = false; // change to true to use non blocking
unsigned long previousMillis = 0;
unsigned long interval = 60000;
WiFiManager wm; // global wm instance
WiFiManagerParameter custom_field; // global param ( for non blocking w params )
String mac_address = "10:97:BD:33:A0:80";//WiFi.macAddress();
int http_error_count=0;
String api_device_id;
bool wait_notify = 0;
int motor_power = 25;

int request_count = 0;


#define IN1 15
#define IN2 4
#define IN3 16
#define IN4 17

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  5        /* Time ESP32 will go to sleep (in seconds) */
// initialize the stepper library
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);


void configModeCallback(WiFiManager *myWiFiManager) {
  //digitalWrite(LED_BUILTIN, LOW);
  ESP.restart();
}
String getParam(String name) {
  //read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback() {
  Serial.println("[CALLBACK] saveParamCallback fired");
  Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
}
void check_setting_wifi() {
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    Serial.println("偵測到按下WIFI設定鍵");
    WiFiManager wifiManager;
    digitalWrite(LED_BUILTIN, HIGH);
    wifiManager.setConfigPortalTimeout(60);
    wifiManager.startConfigPortal("AutoConnectAP");
    Serial.println("connected...yeey :)");
    digitalWrite(LED_BUILTIN, LOW);
  }
}
//清除指令
int clean_command(String device_id){
  HTTPClient http2;
  String post_data = "id="+device_id+"&token="+token;
  http2.begin("https://YOUR_API_HOST/clean-command");
  http2.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http2.POST(post_data);
  http2.end();
  return httpCode;
}
void wakeup_action(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : 
      Serial.println("Wakeup caused by timer");
      Serial.printf("ESP.restart()"); break;
      ESP.restart();
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}
void setup() {
 
  Serial.begin(115200);
  //wakeup_action();
  pinMode(motor_power, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  myStepper.setSpeed(10);
  

  unsigned long currentMillis = millis();

  request_count=0;
  http_error_count=0;
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  Serial.setDebugOutput(true);
//  delay(3000);
  Serial.println("\n Starting");
  Serial.println(mac_address);

  pinMode(TRIGGER_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  // wm.resetSettings(); // wipe settings
  
  if (wm_nonblocking) wm.setConfigPortalBlocking(false);

  // add a custom input field
  int customFieldLength = 40;
  const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input

  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);

  wm.setWiFiAutoReconnect(true);
  // wm.setMenu(menu,6);
  std::vector<const char *> menu = {"wifi", "info", "param", "sep", "restart", "exit"};
  wm.setMenu(menu);
  
  wm.setConfigPortalTimeout(60);
  
//  wm.setAPCallback(configModeCallback);
  bool res;
  //  wm.setAPCallback(0);
//  check_setting_wifi();
  digitalWrite(LED_BUILTIN, HIGH);
  res = wm.autoConnect("AutoConnectAP"); // password protected ap
  digitalWrite(LED_BUILTIN, LOW);
  if (!res) {
    Serial.println("Failed to connect or hit timeout");
    // ESP.restart();
  }
  else {
    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  }


  
}

void loop() {

  HTTPClient http;
  //查看即時命令
  Serial.print("查看即時命令...");
  http.begin("https://YOUR_API_HOST/commands");
  //http.addHeader("Content-Type", "application/json");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.POST("gateway="+mac_address+"&token="+token);
  Serial.println(httpCode);
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println(payload);
    String json = payload;
    Serial.println("parsing json");
    DynamicJsonDocument doc(500);
    deserializeJson(doc, json.c_str());
    Serial.println("parsing json...done");
    http.end();
    if(doc.size()>0){  
      for(int i=0;i<doc.size();i++){
        String device_id = doc[i]["id"].as<String>();
        int laps = doc[i]["command"].as<int>();
        Serial.println(laps);
        //清除指令
        Serial.print("清除指令..");
        int state_code = clean_command(device_id);
        if(state_code>0){
          Serial.println("成功");
          //啟動步進馬達
          digitalWrite(motor_power, HIGH);//開關
          Serial.println("clockwise");
          myStepper.step(stepsPerRevolution*laps);
          delay(1000);        
          //回轉放線
          //myStepper.step(-1*stepsPerRevolution*laps);
          digitalWrite(motor_power, LOW);//開關
        }
        else{
          Serial.println("失敗");
        }
      }
    }
  }
  else{
    http.end();
    http_error_count++;
    ESP.restart();
  }
  delay(1000);
  
  //查看排程任務
  Serial.println("查看排程任務...");
  Serial.print("POST: https://YOUR_API_HOST/schedule-task");
  http.begin("https://YOUR_API_HOST/schedule-task");
  //http.addHeader("Content-Type", "application/json");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  httpCode = http.POST("device_id="+mac_address+"&token="+token);
  Serial.println(httpCode);
  if (httpCode > 0) {
    String json = http.getString();
    Serial.println(json);
    Serial.println("parsing json");
    DynamicJsonDocument doc2(500);
    deserializeJson(doc2, json.c_str());
    Serial.println("parsing json...done");
    http.end(); //關閉http
    int retry_times = 3;
    for(int i=0;i<doc2.size();i++){

      
      String schedule_id = doc2[i]["schedule_id"].as<String>();
      int laps = doc2[i]["command"].as<int>();
      //更新執行時間
      Serial.print("更新執行時間...");
      Serial.print("POST: https://YOUR_API_HOST/schedule-task/update-last-exec-time");
      String post_data = "id="+schedule_id+"&token="+token;
      Serial.println(post_data);
      http.begin("https://YOUR_API_HOST/schedule-task/update-last-exec-time");
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int httpCode = http.POST(post_data);
      http.end(); 
      if(httpCode==200){
        retry_times = 3;
        Serial.println("成功");
        Serial.println("啟動步進馬達");
        Serial.println(laps);
        //啟動步進馬達
        digitalWrite(motor_power, HIGH);//開關
        Serial.println("clockwise");
        myStepper.step(stepsPerRevolution*laps);
        delay(1000);
        //回轉放線
  //      myStepper.step(-1*stepsPerRevolution*laps);
  //      digitalWrite(motor_power, LOW);//開關
      }
      else{
        Serial.print("失敗");
        Serial.println(httpCode);
        
        //重試
        if (retry_times){
          retry_times--;   
          i--;
        }
      }
    }
  }
  else{
    http.end(); //關閉http
  }
  delay(1000);
  
  
  Serial.print("查看當天下個任務倒數計時(秒)...");
  String countdown = "";
  http.begin("https://YOUR_API_HOST/schedule-task/countdown");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  httpCode = http.POST("device_id="+mac_address+"&token="+token);
  Serial.println(httpCode);
  if (httpCode > 0) {
    countdown = http.getString();
    Serial.print("剩餘時間(秒):");
    Serial.println(countdown);
  }
  http.end(); //關閉http
  delay(1000);
  if(http_error_count>10){
      ESP.restart();
  }
  //設定下次醒來時間(秒)
  int sleep_time = 60*60;
  if(countdown != "" and countdown.toInt()>0){
    if(countdown.toInt()<=65*60){
      sleep_time = countdown.toInt();
    }
  }
  Serial.print("已設定下次醒來時間(秒):");
  Serial.println(sleep_time);
  digitalWrite(motor_power, LOW);
  digitalWrite(19, LOW);
  digitalWrite(18, LOW);
  digitalWrite(17, LOW);
  digitalWrite(5, LOW);
  Serial.print("Deep Sleep...");
  Serial.println(sleep_time);
  esp_sleep_enable_timer_wakeup(sleep_time * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}
