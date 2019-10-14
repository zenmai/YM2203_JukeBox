//
// * Arduino nano Pin to YM2203 Map
//     DATA[7..0] = D7 D6 D5 D4 A3 D2 A1 A0 (PD7 PD6 PD5 PD4 PC3 PD2 PC1 PC0)
//            /WR = A4 (PC4)
//             A0 = A5 (PC5)
// 4MHz Clock Out = D3 (OC2B,PD3)
//
// * Arduino nano Pin to microSD Card Reader(Catalex)
//   CS = D10 (SS)
//  SCK = D13 (SCK)
// MOSI = D11 (MOSI)
// MISO = D12 (MISO)
// 
// * Touch A/D Switch
//         Next Music = A6 (ADC6)
// Next Album(Folder) = A7 (ADC7)
// ADC Capacitance Discharge = D8
//

#include <SPI.h>
#include <SD.h>

#define PIN_TOUCH_DISCHARGE 8
#define FLG_NEXT_MUSIC 1
#define FLG_NEXT_ALBUM 2
const int chipSelect = 10;
uint8_t control_flg;
uint32_t master_time;
uint16_t sw_next_music, sw_next_album;
uint8_t sw_next_music_flg, sw_next_album_flg;
uint8_t sw_cnt;

void  wait(uint8_t loop)
{
  uint8_t wi;
  for(wi=0;wi<loop;wi++){
    // 16MHz  nop = @60nSec
    asm volatile("nop\n\t nop\n\t nop\n\t nop\n\t");
  }
}

void opn_write(uint8_t adr, uint8_t data) {
  PORTD = data;
  PORTC = (adr ? 0x20 : 0x00) | 0x14 | (data & 0x0b);
  PORTC &= ~0x10;  // low wr
  wait(4);
  PORTC |= 0x10;  // high wr
}

void opn_reg_write(uint8_t adr, uint8_t data) {
  opn_write(0,adr);
  wait(2);
  opn_write(1,data);
  wait(40);
}

void opn_mute(void){
  opn_reg_write(0x28,0x00);
  opn_reg_write(0x28,0x01);
  opn_reg_write(0x28,0x02);
  opn_reg_write(0x07,0x03f);
  opn_reg_write(0x08,0x00);
  opn_reg_write(0x09,0x00);
  opn_reg_write(0x0a,0x00);
}

// the setup function runs once when you press reset or power the board
void setup() {
  DDRC |= 0x3f;
  DDRD |= 0xfe;
  PORTC = 0x1b;
  PORTD = 0xfe;
  TCCR2A = 0x12;
  TCCR2B = 0x01;
  opn_reg_write(0x07,0xc0);
  OCR2A = OCR2B = 1;

  Serial.begin(9600);
  Serial.println("YM2203 S98 Player");
  sw_next_music = sw_next_album = 0;
  sw_next_music_flg = sw_next_album_flg = 0;
  pinMode(PIN_TOUCH_DISCHARGE,INPUT_PULLUP);
  control_flg = 0;
  sw_cnt = 10;
}

void adc_discharge(void){
    volatile int dumy;
    
    pinMode(PIN_TOUCH_DISCHARGE,OUTPUT);
    digitalWrite(PIN_TOUCH_DISCHARGE,LOW);
    wait(40);
    dumy = analogRead(A6);
    pinMode(PIN_TOUCH_DISCHARGE,INPUT);
    wait(40);
}

