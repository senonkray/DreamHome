#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <flash_hal.h>
#include <math.h>
#include "config.h"
#if __has_include("cloud-config.h")
#include "cloud-config.h"
#endif

#define LUMA_VERSION "2.2.0"
ESP8266WebServer server(80);
uint8_t red=255, green=158, blue=87, brightness=75, speed=35;
bool powerOn=false, timerActive=false;
String effect="static";
uint32_t timerDeadline=0, lastFrame=0, audioTime=0;
bool audioReceived=false;
uint8_t audioLevel=100;
float smoothR=0, smoothG=0, smoothB=0;
// Custom light/sound modes: a palette of up to 6 colors plus a motion or a sound reaction.
const char* const EFFECTS[]={"static","breathe","rainbow","candle","custom","sound"};
const char* const MOTIONS[]={"fade","jump","pulse","flicker"};
const char* const REACTIONS[]={"level","gradient","beat","spark"};
uint32_t palette[6]={0xff9e57};
uint8_t paletteCount=1, motion=0, reaction=0, floorLevel=10, beatIndex=0;
float phaseAcc=0, cyclePos=0, beatAverage=0, spark=0;
uint32_t lastBeat=0;
#include "microphone.h"
#include "cloud.h"
#include "wifi.h"

void cors() {
  server.client().setNoDelay(true);
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
bool localKeySet(){return strlen(API_KEY)>=24 && !String(API_KEY).startsWith("CHANGE_ME");}
// Returns the HTTP status that rejects this request, or 0 when it is allowed.
int authStatus() {
  if(!localKeySet())return 401;
  String origin=server.header("Origin");
  String localOrigin="http://"+WiFi.localIP().toString();
  if(origin.length() && origin!=ALLOWED_ORIGIN && origin!=localOrigin && origin!="http://luma.local")return 403;
  return server.header("X-API-Key")==API_KEY?0:401;
}
bool authorized() {
  int status=authStatus();if(!status)return true;
  errorResponse(status,status==403?"Origin denied":localKeySet()?"Invalid API key":"Local API key not configured");return false;
}
bool numberArg(const char* name,int minimum,int maximum,int& value) {
  if(!server.hasArg(name))return true;
  String text=server.arg(name);
  if(text.length()==0 || text.length()>6)return false;
  for(unsigned int i=0;i<text.length();i++)if(!isDigit(text[i]))return false;
  int n=text.toInt();if(n<minimum||n>maximum)return false;value=n;return true;
}
int nameIndex(const char* const* names,int count,const String& value) {
  for(int i=0;i<count;i++)if(value==names[i])return i;
  return -1;
}
// "ff0000,00ff00,#0000ff" -> up to 6 colors. Rejects the whole list on any bad item.
bool parsePalette(const String& text,uint32_t* out,uint8_t& count) {
  count=0;int start=0;
  while(start<=(int)text.length()) {
    int end=text.indexOf(',',start);if(end<0)end=text.length();
    String item=text.substring(start,end);item.trim();if(item.startsWith("#"))item.remove(0,1);
    if(item.length()!=6||count>=6)return false;
    for(int i=0;i<6;i++)if(!isxdigit(item[i]))return false;
    out[count++]=strtoul(item.c_str(),nullptr,16);start=end+1;
  }
  return count>0;
}
String paletteText() {
  String text;char hex[7];
  for(uint8_t i=0;i<paletteCount;i++){snprintf(hex,sizeof(hex),"%06x",palette[i]&0xffffff);if(i)text+=',';text+=hex;}
  return text;
}
void sendState() {
  static uint32_t sketchSize=ESP.getSketchSize(), freeSketch=ESP.getFreeSketchSpace();
  char color[8];snprintf(color,sizeof(color),"#%02x%02x%02x",red,green,blue);
  uint32_t remaining=timerActive && (int32_t)(timerDeadline-millis())>0 ? (timerDeadline-millis()+999)/1000:0;
  String result="{\"device\":\"luma-esp8266\",\"engine\":2,\"color\":\""+String(color)+"\",\"palette\":\""+paletteText()+"\",\"brightness\":"+String(brightness)+",\"speed\":"+String(speed)+",\"power\":"+(powerOn?"true":"false")+",\"effect\":\""+effect+"\"";
  result+=",\"motion\":\""+String(MOTIONS[motion])+"\",\"react\":\""+String(REACTIONS[reaction])+"\",\"floor\":"+String(floorLevel)+",\"remaining\":"+String(remaining)+",\"rssi\":"+String(WiFi.RSSI());
  String ssid=WiFi.SSID();ssid.replace("\\","\\\\");ssid.replace("\"","\\\"");
  result+=",\"wifi\":{\"ssid\":\""+ssid+"\",\"backup\":"+(wifiNet?"true":"false")+",\"hasBackup\":"+(wifiHasBackup()?"true":"false")+"}";
  result+=",\"micAvailable\":"+String(MIC_ENABLED?"true":"false")+",\"micLevel\":"+String((int)(micEnvelope*100));
  result+=",\"fw\":\""+String(LUMA_VERSION)+"\",\"ota\":{\"sketch\":"+String(sketchSize)+",\"free\":"+String(freeSketch)+",\"fs\":"+String((uint32_t)(FS_end-FS_start))+"}";
  result+=",\"pins\":{\"r\":"+String(RED_PIN)+",\"g\":"+String(GREEN_PIN)+",\"b\":"+String(BLUE_PIN)+",\"mic\":\"A0\"}}";
  cors();server.send(200,"application/json",result);
}
void statePost() {
  if(!authorized())return;
  // Validate the entire update before changing outputs.
  int nextBrightness=brightness,nextSpeed=speed,nextPower=powerOn?1:0,nextAudio=100,nextFloor=floorLevel,nextMotion=motion,nextReaction=reaction;
  if(!numberArg("brightness",0,100,nextBrightness)||!numberArg("speed",1,100,nextSpeed)||!numberArg("power",0,1,nextPower)||!numberArg("audio",0,100,nextAudio)||!numberArg("floor",0,100,nextFloor)) {errorResponse(400,"Out of range");return;}
  String nextEffect=server.hasArg("effect")?server.arg("effect"):effect;
  if(nameIndex(EFFECTS,6,nextEffect)<0){errorResponse(400,"Unknown effect");return;}
  if(server.hasArg("motion")&&(nextMotion=nameIndex(MOTIONS,4,server.arg("motion")))<0){errorResponse(400,"Unknown motion");return;}
  if(server.hasArg("react")&&(nextReaction=nameIndex(REACTIONS,4,server.arg("react")))<0){errorResponse(400,"Unknown reaction");return;}
  uint32_t nextPalette[6];uint8_t nextCount=0;
  if(server.hasArg("palette")&&!parsePalette(server.arg("palette"),nextPalette,nextCount)){errorResponse(400,"Palette must be 1-6 hex colors");return;}
  uint32_t color=(red<<16)|(green<<8)|blue;
  if(server.hasArg("color")) {
    String value=server.arg("color");
    if(value.length()!=6){errorResponse(400,"Color must be six hex digits");return;}
    for(int i=0;i<6;i++)if(!isxdigit(value[i])){errorResponse(400,"Invalid hex color");return;}
    color=strtoul(value.c_str(),nullptr,16);
  }
  if(nextCount){memcpy(palette,nextPalette,sizeof(uint32_t)*nextCount);paletteCount=nextCount;}
  else if(server.hasArg("color")){palette[0]=color;paletteCount=1;}
  red=(color>>16)&255;green=(color>>8)&255;blue=color&255;
  brightness=nextBrightness;speed=nextSpeed;powerOn=nextPower;effect=nextEffect;
  motion=nextMotion;reaction=nextReaction;floorLevel=nextFloor;
  // Browser microphone frames feed sound modes; they expire after 1 s.
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
// Color at a position along the palette; blend=false holds each color, blend=true crossfades.
void paletteAt(float position,bool blend,float& r,float& g,float& b) {
  float p=fmodf(position,(float)paletteCount);if(p<0)p+=paletteCount;
  uint8_t i=min((int)p,paletteCount-1),j=(i+1)%paletteCount;
  float f=blend?p-i:0;f=f*f*(3-2*f);
  uint32_t a=palette[i],c=palette[j];
  r=((a>>16)&255)*(1-f)+((c>>16)&255)*f;g=((a>>8)&255)*(1-f)+((c>>8)&255)*f;b=(a&255)*(1-f)+(c&255)*f;
}
float soundLevel(uint32_t now) {
  if(audioReceived&&(uint32_t)(now-audioTime)<1000)return audioLevel/100.0f;
  return micEnvelope;
}
void updateLight() {
  uint32_t now=millis();
  if(timerActive&&(int32_t)(now-timerDeadline)>=0){timerActive=false;powerOn=false;}
  if(now-lastFrame<20)return;
  float dt=min(.1f,(now-lastFrame)/1000.0f);lastFrame=now;
  float r=red,g=green,b=blue,shape=1,minimum=floorLevel/100.0f;
  // Accumulated phase keeps effects continuous when speed changes.
  phaseAcc+=dt*(.1f+speed/100.0f);if(phaseAcc>6283.19f)phaseAcc-=6283.19f;
  if(effect=="breathe")shape=.35f+.65f*(sinf(phaseAcc)+1)/2;
  else if(effect=="candle")shape=.75f+.15f*sinf(phaseAcc*13)+.1f*sinf(phaseAcc*23);
  else if(effect=="rainbow")rgbFromHue(phaseAcc*32,r,g,b);
  else if(effect=="custom") {
    // At most ~1 color step per second: never a strobe.
    cyclePos+=dt*(.04f+speed*.009f);if(cyclePos>1000.0f*paletteCount)cyclePos=fmodf(cyclePos,paletteCount);
    if(motion==0)paletteAt(cyclePos,true,r,g,b);
    else if(motion==1)paletteAt(cyclePos,false,r,g,b);
    else if(motion==2){float w=sinf(PI*(cyclePos-floorf(cyclePos)));shape=w*w;paletteAt(cyclePos,false,r,g,b);}
    else {shape=.72f+.14f*sinf(phaseAcc*13)+.1f*sinf(phaseAcc*23)+.04f*sinf(phaseAcc*41);paletteAt(cyclePos/3+.15f*sinf(phaseAcc*7),true,r,g,b);}
    shape=minimum+(1-minimum)*shape;
  }
  else if(effect=="sound") {
    float s=soundLevel(now),pulse=s;
    beatAverage+=(s-beatAverage)*.03f;
    // Beat = level jumps above its running average; at most 3 per second.
    if(s>beatAverage*1.3f+.06f&&now-lastBeat>333){lastBeat=now;beatIndex++;spark=1;}
    spark*=.9f;
    if(reaction==1){paletteAt(constrain(s,0.0f,.999f)*(paletteCount-1),true,r,g,b);pulse=.35f+.65f*s;}
    else if(reaction==2){paletteAt(beatIndex,false,r,g,b);pulse=max(s,spark*.9f);}
    else if(reaction==3){paletteAt(beatIndex,false,r,g,b);pulse=spark;}
    else paletteAt(0,false,r,g,b);
    shape=minimum+(1-minimum)*pulse;
  }
  float level=brightness/100.0f*shape;
  if(!powerOn){smoothR=smoothG=smoothB=0;}
  else {smoothR+=(r*level-smoothR)*.25f;smoothG+=(g*level-smoothG)*.25f;smoothB+=(b*level-smoothB)*.25f;}
  analogWrite(RED_PIN,lroundf(smoothR));analogWrite(GREEN_PIN,lroundf(smoothG));analogWrite(BLUE_PIN,lroundf(smoothB));
}
// Wi-Fi update: POST /api/update, multipart field "firmware" (firmware.bin) or "filesystem" (littlefs.bin).
// The light stays as it is while the image is written; the board restarts after a successful update.
bool updateAllowed=false;
String updateError;
uint32_t restartAt=0;
void updateFail(const String& message){if(!updateError.length())updateError=message;if(Update.isRunning())Update.end();}
void updateUpload() {
  HTTPUpload& upload=server.upload();
  if(upload.status==UPLOAD_FILE_START) {
    updateError="";updateAllowed=authStatus()==0;
    if(!updateAllowed)return;
    bool fs=upload.name=="filesystem";
    Serial.printf("Update %s: %s\n",fs?"filesystem":"firmware",upload.filename.c_str());
    if(fs){LittleFS.end();if(!Update.begin((size_t)FS_end-(size_t)FS_start,U_FS))updateFail(Update.getErrorString());}
    else if(!Update.begin((ESP.getFreeSketchSpace()-0x1000)&0xFFFFF000,U_FLASH))updateFail(Update.getErrorString());
  } else if(!updateAllowed||updateError.length()) {
    return;
  } else if(upload.status==UPLOAD_FILE_WRITE) {
    // A firmware image starts with 0xE9; writing one into LittleFS would erase the web app.
    if(Update.progress()==0&&upload.currentSize&&upload.buf[0]==0xE9&&upload.name=="filesystem"){updateFail("This is firmware.bin, not littlefs.bin");return;}
    if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize)updateFail(Update.getErrorString());
  } else if(upload.status==UPLOAD_FILE_END) {
    if(!Update.end(true))updateFail(Update.getErrorString());
  } else if(upload.status==UPLOAD_FILE_ABORTED) {
    updateFail("Upload aborted");
  }
  yield();
}
void updateDone() {
  if(!updateAllowed){authorized();return;}
  if(updateError.length()||!Update.isFinished()){
    if(!updateError.length())updateError="No file received";
    if(!LittleFS.begin())Serial.println("LittleFS unavailable after failed update");
    updateError.replace("\"","'");errorResponse(400,updateError);return;
  }
  cors();server.sendHeader("Connection","close");server.send(200,"application/json","{\"ok\":true,\"restart\":true}");
  restartAt=millis()+700;
}
// Fallback page in flash: works even when LittleFS is empty or broken.
const char UPDATE_PAGE[] PROGMEM=R"(<!doctype html><html lang=ru><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Luma · обновление</title>
<style>body{font:16px system-ui;background:#121311;color:#eee;max-width:420px;margin:40px auto;padding:0 16px}input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0;padding:12px;border-radius:12px;border:1px solid #444;background:#1d1e1b;color:#eee}button{background:#ff9e57;color:#121311;border:0;font-weight:600}progress{width:100%}</style>
<h2>Luma · обновление по Wi-Fi</h2><p>Версия прошивки: <b id=v>…</b></p>
<input id=k type=password placeholder="API_KEY из config.h"><input id=f type=file accept=".bin,.gz"><button id=b>Загрузить</button><progress id=p max=100 value=0 hidden></progress><p id=m></p>
<script>
const $=id=>document.getElementById(id);$('k').value=localStorage.getItem('luma-token')||sessionStorage.getItem('luma-token')||'';
fetch('/api/state',{headers:{'X-API-Key':$('k').value}}).then(r=>r.json()).then(s=>$('v').textContent=s.fw||'?').catch(()=>$('v').textContent='?');
$('b').onclick=async()=>{const file=$('f').files[0];if(!file)return;const head=new Uint8Array(await file.slice(0,2).arrayBuffer());
const fw=head[0]==0xE9||(head[0]==0x1f&&head[1]==0x8b);const fd=new FormData();fd.append(fw?'firmware':'filesystem',file);
const x=new XMLHttpRequest();x.open('POST','/api/update');x.setRequestHeader('X-API-Key',$('k').value);
$('p').hidden=false;$('m').textContent=fw?'Прошивка…':'Файловая система…';x.upload.onprogress=e=>$('p').value=e.loaded/e.total*100;
x.onload=()=>{if(x.status==200){$('m').textContent='Готово. Плата перезагружается…';setTimeout(()=>location.href='/',9000)}else $('m').textContent='Ошибка: '+x.responseText};
x.onerror=()=>$('m').textContent='Соединение прервано';x.send(fd)};
</script>)";
void serveFile(const char* path,const char* type) {
  if(!LittleFS.exists(path)){server.send(503,"text/plain","Upload the data directory to LittleFS first.");return;}
  File file=LittleFS.open(path,"r");server.streamFile(file,type);file.close();
}
void setup() {
  Serial.begin(115200);
  pinMode(RED_PIN,OUTPUT);pinMode(GREEN_PIN,OUTPUT);pinMode(BLUE_PIN,OUTPUT);
  analogWriteRange(255);analogWriteFreq(1000);
  analogWrite(RED_PIN,0);analogWrite(GREEN_PIN,0);analogWrite(BLUE_PIN,0);
  if(!cloudEnabled() && (strlen(API_KEY)<24 || String(API_KEY).startsWith("CHANGE_ME"))) {
    Serial.println("Set a unique API_KEY (at least 24 characters) in config.h, then upload again.");
    while(true)delay(1000);
  }
  LittleFSConfig fsConfig;fsConfig.setAutoFormat(false);LittleFS.setConfig(fsConfig);
  if(!LittleFS.begin())Serial.println("LittleFS unavailable. Upload filesystem image.");
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setSleepMode(WIFI_NONE_SLEEP);WiFi.setAutoReconnect(true);WiFi.hostname("luma");wifiStart();Serial.print("Luma: http://");Serial.println(WiFi.localIP());
  if(MDNS.begin("luma"))MDNS.addService("http","tcp",80);
  server.collectHeaders("X-API-Key","Origin");
  server.on("/api/state",HTTP_GET,[]{if(authorized())sendState();});
  server.on("/api/state",HTTP_POST,statePost);
  server.on("/api/timer",HTTP_POST,timerPost);
  server.on("/api/state",HTTP_OPTIONS,[]{cors();server.send(204);});
  server.on("/api/timer",HTTP_OPTIONS,[]{cors();server.send(204);});
  server.on("/",HTTP_GET,[]{serveFile("/index.html","text/html; charset=utf-8");});
  server.on("/index.html",HTTP_GET,[]{serveFile("/index.html","text/html; charset=utf-8");});
  server.on("/transport.js",HTTP_GET,[]{serveFile("/transport.js","application/javascript; charset=utf-8");});
  server.on("/app.js",HTTP_GET,[]{serveFile("/app.js","application/javascript; charset=utf-8");});
  server.on("/style.css",HTTP_GET,[]{serveFile("/style.css","text/css; charset=utf-8");});
  server.on("/art.svg",HTTP_GET,[]{serveFile("/art.svg","image/svg+xml");});
  server.on("/guide.html",HTTP_GET,[]{serveFile("/guide.html","text/html; charset=utf-8");});
  server.on("/api/update",HTTP_POST,updateDone,updateUpload);
  server.on("/api/update",HTTP_OPTIONS,[]{cors();server.send(204);});
  server.on("/update",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",UPDATE_PAGE);});
  // PlatformIO / Arduino IDE upload over Wi-Fi (espota), protected by API_KEY.
  if(localKeySet()) {
    ArduinoOTA.setHostname("luma");ArduinoOTA.setPassword(API_KEY);
    ArduinoOTA.onStart([]{if(ArduinoOTA.getCommand()==U_FS)LittleFS.end();analogWrite(RED_PIN,0);analogWrite(GREEN_PIN,0);analogWrite(BLUE_PIN,0);Serial.println("OTA update started");});
    ArduinoOTA.onError([](ota_error_t error){Serial.printf("OTA error %u\n",error);LittleFS.begin();});
    ArduinoOTA.begin(false);MDNS.enableArduino(8266,true);
  }
  server.onNotFound([]{server.send(404,"text/plain","Not found");});server.begin();cloudSetup();
}
void loop(){if(restartAt&&(int32_t)(millis()-restartAt)>=0){Serial.println("Update done, restarting");delay(50);ESP.restart();}
  wifiLoop();server.handleClient();ArduinoOTA.handle();MDNS.update();microphoneLoop();updateLight();cloudLoop();updateLight();yield();}
