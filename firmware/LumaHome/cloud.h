#pragma once
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <time.h>
#include "cloud-ca.h"
#if __has_include("cloud-config.h")
#include "cloud-config.h"
#else
#define CLOUD_HOST "luma-home-atelier-d25.abloomgnome257.chatgpt.site"
#define CLOUD_KEY ""
#endif
BearSSL::WiFiClientSecure cloudClient;
BearSSL::Session cloudSession;
BearSSL::X509List cloudTrust(CLOUD_CA);
String cloudBuffer,cloudBoot;
uint32_t cloudRevision=0,cloudNext=0,cloudStarted=0;
bool cloudWaiting=false;
uint8_t cloudFailures=0;
bool cloudEnabled(){return strlen(CLOUD_KEY)>=24 && !String(CLOUD_KEY).startsWith("PASTE_");}
void updateLight();
void cloudSetup(){
 if(!cloudEnabled())return;
 configTime(0,0,"pool.ntp.org","time.google.com");
 cloudClient.setTrustAnchors(&cloudTrust);cloudClient.setSession(&cloudSession);
 cloudClient.setTimeout(4000);cloudClient.setNoDelay(true);
 cloudClient.setBufferSizes(16384,512);
 char boot[17];snprintf(boot,sizeof(boot),"%08x%08x",ESP.random(),ESP.random());cloudBoot=boot;
 Serial.println("Cloud enabled; waiting for secure time sync.");
}
void cloudFail(){cloudClient.stop();cloudWaiting=false;cloudBuffer="";cloudFailures=min(5,(int)cloudFailures+1);cloudNext=millis()+min(10000UL,500UL*(1UL<<cloudFailures));Serial.println("Cloud disconnected; reconnect scheduled.");}
// Decode a complete HTTP body. Supports Content-Length and chunked framing.
bool cloudBody(String& body){
 int split=cloudBuffer.indexOf("\r\n\r\n");if(split<0)return false;
 String headers=cloudBuffer.substring(0,split);headers.toLowerCase();String data=cloudBuffer.substring(split+4);
 int ci=headers.indexOf("content-length:");
 if(ci>=0){int length=headers.substring(ci+15).toInt();if(length<0||length>4096){cloudFail();return false;}if(data.length()<(unsigned)length)return false;body=data.substring(0,length);return true;}
 if(headers.indexOf("transfer-encoding: chunked")>=0){
  unsigned int pos=0;body="";
  while(true){int line=data.indexOf("\r\n",pos);if(line<0)return false;unsigned int size=strtoul(data.substring(pos,line).c_str(),nullptr,16);pos=line+2;if(size>4096){cloudFail();return false;}if(!size)return data.length()>=pos+2;if(data.length()<pos+size+2)return false;body+=data.substring(pos,pos+size);pos+=size+2;}
 }
 if(!cloudClient.connected()){body=data;return true;}return false;
}
void cloudApply(const JsonDocument& doc){
 uint32_t revision=doc["revision"]|cloudRevision;
 if((doc["apply"]|false)&&revision>cloudRevision){
  JsonObjectConst cmd=doc["command"].as<JsonObjectConst>();
  if(cmd.containsKey("color")){String c=cmd["color"].as<String>();if(c.length()==7){uint32_t value=strtoul(c.c_str()+1,nullptr,16);red=value>>16;green=value>>8;blue=value;}}
  if(cmd.containsKey("brightness"))brightness=constrain(cmd["brightness"].as<int>(),0,100);
  if(cmd.containsKey("speed"))speed=constrain(cmd["speed"].as<int>(),1,100);
  if(cmd.containsKey("power"))powerOn=cmd["power"].as<bool>();
  if(cmd.containsKey("effect")){String next=cmd["effect"].as<String>();if(next!="sound"||MIC_ENABLED)effect=next;}
  if(cmd.containsKey("timerEnd")){double until=cmd["timerEnd"].as<double>();double seconds=until/1000.0-(double)time(nullptr);timerActive=seconds>0;timerDeadline=millis()+(uint32_t)(max(0.0,seconds)*1000.0);}
  audioReceived=false;updateLight();
 }
 cloudRevision=revision;
}
void cloudLoop(){
 if(!cloudEnabled()||WiFi.status()!=WL_CONNECTED||time(nullptr)<1700000000)return;
 uint32_t now=millis();
 if(cloudWaiting){
  unsigned int count=0;while(cloudClient.available()&&count++<2048){cloudBuffer+=(char)cloudClient.read();if(cloudBuffer.length()>8192){cloudFail();return;}}
  String body;if(cloudBody(body)){
   if(!cloudBuffer.startsWith("HTTP/1.1 200")&&!cloudBuffer.startsWith("HTTP/1.0 200")){cloudFail();return;}
   StaticJsonDocument<1536> doc;if(deserializeJson(doc,body)){cloudFail();return;}
   cloudApply(doc);cloudWaiting=false;cloudBuffer="";cloudFailures=0;cloudNext=millis()+400;
  }else if(!cloudClient.connected()||now-cloudStarted>4000)cloudFail();
  return;
 }
 if((int32_t)(now-cloudNext)<0)return;
 if(!cloudClient.connected()&&!cloudClient.connect(CLOUD_HOST,443)){cloudFail();return;}
 StaticJsonDocument<512> report;char color[8];snprintf(color,sizeof(color),"#%02x%02x%02x",red,green,blue);
 report["micAvailable"]=bool(MIC_ENABLED);report["micLevel"]=(int)(micEnvelope*100);report["boot"]=cloudBoot;report["ack"]=cloudRevision;report["color"]=color;report["brightness"]=brightness;report["speed"]=speed;report["power"]=powerOn;report["effect"]=effect;
 report["remaining"]=timerActive&&(int32_t)(timerDeadline-millis())>0?(timerDeadline-millis()+999)/1000:0;report["rssi"]=WiFi.RSSI();
 String data;serializeJson(report,data);
 String request="POST /api/cloud/sync HTTP/1.1\r\nHost: "+String(CLOUD_HOST)+"\r\nX-API-Key: "+String(CLOUD_KEY)+"\r\nContent-Type: application/json\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\nContent-Length: "+String(data.length())+"\r\n\r\n"+data;
 if(cloudClient.print(request)!=request.length()){cloudFail();return;}
 cloudStarted=millis();cloudWaiting=true;cloudBuffer="";
}
