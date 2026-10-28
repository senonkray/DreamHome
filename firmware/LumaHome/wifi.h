#pragma once
// Two Wi-Fi networks: the first one always has priority, the second is only a backup.
// Switch to the backup when the primary drops (or, with cloud enabled, loses internet);
// while on the backup, scan for the primary and return as soon as it is back.
#ifndef WIFI_SSID_2
#define WIFI_SSID_2 ""
#define WIFI_PASSWORD_2 ""
#endif
const uint32_t WIFI_SWITCH_MS=20000;      // no link this long -> try the other network
const uint32_t WIFI_CHECK_MS=30000;       // internet check period; 3 failures in a row = router hung
const uint32_t WIFI_RETURN_MS=60000;      // first look for the primary after this long on the backup
const uint32_t WIFI_RETURN_MAX_MS=600000; // the wait doubles while the primary keeps failing
const uint32_t WIFI_STABLE_MS=300000;     // primary up this long = healthy again, reset the wait
uint8_t wifiNet=0, wifiFails=0;
bool wifiLinked=false;
uint32_t wifiSince=0, wifiUpAt=0, wifiCheckAt=0, wifiReturnAt=0, wifiBackoff=WIFI_RETURN_MS;
bool wifiHasBackup(){return strlen(WIFI_SSID_2)>0;}
void wifiJoin(uint8_t net) {
  uint32_t now=millis();
  if(wifiNet==0&&net==1)wifiBackoff=wifiUpAt&&now-wifiUpAt>WIFI_STABLE_MS?WIFI_RETURN_MS:min(WIFI_RETURN_MAX_MS,wifiBackoff*2);
  wifiNet=net;wifiSince=now;wifiUpAt=0;wifiLinked=false;
  WiFi.disconnect();WiFi.begin(net?WIFI_SSID_2:WIFI_SSID,net?WIFI_PASSWORD_2:WIFI_PASSWORD);
  Serial.printf("Wi-Fi: joining %s network\n",net?"backup":"primary");
}
void wifiStart() {
  wifiJoin(0);Serial.print("Connecting to Wi-Fi");
  while(WiFi.status()!=WL_CONNECTED) {
    delay(500);Serial.print('.');
    if(wifiHasBackup()&&millis()-wifiSince>WIFI_SWITCH_MS)wifiJoin(!wifiNet);
  }
  Serial.println();
}
bool wifiInternet() {
  WiFiClient probe;probe.setTimeout(1500);
  bool ok=probe.connect(IPAddress(1,1,1,1),53)||probe.connect(IPAddress(8,8,8,8),53);
  probe.stop();return ok;
}
void wifiLoop() {
  if(!wifiHasBackup())return;
  uint32_t now=millis();bool linked=WiFi.status()==WL_CONNECTED;
  if(linked!=wifiLinked) {
    wifiLinked=linked;wifiSince=now;wifiFails=0;wifiCheckAt=now+WIFI_CHECK_MS;
    if(linked){wifiUpAt=now;wifiReturnAt=now+wifiBackoff;Serial.printf("Wi-Fi: %s network, http://%s\n",wifiNet?"backup":"primary",WiFi.localIP().toString().c_str());}
  }
  if(!linked){if(now-wifiSince>WIFI_SWITCH_MS)wifiJoin(!wifiNet);return;}
  if(wifiNet==0) {
    // Associated but no internet: the router hangs. Only checked when the cloud is in use,
    // so a purely local setup never leaves a working home network.
    if(!cloudEnabled()||(int32_t)(now-wifiCheckAt)<0)return;
    wifiCheckAt=now+WIFI_CHECK_MS;
    if(wifiInternet())wifiFails=0;
    else if(++wifiFails>=3){Serial.println("Wi-Fi: primary has no internet");wifiJoin(1);}
    return;
  }
  if((int32_t)(now-wifiReturnAt)<0)return;
  int found=WiFi.scanComplete();
  if(found==WIFI_SCAN_RUNNING)return;
  if(found<0){WiFi.scanNetworks(true);return;}
  bool seen=false;
  for(int i=0;i<found;i++)if(WiFi.SSID(i)==WIFI_SSID&&WiFi.RSSI(i)>-85)seen=true;
  WiFi.scanDelete();wifiReturnAt=now+wifiBackoff;
  if(seen)wifiJoin(0);
}
