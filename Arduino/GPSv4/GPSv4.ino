#include <LibAPRS.h>
#include <SoftwareSerial.h>
#include <TinyGPS.h>
#include <U8glib.h>

// LibAPRS.h comes from http://unsigned.io and is used verbatim
// SOftwareSerial.h allows you to talk to the GPS while still maintaining
//    use of the other serial lines.
// TinyGPS.h just runs the GPS.
// U8glib.h runs the display.


/* This sample code demonstrates the normal use of a TinyGPS object.
   It requires the use of SoftwareSerial, and assumes that you have a
   4800-baud serial GPS device hooked up on pins 4(rx) and 3(tx).
   Thanks Mikal for the great start...-JCW
*/


TinyGPS gps;
SoftwareSerial ss(11, 12);

// set up the display
U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);

// basic setup for the APRS radio section.
#define ADC_REFERENCE REF_5V
#define OPEN_SQUELCH false

boolean gotPacket = false;
AX25Msg incomingPacket;
uint8_t *packetData;

// set up a couple variables for when we last transmitted.
// aprs spec is around 10 minutes or so (faster if your speed is faster)
// but probably not more than once per minute.
long lastUpdate=0;
int Updatedelay=600; //delay between packets in seconds.


static void smartdelay(unsigned long ms);
static void print_float(float val, float invalid, int len, int prec);
static void print_int(unsigned long val, unsigned long invalid, int len);
static void print_date(TinyGPS &gps);

//static void print_str(const char *str, int len);



void setup()
{
  // initialize serial output.
  // 9600 is mandatory.   the aprs stuff can't hack a faster rate
  // even though it doesn't directly use this.
  Serial.begin(9600);
  Serial.println(F("Sats HDOP Latitude  Longitude  Fix  Date       Time     Date Alt    "));
  Serial.println(F("          (deg)     (deg)      Age                      Age  (m)    "));
  Serial.println(F("--------------------------------------------------------------------"));

  // LED display to tell me when we are transmitting.
  pinMode(2,OUTPUT);
  
  // initialize serial to talk with the GPS.  
  // 9600 is mandatory.
  ss.begin(9600);
  
  // pin 8 is a pushbutton to allow the user to send a packet whenever they want.
  pinMode(8,INPUT);
  
  u8g.firstPage();  
  do {
    draw(200.00,1.01);
  } while( u8g.nextPage() );
  
 
  // Initialise APRS library - This starts the mode
  APRS_init(ADC_REFERENCE, OPEN_SQUELCH);
  
  // You must at a minimum configure your callsign and SSID
// you may also not use this software without a valid license
  APRS_setCallsign("CALLSIGN", 1);
 
  APRS_printSettings();
  Serial.print(F("Free RAM:     ")); Serial.println(freeMemory());

}


// converts float to char*
char *ftoa(char *a, double f, int precision)
{
 long p[] = {0,10,100,1000,10000,100000,1000000,10000000,100000000};
 
 char *ret = a;
 long heiltal = (long)f;
 itoa(heiltal, a, 10);
 while (*a != '\0') a++;
 *a++ = '.';
 long desimal = abs((long)((f - heiltal) * p[precision]));
 itoa(desimal, a, 10);
 return ret;
}


// redraw the screen.
void draw(float flat,float flon) {
  // graphic commands to redraw the complete screen should be placed here  
  //u8g.setFont(u8g_font_unifont);

  u8g.setFont(u8g_font_fub14);
  char z[12];
  ftoa((char*)&z,flat,4);
  u8g.drawStr(0,28,z);
  ftoa((char*)&z,flon,4);
  u8g.drawStr(0,56,z);
  
}


//main loop.   
// here we read GPS and occasionally send a packet.

