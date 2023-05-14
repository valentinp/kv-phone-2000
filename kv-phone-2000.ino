#include "RotaryDial.h"
#include <TimeLib.h>
#include <Bounce.h>
#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include <SerialFlash.h>

#define ROTARY_DIAL_PULSE_PIN 1 
#define ROTARY_DIAL_TIMEOUT 3000 //If no numbers are dialed for this period, newline is sent
#define RECORD_LED_PIN 0

AudioInputI2S            i2s2;           
AudioAnalyzePeak         peak1;          
AudioRecordQueue         queue1;         
AudioPlaySdRaw           playRaw1;       
AudioOutputI2S           i2s1;           
AudioConnection          patchCord1(i2s2, 0, queue1, 0);
AudioConnection          patchCord2(i2s2, 0, peak1, 0);
AudioConnection          patchCord3(playRaw1, 0, i2s1, 0);
AudioConnection          patchCord4(playRaw1, 0, i2s1, 1);
AudioControlSGTL5000     sgtl5000_1; 


// Teensy audio break out board (can also use AUDIO_INPUT_LINEIN)
const int myInput = AUDIO_INPUT_MIC;

// Use these with the Teensy 3.5 & 3.6 & 4.1 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11  // not actually used
#define SDCARD_SCK_PIN   13  // not actually used

// The file where data is recorded
char filename[24];
File frec;

// Remember which mode we're doing
int mode = 0;  // 0=stopped, 1=recording, 2=playing

void setup() {
  Serial.begin(9600);

  pinMode(RECORD_LED_PIN, OUTPUT);

  RotaryDial::setup(ROTARY_DIAL_PULSE_PIN);

  // Synchronize clock to PC
  setSyncProvider(getTeensy3Time);
  
  // Define a callback that will assign the correct datetime for any file system operations
  // (i.e. saving a new audio recording onto the SD card)
  FsDateTime::setCallback(dateTime);

  // Audio connections require memory, and the record queue
  // uses this memory to buffer incoming audio.
  AudioMemory(60);

  // Enable the audio shield, select input, and enable output
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(myInput);
  sgtl5000_1.volume(0.75);
  sgtl5000_1.micGain(10);


  // Initialize the SD card
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here if no SD card, but print a message
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }

}

void loop() {
  if(RotaryDial::available()) {
    int digit = RotaryDial::read();
    Serial.print(digit);
    // Respond to rotary digits
    if (digit == 0) {
      Serial.println("Dialed 0...Recording!");
      if (mode == 2) stopPlaying();
      if (mode == 0) startRecording();
    }
    if (digit == 9) {
      Serial.println("Dialed 9...Stopping!");
      if (mode == 1) stopRecording();
      if (mode == 2) stopPlaying();
    }
    if (digit == 8) {
      Serial.println("Dialed 8...Playing!");
      if (mode == 1) stopRecording();
      if (mode == 0) startPlaying();
    }
  }
  // If we're playing or recording, carry on...
  if (mode == 1) {
    continueRecording();
  }
  if (mode == 2) {
    continuePlaying();
  }
}

// ======= audio

void startRecording() {
  getFilename();
  Serial.println("Recording into file " + String(filename));
  digitalWrite(RECORD_LED_PIN, HIGH); // turn on LED

  if (SD.exists(filename)) {
    // The SD library writes new data to the end of the
    // file, so to start a new recording, the old file
    // must be deleted before new data is written.
    SD.remove(filename);
  }
  frec = SD.open(filename, FILE_WRITE);
  if (frec) {
    queue1.begin();
    mode = 1;
  }
}

void continueRecording() {
  if (queue1.available() >= 2) {
    byte buffer[512];
    // Fetch 2 blocks from the audio library and copy
    // into a 512 byte buffer.  The Arduino SD library
    // is most efficient when full 512 byte sector size
    // writes are used.
    memcpy(buffer, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    memcpy(buffer+256, queue1.readBuffer(), 256);
    queue1.freeBuffer();
    // write all 512 bytes to the SD card
    //elapsedMicros usec = 0;
    frec.write(buffer, 512);
    // Uncomment these lines to see how long SD writes
    // are taking.  A pair of audio blocks arrives every
    // 5802 microseconds, so hopefully most of the writes
    // take well under 5802 us.  Some will take more, as
    // the SD library also must write to the FAT tables
    // and the SD card controller manages media erase and
    // wear leveling.  The queue1 object can buffer
    // approximately 301700 us of audio, to allow time
    // for occasional high SD card latency, as long as
    // the average write time is under 5802 us.
    //Serial.print("SD write, us=");
    //Serial.println(usec);
  }
}

void stopRecording() {
  Serial.println("stopRecording");
  digitalWrite(RECORD_LED_PIN, LOW); // turn off LED
  queue1.end();
  if (mode == 1) {
    while (queue1.available() > 0) {
      frec.write((byte*)queue1.readBuffer(), 256);
      queue1.freeBuffer();
    }
    frec.close();
  }
  mode = 0;
}


void startPlaying() {
  Serial.println("Starting to play file" + String(filename));
  playRaw1.play(filename);
  mode = 2;
}

void continuePlaying() {
  if (!playRaw1.isPlaying()) {
    playRaw1.stop();
    mode = 0;
  }
}

void stopPlaying() {
  Serial.println("stopPlaying");
  if (mode == 2) playRaw1.stop();
  mode = 0;
}

// ======= misc helpers
void getFilename() {
  sprintf(filename, "%02d-%02d-%02d-%02d-%02d-%02d.raw",  hour(), minute(), second(), day(), month(), year());
}
time_t getTeensy3Time()
{
  return Teensy3Clock.get();
}

// Callback to assign timestamps for file system operations
void dateTime(uint16_t* date, uint16_t* time, uint8_t* ms10) {

  // Return date using FS_DATE macro to format fields.
  *date = FS_DATE(year(), month(), day());

  // Return time using FS_TIME macro to format fields.
  *time = FS_TIME(hour(), minute(), second());

  // Return low time bits in units of 10 ms.
  *ms10 = second() & 1 ? 100 : 0;
}