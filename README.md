# SparkIOBLE

Credit to Evgeniy Aslovskiy (copych/BLE_Spark_pedal on github) for showing how to get BLE incoporated into the SparkComms class.   


New version of SparkIO now using BLE (optional)   

Key differences from SparkIO:   

- There is no class for SparkComms - it is now just a library   
- You need to call start_bt with a boolean parameter   
      start_bt(false)- false uses bluetooth serial   
      start_bt(true) - true uses BLE   



```
SparkAppIO app_io(true);         // true in the parameter sends all messages from serial to the amp (via bt)  - passthru
SparkIO spark_io(true);          // true in the parameter sends all messages from the amp (via bt) to serial - passthru

unsigned int cmdsub;
SparkPreset preset;
SparkMessage msg;

void setup() 
{
  ...


  start_bt(true);     // start bt using BLE
  connect_to_spark(); // connect bt to amp
  start_ser();        // start serial and assume another ESP 32 is linked to the app
  
  ...
}


```
To convert any existing code, remove the bits commented out below, and add a boolean parameter to start_bt()

```
SparkAppIO app_io(true);         // true in the parameter sends all messages from serial to the amp (via bt)  - passthru
SparkIO spark_io(true);          // true in the parameter sends all messages from the amp (via bt) to serial - passthru
/* SparkComms spark_comms; */

unsigned int cmdsub;
SparkPreset preset;
SparkMessage msg;

void setup() 
{
  ...

  /* spark_io.comms = &spark_comms; */  // link to the comms class
  /* app_io.comms = &spark_comms;   */  // link to the comms class

  /* spark_comms. */   start_bt(    true    );         // start bt
  /* spark_comms. */   connect_to_spark();             // connect bt to amp
  /* spark_comms. */   start_ser();                    // start serial and assume another ESP 32 is linked to the app
  
  ...
}
```