void loop()
{
  float flat, flon;
  unsigned long age, date, time, chars = 0;
  unsigned short sentences = 0, failed = 0;
  byte buttonState;
  unsigned long gps_speed;
  int FlexibleDelay = Updatedelay;

  //print some various status  
  print_int(gps.satellites(), TinyGPS::GPS_INVALID_SATELLITES, 5);
  print_int(gps.hdop(), TinyGPS::GPS_INVALID_HDOP, 5);
  
  // get where we are.
  gps.f_get_position(&flat, &flon, &age);
  // speed comes in 100th's of a knot.  30 knots = 34.5 mph
  // 100 for gps_speed = 1 knot.
  gps_speed = gps.speed();
  
  // 10 mph = no alteration
  // 20 mph = /2
  // 30 mph = /3
  // 40 mph = /4 etc.
  if ((gps_speed > 100) && (gps_speed <20000)){
     FlexibleDelay = Updatedelay /(gps_speed / 1000) ;  
  } else {
      FlexibleDelay = Updatedelay; 
  }
    // more debug print.
  print_float(flat, TinyGPS::GPS_INVALID_F_ANGLE, 10, 6);
  print_float(flon, TinyGPS::GPS_INVALID_F_ANGLE, 11, 6);
  print_int(age, TinyGPS::GPS_INVALID_AGE, 5);
  
  print_date(gps);
  
  Serial.print(gps_speed);
  Serial.print(" ");
  Serial.print(FlexibleDelay);

if ((flat==TinyGPS::GPS_INVALID_F_ANGLE) || (flon==TinyGPS::GPS_INVALID_F_ANGLE)){
 // Serial.println("data is bad...");
  // redraw the screen
  u8g.firstPage();  
  do {
    draw(0,0);
  } while( u8g.nextPage() );
  
} else {

  // check for any new packets.
  smartdelay(250);
  processPacket();
  smartdelay(250);

  //redraw the screen.
  u8g.firstPage();  
  do {
    draw(flat,flon);
  } while( u8g.nextPage() );
  
  // check to see if the user pushed the button
  buttonState=digitalRead(8);
  
  if ((buttonState==1) || ((millis()-lastUpdate)/1000 > FlexibleDelay)||(Updatedelay==0)){
//  Serial.println();
//  Serial.println(millis());
//  Serial.println(lastUpdate);
//  Serial.println(Updatedelay);

  // turn on the LED
  digitalWrite(2,HIGH);
  // redraw the screen
  u8g.firstPage();  
  do {
    draw(-1,-1);
  } while( u8g.nextPage() );
  
  //no magic in the delay.  just gives time for the LED to light up and shutdown with the transmit cycle in the middle.
  
  smartdelay(200);
    locationUpdate(flat,flon);
  smartdelay(200);
  
  // redraw the screen.
  u8g.firstPage();  
  do {
    draw(flat,flon);
  } while( u8g.nextPage() );

  // turn off the LED
  digitalWrite(2,LOW);
  lastUpdate=millis();
  }
  
  //digitalWrite(2,LOW);  
}

Serial.println();
  
  smartdelay(1000);
  // check for packets.
  processPacket();
  
}


void locationUpdate(float flat,float flon) {
  // Let's first set our latitude and longtitude.
  // These should be in NMEA format!


// flat, negative == Southern hemisphere
// flon, negative == western hemisphere
//  for instance...  43.646808, -116.286437  
//nmea then is:
//  43 .. (.646808*60),N
// 116 .. (.286437*60),W  (w since it was negative)
// APRS chokes when you send a ,N though... 
// it also chokes when you send more than 2 decimal places.

  int temp;
  char y[13];
  char z[13];

// CONVERT to NMEA.  
  if (flat<0){
     temp=-(int)flat;
     flat=temp*100 - (flat+temp)*60;
     ftoa((char*)&y,flat,3);   
     //z[10]=',';
     if (flat > 10000) {
       y[8]='S';
       y[9]=0;
     } else {
       y[7]='S';
       y[8]=0;
     }
  } else {
     temp=(int)flat;
     flat=temp*100 + (flat-temp)*60;
     ftoa((char*)&y,flat,3);      
     //z[10]=',';
     if (flat > 10000) {
       y[8]='N';
       y[9]=0;
     } else {
       y[7]='N';
       y[8]=0;
     }  
   }
     
   APRS_setLat(y);
   
  Serial.println();
  Serial.println(F("Location Update:"));
  Serial.print(F("Lat: "));
  Serial.println(y);
  
  if (flon<0){
    temp=-(int)flon;
    flon=temp*100 - (flon+temp)*60;
    ftoa((char*)&z,flon,3);   
     //z[10]=',';
     if (flon > 10000){
       z[8]='W';
       z[9]=0;
     } else {
      z[7]='W';
      z[8]=0; 
     }
  } else {
    temp=(int)flon;
    flon=temp*100 + (flon-temp)*60;
    ftoa((char*)&z,flon,3);   
     //z[10]=',';
     if (flon > 10000){
       z[8]='E';
       z[9]=0;
     } else {
      z[7]='E';
      z[8]=0; 
     }
     
}
    APRS_setLon(z);
 
 // done converting to NMEA.

  Serial.print(F("Lon:"));
  Serial.println(z);
    
  // We can optionally set power/height/gain/directivity
  // information. These functions accept ranges
  // from 0 to 10, directivity 0 to 9.
  // See this site for a calculator:
  // http://www.aprsfl.net/phgr.php
  // LibAPRS will only add PHG info if all four variables
  // are defined!
  //APRS_setPower(2);
  //APRS_setHeight(4);
  //APRS_setGain(7);
  //APRS_setDirectivity(0);
  
  // We'll define a comment string
  char *comment = " JWAprsTrkr";
    
  // And send the update
  APRS_sendLoc(comment, strlen(comment));
  
}

