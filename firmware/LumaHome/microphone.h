#pragma once
// Add MIC_ENABLED 1 to config.h ONLY after checking the board's A0 voltage range.
#ifndef MIC_ENABLED
#define MIC_ENABLED 0
#endif
#ifndef MIC_NOISE_FLOOR
#define MIC_NOISE_FLOOR 8.0f
#endif
#ifndef MIC_GAIN
#define MIC_GAIN 6.0f
#endif
float micEnvelope=0,micCenter=512;
uint32_t micLast=0;
bool micInitialized=false;
void microphoneLoop(){
#if MIC_ENABLED
 uint32_t now=millis();if(now-micLast<6)return;micLast=now;
 float sample=analogRead(A0);if(!micInitialized){micCenter=sample;micInitialized=true;}
 micCenter+=(sample-micCenter)*.015f;
 float amplitude=max(0.0f,fabsf(sample-micCenter)-MIC_NOISE_FLOOR);
 float target=constrain(amplitude*MIC_GAIN*(.25f+speed/25.0f)/512.0f,0.0f,1.0f);
 micEnvelope+=(target-micEnvelope)*(target>micEnvelope?.55f:.12f);
#endif
}
