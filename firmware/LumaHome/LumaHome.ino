#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <math.h>
#include "config.h"

ESP8266WebServer server(80);
uint8_t red=255, green=158, blue=87, brightness=75, speed=35;
bool powerOn=false, timerActive=false;
String effect="static";
uint32_t timerDeadline=0, lastFrame=0, audioTime=0;
bool audioReceived=false;
uint8_t audioLevel=100;
float smoothR=0, smoothG=0, smoothB=0;

void cors() {
  String origin=server.header("Origin");
  if(origin==ALLOWED_ORIGIN) {
    server.sendHeader("Access-Control-Allow-Origin",origin);
    server.sendHeader("Vary","Origin");
  }
  server.sendHeader("Access-Control-Allow-Methods","GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers","Content-Type, X-API-Key");
  server.sendHeader("Cache-Control","no-store");
}
void errorResponse(int code,const String& message) {
  cors(); server.send(code,"application/json","{\"error\":\""+message+"\"}");
}
bool authorized() {
  String origin=server.header("Origin");
  String localOrigin="http://"+WiFi.localIP().toString();
  if(origin.length() && origin!=ALLOWED_ORIGIN && origin!=localOrigin && origin!="http://luma.local") {
    errorResponse(403,"Origin denied"); return false;
  }
  if(server.header("X-API-Key")!=API_KEY) {errorResponse(401,"Invalid API key");return false;}
  return true;
}
bool numberArg(const char* name,int minimum,int maximum,int& value) {
  if(!server.hasArg(name))return true;
  String text=server.arg(name);
  if(text.length()==0 || text.length()>6)return false;
  for(unsigned int i=0;i<text.length();i++)if(!isDigit(text[i]))return false;
  int n=text.toInt();if(n<minimum||n>maximum)return false;value=n;return true;
}
void sendState() {
  char color[8];snprintf(color,sizeof(color),"#%02x%02x%02x",red,green,blue);
  uint32_t remaining=timerActive && (int32_t)(timerDeadline-millis())>0 ? (timerDeadline-millis()+999)/1000:0;
  String result="{\"device\":\"luma-esp8266\",\"color\":\""+String(color)+"\",\"brightness\":"+String(brightness)+",\"speed\":"+String(speed)+",\"power\":"+(powerOn?"true":"false")+",\"effect\":\""+effect+"\",\"remaining\":"+String(remaining)+",\"rssi\":"+String(WiFi.RSSI())+"}";
  cors();server.send(200,"application/json",result);
}
void statePost() {
  if(!authorized())return;
  // Validate the entire update before changing outputs.
  int nextBrightness=brightness,nextSpeed=speed,nextPower=powerOn?1:0,nextAudio=100;
  if(!numberArg("brightness",0,100,nextBrightness)||!numberArg("speed",1,100,nextSpeed)||!numberArg("power",0,1,nextPower)||!numberArg("audio",0,100,nextAudio)) {errorResponse(400,"Out of range");return;}
  String nextEffect=server.hasArg("effect")?server.arg("effect"):effect;
  if(nextEffect!="static"&&nextEffect!="breathe"&&nextEffect!="rainbow"&&nextEffect!="candle"){errorResponse(400,"Unknown effect");return;}
  uint32_t color=(red<<16)|(green<<8)|blue;
  if(server.hasArg("color")) {
    String value=server.arg("color");
    if(value.length()!=6){errorResponse(400,"Color must be six hex digits");return;}
    for(int i=0;i<6;i++)if(!isxdigit(value[i])){errorResponse(400,"Invalid hex color");return;}
    color=strtoul(value.c_str(),nullptr,16);
  }
  red=(color>>16)&255;green=(color>>8)&255;blue=color&255;
  brightness=nextBrightness;speed=nextSpeed;powerOn=nextPower;effect=nextEffect;
  if(server.hasArg("audio")){audioLevel=nextAudio;audioTime=millis();audioReceived=true;}
  else audioReceived=false;
  sendState();
}
void timerPost() {
  if(!authorized())return;
  int minutes=0;
  if(!server.hasArg("minutes")||!numberArg("minutes",0,1440,minutes)){errorResponse(400,"Invalid minutes");return;}
  timerActive=minutes>0;timerDeadline=millis()+(uint32_t)minutes*60000UL;sendState();
}
void rgbFromHue(float hue,float& r,float& g,float& b) {
  float h=fmodf(hue,360.0f)/60.0f;
  float x=1-fabsf(fmodf(h,2.0f)-1);
  float a=0,c=0,d=0;
  if(h<1){a=1;c=x;}else if(h<2){a=x;c=1;}else if(h<3){c=1;d=x;}else if(h<4){c=x;d=1;}else if(h<5){a=x;d=1;}else{a=1;d=x;}
  r=255*(.34f+.66f*a);g=255*(.34f+.66f*c);b=255*(.34f+.66f*d);
}
void updateLight() {
  uint32_t now=millis();
  if(timerActive&&(int32_t)(now-timerDeadline)>=0){timerActive=false;powerOn=false;}
  if(now-lastFrame<20)return;lastFrame=now;
  float r=red,g=green,b=blue,level=brightness/100.0f;
  float phase=(now/1000.0f)*(.1f+speed/100.0f);
  if(effect=="breathe")level*=.35f+.65f*(sinf(phase)+1)/2;
  if(effect=="candle")level*=.75f+.15f*sinf(phase*13)+.1f*sinf(phase*23);
  if(effect=="rainbow")rgbFromHue(phase*32,r,g,b);
  // Audio frames expire automatically if the browser disconnects.
  if(audioReceived&&(uint32_t)(now-audioTime)<1000)level*=audioLevel/100.0f;
  if(!powerOn){smoothR=smoothG=smoothB=0;}
  else {smoothR+=(r*level-smoothR)*.25f;smoothG+=(g*level-smoothG)*.25f;smoothB+=(b*level-smoothB)*.25f;}
  analogWrite(RED_PIN,lroundf(smoothR));analogWrite(GREEN_PIN,lroundf(smoothG));analogWrite(BLUE_PIN,lroundf(smoothB));
}
void serveFile(const char* path,const char* type) {
  if(!LittleFS.exists(path)){server.send(503,"text/plain","Upload the data directory to LittleFS first.");return;}
  File file=LittleFS.open(path,"r");server.streamFile(file,type);file.close();
}
void setup() {
  Serial.begin(115200);
  pinMode(RED_PIN,OUTPUT);pinMode(GREEN_PIN,OUTPUT);pinMode(BLUE_PIN,OUTPUT);
  analogWriteRange(255);analogWriteFreq(1000);
  analogWrite(RED_PIN,0);analogWrite(GREEN_PIN,0);analogWrite(BLUE_PIN,0);
  if(strlen(API_KEY)<24 || String(API_KEY).startsWith("CHANGE_ME")) {
    Serial.println("Set a unique API_KEY (at least 24 characters) in config.h, then upload again.");
    while(true)delay(1000);
  }
  LittleFSConfig fsConfig;fsConfig.setAutoFormat(false);LittleFS.setConfig(fsConfig);
  if(!LittleFS.begin())Serial.println("LittleFS unavailable. Upload filesystem image.");
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.hostname("luma");WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print('.');}
  Serial.println();Serial.print("Luma: http://");Serial.println(WiFi.localIP());
  if(MDNS.begin("luma"))MDNS.addService("http","tcp",80);
  server.collectHeaders("X-API-Key","Origin");
  server.on("/api/state",HTTP_GET,[]{if(authorized())sendState();});
  server.on("/api/state",HTTP_POST,statePost);
  server.on("/api/timer",HTTP_POST,timerPost);
  server.on("/api/state",HTTP_OPTIONS,[]{cors();server.send(204);});
  server.on("/api/timer",HTTP_OPTIONS,[]{cors();server.send(204);});
  server.on("/",HTTP_GET,[]{serveFile("/index.html","text/html; charset=utf-8");});
  server.on("/index.html",HTTP_GET,[]{serveFile("/index.html","text/html; charset=utf-8");});
  server.on("/app.js",HTTP_GET,[]{serveFile("/app.js","application/javascript; charset=utf-8");});
  server.on("/style.css",HTTP_GET,[]{serveFile("/style.css","text/css; charset=utf-8");});
  server.on("/art.svg",HTTP_GET,[]{serveFile("/art.svg","image/svg+xml");});
  server.on("/guide.html",HTTP_GET,[]{serveFile("/guide.html","text/html; charset=utf-8");});
  server.onNotFound([]{server.send(404,"text/plain","Not found");});server.begin();
}
void loop(){server.handleClient();MDNS.update();updateLight();yield();}