// Here's a function to process incoming packets
// Remember to call this function often, so you
// won't miss any packets due to one already
// waiting to be processed
void processPacket() {
  if (gotPacket) {
    gotPacket = false;
    
    Serial.print(F("Rcvd APRS packet. SRC: "));
    Serial.print(incomingPacket.src.call);
    Serial.print(F("-"));
    Serial.print(incomingPacket.src.ssid);
    Serial.print(F(". DST: "));
    Serial.print(incomingPacket.dst.call);
    Serial.print(F("-"));
    Serial.print(incomingPacket.dst.ssid);
    Serial.print(F(". Data: "));

    for (int i = 0; i < incomingPacket.len; i++) {
      Serial.write(incomingPacket.info[i]);
    }
    Serial.println(F(""));

    // Remeber to free memory for our buffer!
    free(packetData);

    // You can print out the amount of free
    // RAM to check you don't have any memory
    // leaks
     Serial.print(F("Free RAM: ")); Serial.println(freeMemory());
  }
}


//basically delay without stopping the processor.  
// the library still needs to be able to decode incoming packets.
static void smartdelay(unsigned long ms)
{
  unsigned long start = millis();
  do 
  {
    while (ss.available())
      gps.encode(ss.read());
  } while (millis() - start < ms);
}

static void print_float(float val, float invalid, int len, int prec)
{
  if (val == invalid)
  {
    while (len-- > 1)
      Serial.print(F("*"));
    Serial.print(F(" "));
  }
  else
  {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i=flen; i<len; ++i)
      Serial.print(F(" "));
  }
  smartdelay(0);
}

static void print_int(unsigned long val, unsigned long invalid, int len)
{
  char sz[32];
  if (val == invalid)
    strcpy(sz, "*******");
  else
    sprintf(sz, "%ld", val);
  sz[len] = 0;
  for (int i=strlen(sz); i<len; ++i)
    sz[i] = ' ';
  if (len > 0) 
    sz[len-1] = ' ';
  Serial.print(sz);
  smartdelay(0);
}

static void print_date(TinyGPS &gps)
{
  int year;
  byte month, day, hour, minute, second, hundredths;
  unsigned long age;
  gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, &hundredths, &age);
  if (age == TinyGPS::GPS_INVALID_AGE)
    Serial.print(F("********** ******** "));
  else
  {
    char sz[32];
    sprintf(sz, "%02d/%02d/%02d %02d:%02d:%02d ",
        month, day, year, hour, minute, second);
    Serial.print(sz);
  }
  print_int(age, TinyGPS::GPS_INVALID_AGE, 5);
  smartdelay(0);
}

static void print_str(const char *str, int len)
{
  int slen = strlen(str);
  for (int i=0; i<len; ++i)
    Serial.print(i<slen ? str[i] : ' ');
  smartdelay(0);
}


void aprs_msg_callback(struct AX25Msg *msg) {
  // If we already have a packet waiting to be
  // processed, we must drop the new one.
  if (!gotPacket) {
    // Set flag to indicate we got a packet
    gotPacket = true;

    // The memory referenced as *msg is volatile
    // and we need to copy all the data to a
    // local variable for later processing.
    memcpy(&incomingPacket, msg, sizeof(AX25Msg));

    // We need to allocate a new buffer for the
    // data payload of the packet. First we check
    // if there is enough free RAM.
    if (freeMemory() > msg->len) {
      packetData = (uint8_t*)malloc(msg->len);
      memcpy(packetData, msg->info, msg->len);
      incomingPacket.info = packetData;
    } else {
      // We did not have enough free RAM to receive
      // this packet, so we drop it.
      gotPacket = false;
    }
  }
}

