#include "BluetoothSerial.h"
#include "heltec.h"

#include "Spark.h"
#include "SparkAppIO.h"
#include "SparkIO.h"

#define BUF_MAX 5000
#define PGM_NAME "Heltec - Spark"
#define SCR_HEIGHT 10
#define CONNECT_PIN 19
#define HW_SERIAL2_RX 34
#define SWITCH_DEBOUNCE 400

HardwareSerial *midi;

bool app_connected;

int scr_line;
uint8_t pre;                          // current preset
uint8_t buf[BUF_MAX];

SparkAppIO app_io(true);
SparkIO spark_io(true);

char str[STR_LEN];
uint8_t selected_preset;

uint8_t b;
uint8_t midi_in;

int i, j, p;
int pv;
int found;

int v[5], vo[5];                      // used for pot changed
int pin[]{32,37,39,38,36};            // pot gpio numbers

float my_vol;

unsigned long sw_last_milli[8];       // used for debounce
int sw_val[8];                        // used for debounce
int sw_pin[]{12,14,27,26,0,17,2,22};  // switch gpio numbers


uint8_t current_model, current_effect;
char *new_name;

unsigned int cmdsub;
SparkMessage msg;
SparkPreset preset;
SparkPreset presets[6];

char spark_amps[][STR_LEN]{"RolandJC120", "Twin", "ADClean", "94MatchDCV2", "Bassman", "AC Boost", "Checkmate",
                           "TwoStoneSP50", "Deluxe65", "Plexi", "OverDrivenJM45", // "OverDrivenLuxVerb",
                           "Bogner", "OrangeAD30", //"AmericanHighGain", 
                           "SLO100", "YJM100", "Rectifier",
                           "EVH", "SwitchAxeLead", "Invader", "BE101", "Acoustic", "AcousticAmpV2", //"FatAcousticV2", "FlatAcoustic", 
                           "GK800", "Sunny3000", "W600", "Hammer500"};

char spark_drives[][STR_LEN]{"Booster", //"DistortionTS9", //"Overdrive", 
                             "Fuzz", "ProCoRat", "BassBigMuff",
                             "GuitarMuff", // "MaestroBassmaster", 
                             "SABdriver"};
char spark_compressors[][STR_LEN]{"LA2AComp", "BlueComp", "Compressor", "BassComp" }; // "BBEOpticalComp"};


char spark_modulations[][STR_LEN]{"Tremolo", "ChorusAnalog", "Flanger", "Phaser", "Vibrato01", "UniVibe",
                                  "Cloner", "MiniVibe", "Tremolator"}; //"TremoloSquare"};
char spark_delays[][STR_LEN]{"DelayMono", 
                             //"DelayEchoFilt", 
                             "VintageDelay"}; 
                             //"DelayReverse",
                             //"DelayMultiHead", 
                             //"DelayRe201"
                             
char* effects[7];

void printit(char *str) {
  if (scr_line >= 7) {
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, PGM_NAME);
    scr_line = 1;
  }
  Heltec.display->drawString(0,scr_line *8, str);
  Heltec.display->display();
  scr_line++;
}

void dump_preset(SparkPreset preset) {
  int i,j;

  Serial.print(preset.curr_preset); Serial.print(" ");
  Serial.print(preset.preset_num); Serial.print(" ");
  Serial.print(preset.Name); Serial.print(" ");

  Serial.println(preset.Description);

  for (j=0; j<7; j++) {
    Serial.print("    ");
    Serial.print(preset.effects[j].EffectName); Serial.print(" ");
    if (preset.effects[j].OnOff == true) Serial.print(" On "); else Serial.print (" Off ");
    for (i = 0; i < preset.effects[j].NumParameters; i++) {
      Serial.print(preset.effects[j].Parameters[i]); Serial.print(" ");
    }
    Serial.println();
  }
}


