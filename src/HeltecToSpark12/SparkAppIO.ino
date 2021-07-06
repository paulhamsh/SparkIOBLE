#include "Spark.h"
#include "SparkAppIO.h"

/*  SparkAppIO
 *  
 *  SparkAppIO handles communication to and from the Positive Grid Spark App over bluetooth for ESP32 boards
 *  
 *  From the programmers perspective, you create and read two formats - a Spark Message or a Spark Preset.
 *  The Preset has all the data for a full preset (amps, effects, values) and can be sent or received from the amp.
 *  The Message handles all other changes - change amp, change effect, change value of an effect parameter, change hardware preset and so on
 *  
 *  The class is initialized by creating an instance such as:
 *  
 *  SparkClass sp;
 *  
 *  Conection is handled with the two commands:
 *  
 *    sp.start_bt();
 *    
 *  
 *  Messages and presets to and from the amp are then queued and processed.
 *  The essential thing is the have the process() function somewhere in loop() - this handles all the processing of the input and output queues
 *  
 *  loop() {
 *    ...
 *    sp.process()
 *    ...
 *    do something
 *    ...
 *  }
 * 
 * Sending functions:
 *     void create_preset(SparkPreset *preset);    
 *     void get_serial();    
 *     void turn_effect_onoff(char *pedal, bool onoff);    
 *     void change_hardware_preset(uint8_t preset_num);    
 *     void change_effect(char *pedal1, char *pedal2);    
 *     void change_effect_parameter(char *pedal, int param, float val);
 *     
 *     These all create a message or preset to be sent to the amp when they reach the front of the 'send' queue
 *  
 * Receiving functions:
 *     bool get_message(unsigned int *cmdsub, SparkMessage *msg, SparkPreset *preset);
 * 
 *     This receives the front of the 'received' queue - if there is nothing it returns false
 *     
 *     Based on whatever was in the queue, it will populate fields of the msg parameter or the preset parameter.
 *     Eveything apart from a full preset sent from the amp will be a message.
 *     
 *     You can determine which by inspecting cmdsub - this will be 0x0301 for a preset.
 *     
 *     Other values are:
 *     
 *     cmdsub       str1                   str2              val           param1             param2                onoff
 *     0123         amp serial #
 *     0137         effect name                              effect val    effect number
 *     0106         old effect             new effect
 *     0138                                                                0                  new hw preset (0-3)
 * 
 * 
 * 
 */

//
// SparkAppIO class
//

SparkAppIO::SparkAppIO(bool passthru) {
  pass_through = passthru;
  rb_state = 0;
  rc_state = 0;
  oc_seq = 0x40;
  ob_ok_to_send = true;
  ob_last_sent_time = millis();

  ser_pos = 0;
  ser_state = 0;
  ser_len = -1;
}

SparkAppIO::~SparkAppIO() {
  
}




// 
// Main processing routine
//

void SparkAppIO::process() 
{
  // process inputs
  process_in_blocks();
  process_in_chunks();

  /*
  if (!in_message.is_empty()) {
    Serial.print("FROM SPARK ");
    in_message.dump2();
  }
  
  
  if (!out_message.is_empty()) {
    Serial.print("TO SPARK ");
    out_message.dump2();
  }

  
  if (!ob_ok_to_send && (millis() - ob_last_sent_time > 500)) {
    DEBUG("Timeout on send");
    ob_ok_to_send = true;
  }
*/

  // process outputs
  
  process_out_chunks();
  process_out_blocks();
}


//
// Routine to read the block from bluetooth and put into the in_chunk ring buffer
//