void sw_test(void){
  uint8_t pause_flg = 0;
  
  while(master_time > millis() || pause_flg){
    adc_discharge();
    sw_next_music += analogRead(A6);
    adc_discharge();
    sw_next_album += analogRead(A7);
    if(--sw_cnt==0){
      sw_cnt = 30;
      //Serial.print(sw_next_music,DEC);
      //Serial.print("\t");
      //Serial.print(sw_next_album,DEC);
      sw_next_music_flg = (sw_next_music_flg << 1) | ((sw_next_music > 500) ? 1 : 0);
      sw_next_album_flg = (sw_next_album_flg << 1) | ((sw_next_album > 1000) ? 1 : 0);
      //Serial.print("\t");
      //Serial.print(sw_next_music_flg,HEX);
      //Serial.print("\t");
      //Serial.println(sw_next_album_flg,HEX);
      sw_next_music = sw_next_album = 0;
      if(sw_next_music_flg == 0x0f && sw_next_album_flg == 0 && pause_flg == 0){
        control_flg = FLG_NEXT_MUSIC;
        return;
      }
      if(sw_next_album_flg == 0x0f && sw_next_music_flg == 0 && pause_flg == 0){
        control_flg = FLG_NEXT_ALBUM;
        return;
      }
      if(sw_next_music_flg == 0x0f && sw_next_album_flg){
        pause_flg ^= 0xff;
        master_time = millis();
        opn_mute();
      }
    }
  }
}

// the loop function runs over and over again forever
void loop() {
  char buff[32];
  uint8_t dd,i, sub_folder_enter_flg,skip_system_folder_flg ;
  uint32_t loop_point,timer_info,offset_dump_data;
  File root, entry, sub_entry;

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    while (1);
  }
  Serial.println("card initialized.");

  entry = root = SD.open("/");
  sub_folder_enter_flg = 0;
  skip_system_folder_flg = 1;
  
  while(1){
    if(sub_folder_enter_flg && control_flg == FLG_NEXT_ALBUM){
      Serial.println("skip dir");
      while(entry){
        entry = sub_entry.openNextFile();
      }
    }else if(sub_folder_enter_flg){
      entry = sub_entry.openNextFile();
    }else{
      entry = root.openNextFile();
    }
    control_flg = 0;
    
    if(!entry){
      if (sub_folder_enter_flg){
        Serial.println("not entry(1)");
        sub_folder_enter_flg = 0;
        sub_entry.close();
        continue;
      }else{
        Serial.println("not entry(0)");
        root = SD.open("/");
        skip_system_folder_flg = 1;
        continue;
      }
    }
    
    if (entry.isDirectory()){
      Serial.print("isdir:");
      Serial.println(entry.name());
      if(skip_system_folder_flg == 0){
        sub_entry = entry;
        sub_folder_enter_flg = 1;
      }
      skip_system_folder_flg = 0;
      continue;
    }
    
    if(sub_folder_enter_flg == 0){
      strcpy(buff,entry.name());
    } else {
      strcpy(buff,"/");
      strcat(buff,sub_entry.name());
      strcat(buff,"/");
      strcat(buff,entry.name());
    }
    
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    File dataFile = SD.open(buff);
    Serial.print("open:");
    Serial.println(buff);
    entry.close();
    
    // if the file is available, write to it:
    if (dataFile) {
      dataFile.seek(0x04);
      dataFile.read(&timer_info,4);
      if(timer_info==0)
        timer_info = 10;
      dataFile.seek(0x14);
      dataFile.read(&offset_dump_data,4);
      dataFile.read(&loop_point,4);
      dataFile.seek(offset_dump_data);
      master_time = millis();
      control_flg = 0;
      while (dataFile.available() && control_flg==0) {
        dd = dataFile.read();
        switch(dd){
          case 0xff:
            master_time += timer_info;
            sw_test();
            break;
          case 0xfe:
            dd = dataFile.read();
            master_time += timer_info*(2+dd);
            sw_test();
            break;
          case 0x00:
            dd = dataFile.read();
            opn_reg_write(dd,dataFile.read());
            break;
          case 0xfd:
            dataFile.seek(loop_point);
            Serial.println("loop");
            control_flg = 99;
            break;
          default:
            Serial.print("command err:");
            Serial.print(dd,HEX);
            while(1);
        }
        //Serial.write(dataFile.read());
      }
      dataFile.close();
      opn_mute();
      if(control_flg == 99)
        delay(2000);
    }
    // if the file isn't open, pop up an error:
    else {
      Serial.println("error opening datalog.txt");
    }
  }
}