void setup() {

//  HWSerial.begin(HW_BAUD, SERIAL_8N1, 33, 32);

  Heltec.begin(true /*DisplayEnable Enable*/, false /*LoRa Enable*/, true /*Serial Enable*/);

  // Serial MIDI setup - set up and flush input
  
  midi = new HardwareSerial(1); 
  // 34 is rx, - is tx
  midi->begin(31250, SERIAL_8N1, HW_SERIAL2_RX, -1);
  while (midi->available())
    b = midi->read(); 
  
  pinMode(CONNECT_PIN, OUTPUT);
  digitalWrite(CONNECT_PIN, LOW);

  for (i = 0; i < 8; i++)
    pinMode(sw_pin[i], INPUT_PULLUP);

  
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, PGM_NAME);
  Heltec.display->display();
  scr_line = 1;

  start_bt(true);         // true uses BLE
  connect_to_spark();
  
  start_ser();
  digitalWrite(CONNECT_PIN, HIGH);
  printit("Connected");
  app_connected = false;
  
  pre = 0;
  current_model = 0;
  current_effect = 3;

  for (i = 0; i < 5; i++) {
    v[i]=analogRead(pin[i]) / 64;
    vo[i]=v[i];
  }

  for (i = 0; i < 8; i++) {
    sw_last_milli[i] = 0;
    sw_val[i] = HIGH;
  }
  my_vol = 0.1;
  
  effects[0] = "bias.noisegate";
  effects[1] = "LA2ACompBlueCompCompressorBassCompBBEOpticalComp";
  effects[2] = "BoosterDistortionTS9OverdriveFuzzProCoRatBassBigMuffGuitarMuffMaestroBassmasterSABdriver";
  effects[3] = "RolandJC120TwinADClean94MatchDCV2BassmanAC BoostCheckmateTwoStoneSP50Deluxe65PlexiOverDrivenJM45OverDrivenLuxVerb"
               "BognerOrangeAD30AmericanHighGainSLO100YJM100RectifierEVHSwitchAxeLeadInvaderBE101AcousticAcousticAmpV2FatAcousticV2"
               "FlatAcousticGK800Sunny3000W600Hammer500";
  effects[4] = "TremoloChorusAnalogFlangerPhaserVibrato01UniVibeClonerMiniVibeTremolatorTremoloSquare";
  effects[5] = "DelayMonoDelayEchoFiltVintageDelayDelayReverseDelayMultiHeadDelayRe201";
  effects[6] = "bias.reverb";
}


  
void loop() {

  spark_io.process();
  app_io.process();

  // Messages from the amp
  
  if (spark_io.get_message(&cmdsub, &msg, &preset)) { //there is something there
    Serial.print("From Spark: ");
    Serial.println(cmdsub, HEX);
    sprintf(str, "< %4.4x", cmdsub);
    printit(str);
    
    if (cmdsub == 0x0301) {
      p = preset.preset_num;
      j = preset.curr_preset;
      if (p == 0x7f)       
        p = 4;
      if (j == 0x01)
        p = 5;
      presets[p] = preset;
      dump_preset(preset);
    }

    if (cmdsub == 0x0306) {
      strcpy(presets[5].effects[3].EffectName, msg.str2);
      Serial.print("Change to amp model ");
      Serial.println(presets[5].effects[3].EffectName);
    }

    if (cmdsub == 0x0337) {
      Serial.print("Change model parameter ");
      Serial.print(msg.str1);
      Serial.print(" ");
      Serial.print(msg.param1);   
      Serial.print(" ");   
      Serial.println(msg.val);
    }
    
    if (cmdsub == 0x0338) {
      selected_preset = msg.param2;
      presets[5] = presets[selected_preset];
      Serial.print("Change to preset: ");
      Serial.println(selected_preset, HEX);
    }      
    
    if (cmdsub == 0x0327) {
      selected_preset = msg.param2;
      if (selected_preset == 0x7f) 
        selected_preset=4;
      presets[selected_preset] = presets[5];
      Serial.print("Store in preset: ");
      Serial.println(selected_preset, HEX);
    }

    if (cmdsub == 0x0310) {
      selected_preset = msg.param2;
      j = msg.param1;
      if (selected_preset == 0x7f) 
        selected_preset = 4;
      if (j == 0x01) 
        selected_preset = 5;
      presets[5] = presets[selected_preset];
      Serial.print("Hadware preset is: ");
      Serial.println(selected_preset, HEX);
    }
  }

  // Messages from the app

  if (app_io.get_message(&cmdsub, &msg, &preset)) { //there is something there
    Serial.print("To Spark: ");
    Serial.println(cmdsub, HEX);
    sprintf(str, "> %4.4x", cmdsub);
    printit(str);

    if (cmdsub == 0x022f) 
      app_connected = true;
    
    if (cmdsub == 0x0104) {
      Serial.print("Change model parameter ");
      Serial.print(msg.str1);
      Serial.print(" ");
      Serial.print(msg.param1);   
      Serial.print(" ");   
      Serial.println(msg.val);
    }    
    
    if (cmdsub == 0x0101) {
      p = preset.preset_num;
      if (p == 0x7f) 
        p = 4;
      presets[p]=preset;  
      Serial.print("Send new preset: ");
      Serial.println(p, HEX);      
    }

    if (cmdsub == 0x0138) {
      if (msg.param1 == 0x01) DEBUG("Got a change to preset 0x100 from the app");
      
      selected_preset = msg.param2;
      if (selected_preset == 0x7f) 
        selected_preset=4;
      presets[5] = presets[selected_preset];
      Serial.print("Change to preset: ");
      Serial.println(selected_preset, HEX);
    }

    if (cmdsub == 0x0106) {
      found = -1;
      for (i = 1; found == -1 && i <= 5; i++) {
        if (strstr(effects[i], msg.str1) != NULL) {
          Serial.println (i);
          found = i;
        }
      }
      if (found >= 0) {
        strcpy(presets[5].effects[found].EffectName, msg.str2);
        Serial.print("Change to new effect ");
        Serial.print(msg.str1);
        Serial.print(" ");
        Serial.println(msg.str2);
      }
    }
  }   

  // Reaction to switch presses and serial MIDI input


  // get MIDI input
  midi_in = 0;
  if (midi->available()) {
    b = midi->read();
    if (b == 0x90) {
      midi_in = midi->read();
      b = midi->read();
    }
  }

  // get switch input
  for (i = 0; i < 8; i++) {
    sw_val[i] = HIGH;
    if (sw_last_milli[i] != 0) {
      if (millis() - sw_last_milli[i] > SWITCH_DEBOUNCE) 
        sw_last_milli[i] = 0;
    }
    if (sw_last_milli[i] == 0) {
      pv = digitalRead(sw_pin[i]);
      sw_val[i] = pv;
      if (pv == LOW) sw_last_milli[i] = millis();
    }
  }

  if (sw_val[0] == LOW || midi_in == 0x31) {      
    // next preset
    spark_io.change_hardware_preset(pre);
    if (app_connected) 
      app_io.change_hardware_preset(pre);
    presets[5] = presets[pre];
    printit("<> Hardware chg");
    pre++;
    if (pre > 3) pre = 0;
  }
  else if (sw_val[1] == LOW || midi_in == 0x32) {
    my_vol+= 0.1;
    if (my_vol > 1.0)
      my_vol = 0.1;

    spark_io.change_effect_parameter(presets[5].effects[3].EffectName, 0, my_vol);
    if (app_connected)
      app_io.change_effect_parameter(presets[5].effects[3].EffectName, 0, my_vol);
      
  }
  else if (sw_val[3] == LOW || midi_in == 0x34) {
    // previous effect
    if (current_effect == 6) 
      current_effect = 0;
    else 
      current_effect++;

    sprintf(str, "Next effect %1.1x", current_effect);
    printit(str);
      
    current_model = 0;
  }
  else if (sw_val[2] == LOW || midi_in == 0x33) {
    // next effect
    if (current_effect == 0) 
      current_effect = 6;
    else 
      current_effect--;

    sprintf(str, "Previous effect %1.1x", current_effect);
    printit(str);

    current_model = 0;
  }
  else if (sw_val[5] == LOW || midi_in == 0x36) {
    // next type
    switch (current_effect) {
      case 0:
        // noisegate
        // nothing to change for noisegate
        break;
      case 1:
        // compressor
        current_model++; 
        if (current_model > 3) current_model = 0;
        new_name = spark_compressors[current_model];
        
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected) 
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 2: 
        // drive         
        current_model++; 
        if (current_model > 5) current_model = 0;
        new_name = spark_drives[current_model];
        
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected)  
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 3:
        // amp
        current_model++;
        if (current_model > 25) current_model = 0;
        new_name = spark_amps[current_model];
        
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected) 
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 4:
        // mod
        current_model++;
        if (current_model > 8) current_model = 0;
        new_name = spark_modulations[current_model];
        
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected) 
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 5:
        // delay
        current_model++;
        if (current_model > 1) current_model = 0;
        new_name = spark_delays[current_model];
         
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected) 
           app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 6: 
        // reverb
        // nothing to change for reverb
        break;
    }
  } 
  else if (sw_val[4] == LOW || midi_in == 0x35) {
    // next type
    switch (current_effect) {
      case 0:
        // noisegate
        // nothing to change for noisegate
        break;
      case 1:
        // compressor
        if (current_model == 0) 
          current_model = 3;
        else
          current_model--;
          
        new_name = spark_compressors[current_model];
         
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected)
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 2: 
        // drive         
        if (current_model == 0) 
          current_model = 5;
        else
          current_model--;
          
        new_name = spark_drives[current_model];
        
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected)
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 3:
        // amp
        if (current_model == 0) 
          current_model = 25;
        else
          current_model--;
            
        new_name = spark_amps[current_model];
          
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected)
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 4:
        // mod
        if (current_model == 0) 
          current_model = 8;
        else
          current_model--;
            
        new_name = spark_modulations[current_model];
          
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected)
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 5:
        // delay
        if (current_model == 0) 
          current_model = 1;
        else
          current_model--;
            
        new_name = spark_delays[current_model];
          
        spark_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        if (app_connected)
          app_io.change_effect(presets[5].effects[current_effect].EffectName, new_name);
        strcpy(presets[5].effects[current_effect].EffectName, new_name);
        Serial.println(new_name);
        break;
      case 6: 
        // reverb
        // nothing to change for reverb
        break;
     }
  } 

    

  // update for potentiometers - current_model in current_effect
  for (i = 0; i < 5; i++) {
    v[i] = analogRead(pin[i]) / 64;
    if (abs(v[i] - vo[i]) > 2) {
      vo[i] = v[i];
      spark_io.change_effect_parameter(presets[5].effects[current_effect].EffectName, i, v[i] / 64.0);
      if (app_connected)
        app_io.change_effect_parameter(presets[5].effects[current_effect].EffectName, i, v[i] / 64.0);
      printit("<> Param chg");           
    }
  }

}