uint8_t chunk_header_to_spark[16]{0x01, 0xfe, 0x00, 0x00, 0x53, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void SparkAppIO::process_in_blocks() {
  uint8_t b;
  bool boo;

  while (ser_available()) {
    b = ser_read();

    // **** PASSTHROUGH OF SERIAL TO BLUETOOTH ****

    if (pass_through) {
      if (ser_state == 0 && b == 0x01) {
        ser_state = 1;
      }
      else if (ser_state == 1) {
        if (b == 0xfe) {
          ser_state = 2;
          ser_buf[0] = 0x01;
          ser_buf[1] = 0xfe;
          ser_pos = 2;
        }
        else 
          ser_state = 0;
      }
      else if (ser_state == 2) {
        if (ser_pos == 6) {
          ser_len = b;
        }
        ser_buf[ser_pos++] = b;
        if (ser_pos == ser_len) {
          bt_write(ser_buf, ser_pos);   
          ser_pos = 0;
          ser_len = -1; 
          ser_state = 0; 
        }
      }
      if (ser_pos > MAX_SER_BUFFER) {
        Serial.println("APPIO IO_PROCESS_IN_BLOCKS OVERRUN");
        while (true);
      }
    }
    
    // **** END PASSTHROUGH ****

    // check the 7th byte which holds the block length
    if (rb_state == 6) {
      rb_len = b - 16;
      rb_state++;
    }
    // check every other byte in the block header for a match to the header standard
    else if (rb_state > 0 && rb_state < 16) {
      if (b == chunk_header_to_spark[rb_state]) {
        rb_state++;
      }
      else {
        rb_state = 0;
        DEBUG("SparkAppIO bad block header");
      }
    } 
    // and once past the header just read the next bytes as defined by rb_len
    // store these to the chunk buffer
    else if (rb_state == 16) {
      in_chunk.add(b);
      rb_len--;
      if (rb_len == 0) {
        rb_state = 0;
        in_chunk.commit();
      }
    }
      
    // checking for rb_state 0 is done separately so that if a prior step finds a mismatch
    // and resets the state to 0 it can be processed here for that byte - saves missing the 
    // first byte of the header if that was misplaced
    
    if (rb_state == 0) 
      if (b == chunk_header_to_spark[0]) 
        rb_state++;
  }
}

//
// Routine to read chunks from the in_chunk ring buffer and copy to a in_message msgpack buffer
//

void SparkAppIO::process_in_chunks() {
  uint8_t b;
  bool boo;
  unsigned int len;
  uint8_t len_h, len_l;

  while (!in_chunk.is_empty()) {               // && in_message.is_empty()) {  -- no longer needed because in_message is now a proper ringbuffer
    boo = in_chunk.get(&b);
    if (!boo) DEBUG("Chunk is_empty was false but the buffer was empty!");

    switch (rc_state) {
      case 1:
        if (b == 0x01) 
          rc_state++; 
        else 
          rc_state = 0; 
        break;
      case 2:
        rc_seq = b; 
        rc_state++; 
        break;
      case 3:
        rc_checksum = b;
        rc_state++; 
        break;
      case 4:
        rc_cmd = b; 
        rc_state++; 
        break;
      case 5:
        rc_sub = b; 
        rc_state = 10;
/*
        // flow control for blocking sends - put here in case we want to check rc_sub too
        if (rc_cmd == 0x04 && rc_sub == 0x01) {
          ob_ok_to_send = true;
          DEBUG("Unblocked");
        }
*/        
        // set up for the main data loop - rc_state 10
        rc_bitmask = 0x80;
        rc_calc_checksum = 0;
        rc_data_pos = 0;
        
        // check for multi-chunk
        if (rc_cmd == 1 && rc_sub == 1) 
          rc_multi_chunk = true;
        else {
          rc_multi_chunk = false;
          in_message_bad = false;
          in_message.add(rc_cmd);
          in_message.add(rc_sub);
          in_message.add(0);
          in_message.add(0);
        }
        break;
      case 10:                    // the main loop which ends on an 0xf7
        if (b == 0xf7) {
          if (rc_calc_checksum != rc_checksum) 
            in_message_bad = true;
          rc_state = 0;
          if (!rc_multi_chunk || (rc_this_chunk == rc_total_chunks-1)) { //last chunk in message
            if (in_message_bad) {
              DEBUG("Bad message, dropped");
              in_message.drop();
            }
            else {
              len = in_message.get_len();
              uint_to_bytes(len, &len_h, &len_l);

              in_message.set_at_index(2, len_h);
              in_message.set_at_index(3, len_l);
              in_message.commit();
            }  
          }
        }
        else if (rc_bitmask == 0x80) { // if the bitmask got to this value it is now a new bits 
          rc_calc_checksum ^= b;
          rc_bits = b;
          rc_bitmask = 1;
        }
        else {
          rc_data_pos++;
          rc_calc_checksum ^= b;          
          if (rc_bits & rc_bitmask) 
            b |= 0x80;
          rc_bitmask *= 2;
          
          if (rc_multi_chunk && rc_data_pos == 1) 
            rc_total_chunks = b;
          else if (rc_multi_chunk && rc_data_pos == 2) {
            rc_last_chunk = rc_this_chunk;
            rc_this_chunk = b;
            if (rc_this_chunk == 0) {
              in_message_bad = false;
              in_message.add(rc_cmd);
              in_message.add(rc_sub);
              in_message.add(0);
              in_message.add(0);
            }
            else if (rc_this_chunk != rc_last_chunk+1)
              in_message_bad = true;
          }
          else if (rc_multi_chunk && rc_data_pos == 3) 
            rc_chunk_len = b;
          else {  
            in_message.add(b);             
          }
          
        };
        break;
    }

    // checking for rc_state 0 is done separately so that if a prior step finds a mismatch
    // and resets the state to 0 it can be processed here for that byte - saves missing the 
    // first byte of the header if that was misplaced
    
    if (rc_state == 0) {
      if (b == 0xf0) 
        rc_state++;
    }
  }
}

//// Routines to interpret the data

void SparkAppIO::read_byte(uint8_t *b)
{
  uint8_t a;
  in_message.get(&a);
  *b = a;
}   
   
void SparkAppIO::read_string(char *str)
{
  uint8_t a, len;
  int i;

  read_byte(&a);
  if (a == 0xd9) {
    read_byte(&len);
  }
  else if (a > 0xa0) {
    len = a - 0xa0;
  }
  else {
    read_byte(&a);
    if (a < 0xa1 || a >= 0xc0) DEBUG("Bad string");
    len = a - 0xa0;
  }

  if (len > 0) {
    // process whole string but cap it at STR_LEN-1
    for (i = 0; i < len; i++) {
      read_byte(&a);
      if (a<0x20 || a>0x7e) a=0x20; // make sure it is in ASCII range - to cope with get_serial 
      if (i < STR_LEN -1) str[i]=a;
    }
    str[i > STR_LEN-1 ? STR_LEN-1 : i]='\0';
  }
  else {
    str[0]='\0';
  }
}   

void SparkAppIO::read_prefixed_string(char *str)
{
  uint8_t a, len;
  int i;

  read_byte(&a); 
  read_byte(&a);

  if (a < 0xa1 || a >= 0xc0) DEBUG("Bad string");
  len = a-0xa0;

  if (len > 0) {
    for (i = 0; i < len; i++) {
      read_byte(&a);
      if (a<0x20 || a>0x7e) a=0x20; // make sure it is in ASCII range - to cope with get_serial 
      if (i < STR_LEN -1) str[i]=a;
    }
    str[i > STR_LEN-1 ? STR_LEN-1 : i]='\0';
  }
  else {
    str[0]='\0';
  }
}   

void SparkAppIO::read_float(float *f)
{
  union {
    float val;
    byte b[4];
  } conv;   
  uint8_t a;
  int i;

  read_byte(&a);  // should be 0xca
  if (a != 0xca) return;

  // Seems this creates the most significant byte in the last position, so for example
  // 120.0 = 0x42F00000 is stored as 0000F042  
   
  for (i=3; i>=0; i--) {
    read_byte(&a);
    conv.b[i] = a;
  } 
  *f = conv.val;
}

void SparkAppIO::read_onoff(bool *b)
{
  uint8_t a;
   
  read_byte(&a);
  if (a == 0xc3)
    *b = true;
  else // 0xc2
    *b = false;
}

// The functions to get the message

bool SparkAppIO::get_message(unsigned int *cmdsub, SparkMessage *msg, SparkPreset *preset)
{
  uint8_t cmd, sub, len_h, len_l;
  unsigned int len;
  unsigned int cs;
   
  uint8_t junk;
  int i, j;
  uint8_t num;

  if (in_message.is_empty()) return false;

  read_byte(&cmd);
  read_byte(&sub);
  read_byte(&len_h);
  read_byte(&len_l);
  
  bytes_to_uint(len_h, len_l, &len);
  bytes_to_uint(cmd, sub, &cs);

  *cmdsub = cs;
  switch (cs) {
    // 0x02 series - requests
    // get preset information
    case 0x0201:
      read_byte(&msg->param1);
      read_byte(&msg->param2);
      for (i=0; i < 30; i++) read_byte(&junk); // 30 bytes of 0x00
      break;            
    // get current hardware preset number - this is a request with no payload
    case 0x0210:
      break;
    // get amp name - no payload
    case 0x0211:
      break;
    // get name - this is a request with no payload
    case 0x0221:
      break;
    // get serial number - this is a request with no payload
    case 0x0223:
      break;
    // the UNKNOWN command - 0x0224 00 01 02 03
    case 0x0224:
      // the data is a fixed array of four bytes (0x94 00 01 02 03)
      read_byte(&junk);
      read_byte(&msg->param1);
      read_byte(&msg->param2);
      read_byte(&msg->param3);
      read_byte(&msg->param4);
      break;
    // get firmware version - this is a request with no payload
    case 0x022f:
      break;
    // 0x01 series - instructions
    // change effect parameter
    case 0x0104:
      read_string(msg->str1);
      read_byte(&msg->param1);
      read_float(&msg->val);
      break;
    // change effect model
    case 0x0106:
      read_string(msg->str1);
      read_string(msg->str2);
      break;
    // enable / disable effecct
    case 0x0115:
      read_string(msg->str1);
      read_onoff(&msg->onoff);
      break;    
    // change preset to 0-3, 0x7f
    case 0x0138:
      read_byte(&msg->param1);
      read_byte(&msg->param2);
      break;
    // send whole new preset to 0-3, 0x7f  
    case 0x0101:
      read_byte(&junk);
      read_byte(&preset->preset_num);
      read_string(preset->UUID); 
      read_string(preset->Name);
      read_string(preset->Version);
      read_string(preset->Description);
      read_string(preset->Icon);
      read_float(&preset->BPM);

      for (j=0; j<7; j++) {
        read_string(preset->effects[j].EffectName);
        read_onoff(&preset->effects[j].OnOff);
        read_byte(&num);
        preset->effects[j].NumParameters = num - 0x90;
        for (i = 0; i < preset->effects[j].NumParameters; i++) {
          read_byte(&junk);
          read_byte(&junk);
          read_float(&preset->effects[j].Parameters[i]);
        }
      }
      read_byte(&preset->chksum);  
      break;
    default:
      Serial.print("Unprocessed message SparkAppIO ");
      Serial.print (cs, HEX);
      Serial.print(":");
      for (i = 0; i < len - 4; i++) {
        read_byte(&junk);
        Serial.print(junk, HEX);
        Serial.print(" ");
      }
      Serial.println();
  }

  return true;
}

    
//
// Output routines
//


void SparkAppIO::start_message(int cmdsub)
{
  om_cmd = (cmdsub & 0xff00) >> 8;
  om_sub = cmdsub & 0xff;

  // THIS IS TEMPORARY JUST TO SHOW IT WORKS!!!!!!!!!!!!!!!!
  //sp.out_message.clear();

  out_message.add(om_cmd);
  out_message.add(om_sub);
  out_message.add(0);      // placeholder for length
  out_message.add(0);      // placeholder for length
}


void SparkAppIO::end_message()
{
  unsigned int len;
  uint8_t len_h, len_l;
  
  len = out_message.get_len();
  uint_to_bytes(len, &len_h, &len_l);
  
  out_message.set_at_index(2, len_h);   
  out_message.set_at_index(3, len_l);
  out_message.commit();
}


void SparkAppIO::write_byte(byte b)
{
  out_message.add(b);
}

void SparkAppIO::write_prefixed_string(const char *str)
{
  int len, i;

  len = strnlen(str, STR_LEN);
  write_byte(byte(len));
  write_byte(byte(len + 0xa0));
  for (i=0; i<len; i++)
    write_byte(byte(str[i]));
}

void SparkAppIO::write_string(const char *str)
{
  int len, i;

  len = strnlen(str, STR_LEN);
  write_byte(byte(len + 0xa0));
  for (i=0; i<len; i++)
    write_byte(byte(str[i]));
}      
  
void SparkAppIO::write_long_string(const char *str)
{
  int len, i;

  len = strnlen(str, STR_LEN);
  write_byte(byte(0xd9));
  write_byte(byte(len));
  for (i=0; i<len; i++)
    write_byte(byte(str[i]));
}

void SparkAppIO::write_float (float flt)
{
  union {
    float val;
    byte b[4];
  } conv;
  int i;
   
  conv.val = flt;
  // Seems this creates the most significant byte in the last position, so for example
  // 120.0 = 0x42F00000 is stored as 0000F042  
   
  write_byte(0xca);
  for (i=3; i>=0; i--) {
    write_byte(byte(conv.b[i]));
  }
}

void SparkAppIO::write_onoff (bool onoff)
{
  byte b;

  if (onoff)
  // true is 'on'
    b = 0xc3;
  else
    b = 0xc2;
  write_byte(b);
}

//
//
//

void SparkAppIO::change_effect_parameter (char *pedal, int param, float val)
{
   start_message (0x0337);
   write_prefixed_string (pedal);
   write_byte (byte(param));
   write_float(val);
   end_message();
}


void SparkAppIO::change_effect (char *pedal1, char *pedal2)
{
   start_message (0x0306);
   write_prefixed_string (pedal1);
   write_prefixed_string (pedal2);
   end_message();
}

void SparkAppIO::change_hardware_preset (uint8_t preset_num)
{
   // preset_num is 0 to 3

   start_message (0x0338);
   write_byte (0);
   write_byte (preset_num);     
   end_message();  
}

void SparkAppIO::turn_effect_onoff (char *pedal, bool onoff)
{
   start_message (0x0315);
   write_prefixed_string (pedal);
   write_onoff (onoff);
   end_message();
}

void SparkAppIO::save_hardware_preset(uint8_t preset_num)
{
   start_message (0x0327);
   write_byte (0);
   write_byte (preset_num);  
   end_message();
}

void SparkAppIO::create_preset(SparkPreset *preset)
{
  int i, j, siz;

  start_message (0x0301);

  write_byte (0x00);
  write_byte (preset->preset_num);   
  write_long_string (preset->UUID);
  write_string (preset->Name);
  write_string (preset->Version);
  if (strnlen (preset->Description, STR_LEN) > 31)
    write_long_string (preset->Description);
  else
    write_string (preset->Description);
  write_string(preset->Icon);
  write_float (preset->BPM);

   
  write_byte (byte(0x90 + 7));       // always 7 pedals

  for (i=0; i<7; i++) {
      
    write_string (preset->effects[i].EffectName);
    write_onoff(preset->effects[i].OnOff);

    siz = preset->effects[i].NumParameters;
    write_byte ( 0x90 + siz); 
      
    for (j=0; j<siz; j++) {
      write_byte (j);
      write_byte (byte(0x91));
      write_float (preset->effects[i].Parameters[j]);
    }
  }
  write_byte (preset->chksum);  
  end_message();
}

//
//
//

void SparkAppIO::out_store(uint8_t b)
{
  uint8_t bits;
  
  if (oc_bit_mask == 0x80) {
    oc_bit_mask = 1;
    oc_bit_pos = out_chunk.get_pos();
    out_chunk.add(0);
  }
  
  if (b & 0x80) {
    out_chunk.set_bit_at_index(oc_bit_pos, oc_bit_mask);
    oc_checksum ^= oc_bit_mask;
  }
  out_chunk.add(b & 0x7f);
  oc_checksum ^= (b & 0x7f);

  oc_len++;

  /*
  if (oc_bit_mask == 0x40) {
    out_chunk.get_at_index(oc_bit_pos, &bits);
    oc_checksum ^= bits;    
  }
*/  
  oc_bit_mask *= 2;
}


void SparkAppIO::process_out_chunks() {
  int i, j, len;
  int checksum_pos;
  uint8_t b;
  uint8_t len_h, len_l;

  uint8_t num_chunks, this_chunk, this_len;
 
  while (!out_message.is_empty()) {
    out_message.get(&oc_cmd);
    out_message.get(&oc_sub);
    out_message.get(&len_h);
    out_message.get(&len_l);
    bytes_to_uint(len_h, len_l, &oc_len);
    len = oc_len -4;

    if (len > 0x19) { //this is a multi-chunk message for amp to app (max 0x19 data)
      num_chunks = int(len / 0x19) + 1;
      for (this_chunk=0; this_chunk < num_chunks; this_chunk++) {
       
        // create chunk header
        out_chunk.add(0xf0);
        out_chunk.add(0x01);
        if (oc_sub == 0x01)        // asked for a preset
          out_chunk.add(rc_seq);   // last sequence received
        else {
          out_chunk.add(oc_seq);
          oc_seq++;
          if (oc_seq > 0x7f) oc_seq = 0x40;
        }
        
        checksum_pos = out_chunk.get_pos();
        out_chunk.add(0); // checksum
        
        out_chunk.add(oc_cmd);
        out_chunk.add(oc_sub);

        if (num_chunks == this_chunk+1) 
          this_len = len % 0x19;            
        else 
          this_len = 0x19;                  

        oc_bit_mask = 0x80;
        oc_checksum = 0;
        
        // create chunk sub-header          
        out_store(num_chunks);
        out_store(this_chunk);
        out_store(this_len);
        
        for (i = 0; i < this_len; i++) {
          out_message.get(&b);
          out_store(b);
        }
        out_chunk.set_at_index(checksum_pos, oc_checksum);        
        out_chunk.add(0xf7);
      }
    } 
    else { 
    // create chunk header
      out_chunk.add(0xf0);
      out_chunk.add(0x01);
      out_chunk.add(oc_seq);

      checksum_pos = out_chunk.get_pos();
      out_chunk.add(0); // checksum

      out_chunk.add(oc_cmd);
      out_chunk.add(oc_sub);

      oc_bit_mask = 0x80;
      oc_checksum = 0;
      for (i = 0; i < len; i++) {
        out_message.get(&b);
        out_store(b);
      }
     out_chunk.set_at_index(checksum_pos, oc_checksum);        
     out_chunk.add(0xf7);
    }
    out_chunk.commit();
  }
}

void SparkAppIO::process_out_blocks() {
  int i;
  int len;
  uint8_t b;  
  uint8_t cmd, sub;

  while (!out_chunk.is_empty()) {
    ob_pos = 16;
  
    out_block[0]= 0x01;
    out_block[1]= 0xfe;  
    out_block[2]= 0x00;    
    out_block[3]= 0x00;
    out_block[4]= 0x41;
    out_block[5]= 0xff;
    out_block[6]= 0x00;
    for (i=7; i<16;i++) 
      out_block[i]= 0x00;
    
    b = 0;
    while (ob_pos < 0x6a && !out_chunk.is_empty()) {
      out_chunk.get(&b);
      out_block[ob_pos++] = b;
    }
    out_block[6] = ob_pos;
/*
    for (i=0; i<ob_pos;i++) {
      if (out_block[i]<16) Serial.print("0");
      Serial.print(out_block[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
*/    
    ser_write(out_block, ob_pos);
    delay(50);
  }
}
