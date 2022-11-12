
#define SensorPin2      D3
#define DhtDataPin      D4
#define oneWirePin      D5
#define SensorPin       D6
#define RtcPin          D7


#define MIN_BAT_STATUS1 550
#define MIN_BAT_STATUS2 590

#define VENTANA_DATOS   15

#define MAXTRY          5

#define DEBUG_SERIAL

#include <Wire.h>
#include <RTC.h>

DS3231 RTC;

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 121);
DNSServer dnsServer;

ESP8266WebServer server(80);

#include <OneWire.h>
#include <DallasTemperature.h>


OneWire oneWire(oneWirePin);
DallasTemperature sensors(&oneWire);

#include <LittleFS.h>


const int ERR_TEMP  = 1;
const int ERR_TEMP2 = 2;
const int ERR_RTC   = 4;
const int ERR_FS    = 8;

uint8_t tipo_de_error = 0;

bool init_fs = false;

uint32_t epoch = 0;

uint8_t modo_WiFi_count = 0;

uint32_t vbat;

uint8_t intentos;

float temperatureC1;
float temperatureC2;
float humity;

struct {
  char magic[2];
    
  char dname[75];
  
  char fname_logo[32];
  
   uint16_t sleepy;
   
  int16_t t_delta;
  int16_t t2_delta;
  int16_t h_delta;
   
  
  uint32_t last_epoch;
  uint32_t next_epoch;
  uint32_t crc32;
} rtcData;


uint32_t calculateCRC32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffff;
  while (length--) {
    uint8_t c = *data++;
    for (uint32_t i = 0x80; i > 0; i >>= 1) {
      bool bit = crc & 0x80000000;
      if (c & i) {
        bit = !bit;
      }
      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }
  return crc;
}

bool RTC_LEAP_YEAR(uint16_t year) {
  return ((((year) % 4 == 0) && ((year) % 100 != 0)) || ((year) % 400 == 0));
}

void convierte_epoch(char *data, bool separacion=false) {
  uint8_t RTC_Months[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}, /* Not leap year */
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}  /* Leap year */
  };
  uint32_t dd1;
  memcpy(&dd1,&data[0],4);
  

  dd1 -= 5*60*60;
  
  uint8_t x1,x2,x4,x5,x6;
  uint16_t x3;
  x6 = dd1 % 60;
  dd1 /= 60;
  x5 = dd1 % 60;
  dd1 /= 60;
  x4 = dd1 % 24;
  dd1 /= 24;
  x3 = 1970;
  while (1) {
    if (RTC_LEAP_YEAR(x3)) {
      if (dd1 >= 366) {
        dd1 -= 366;
      } else {
        break;
      }
    } else if (dd1 >= 365) {
      dd1 -= 365;
    } else {
      break;
    }
    x3++;
  }
  for (x2 = 0; x2 < 12; x2++) {
    if (RTC_LEAP_YEAR(x3)) {
      if (dd1 >= (uint32_t)RTC_Months[1][x2]) {
        dd1 -= RTC_Months[1][x2];
      } else {
        break;
      }
    } else if (dd1 >= (uint32_t)RTC_Months[0][x2]) {
      dd1 -= RTC_Months[0][x2];
    } else {
      break;
    }
  }
  x2++;
  x1 = dd1 + 1;
  if(separacion)
    sprintf(data,PSTR("%04d/%02d/%02d;%02d:%02d"),x3,x2,x1,x4,x5);
  else
    sprintf(data,PSTR("%04d/%02d/%02d %02d:%02d"),x3,x2,x1,x4,x5);
}

void convierte_data(char * data) {
  uint16_t dd2;
  uint16_t dd3;
  uint16_t dd4;
  memcpy(&dd2,&data[4],2);
  memcpy(&dd3,&data[6],2);
  memcpy(&dd4,&data[8],2);
  convierte_epoch(data, true);
  

  strcat(data,";");
  char data2[16];
  if(dd2 == 0xFFFF)
    strcat(data,";");
  else {
    float vv1 = dd2/100.0f;
    vv1 += rtcData.t_delta/100.0f;
    sprintf(data2,PSTR("%0.2f;"),vv1);
    strcat(data, data2);
  }
  if(dd3 == 0xFFFF)
    strcat(data,";");
  else {
    float vv2 = dd3/100.0f;
    vv2 += rtcData.t2_delta/100.0f;
    sprintf(data2,PSTR("%0.2f;"),vv2);
    strcat(data, data2);
  }
  if(dd4 != 0xFFFF) {
    float vv3 = dd4/100.0f;
    vv3 += rtcData.h_delta/100.0f;
    sprintf(data2,PSTR("%0.2f"),vv3);
    strcat(data, data2);
  }
  strcat(data,"\r\n");
  #ifdef DEBUG_SERIAL
    Serial.print(data);
  #endif
}

String fname;
File fsUploadFile;
void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    fname = upload.filename;
    if(!fname.startsWith("/")) fname = "/"+fname;
	
        fsUploadFile = LittleFS.open(fname, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) {
      fsUploadFile.close();
      if(fname.length() > sizeof(rtcData.fname_logo)-1) {
        LittleFS.remove(fname);
      } else {
        int ii = fname.lastIndexOf(".");
        if(ii==-1)
          LittleFS.remove(fname);
        else {
          String aa = fname.substring(ii+1);
          aa.toUpperCase();
          if(aa.equals("JPG") || aa.equals("JPEG") || aa.equals("GIF") || aa.equals("PNG") || aa.equals("WEBP") || aa.equals("BMP")) {
          
		  
            File file = LittleFS.open("/config", "w");
            if(file){
              if(strlen(rtcData.fname_logo)>4 && LittleFS.exists(rtcData.fname_logo))
                LittleFS.remove(rtcData.fname_logo);
              fname.toCharArray(rtcData.fname_logo, sizeof(rtcData.fname_logo));
              file.write((uint8_t *)&rtcData,sizeof(rtcData)-12);
              file.close();
              #ifdef DEBUG_SERIAL
                Serial.print(F("CH_CONFIG(4)="));
                Serial.println(rtcData.fname_logo);
              #endif
            } else
              LittleFS.remove(fname);
          } else {
            LittleFS.remove(fname);
          }
          fname = String();
        }
      }
      
            server.sendHeader("Location","/list");
      server.send(303);
    } else {
      server.sendHeader("Location","/upload");
      server.send(303);
    }
  }
}

void handleFileList() {
  String path;
  if (!server.hasArg("dir")) {
    path = "/";
  } else {
    path = server.arg("dir");
  }
  if (path != "/" && !LittleFS.exists(path)) {
    server.send(500, "text/json", F("ERROR handleFileList"));
    return;
  }
  Dir dir = LittleFS.openDir(path);
  path.clear();

  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") {
      output += ',';
    }
    bool isDir = false;
    output += "{\"type\":\"";
    if (dir.isDirectory()) {
      output += "dir";
    } else {
      output += F("file\",\"size\":\"");
      output += dir.fileSize();
    }
    output += "\",\"name\":\"";
    if (entry.name()[0] == '/') {
      output += &(entry.name()[1]);
    } else {
      output += entry.name();
    }
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}

void handleRedirect() {
  server.send(200, "text/html", F("<html><head><script type = \"text/javascript\"> function Redirect() { window.location.replace(\"/\"); } setTimeout('Redirect()', 100); </script></head><body></body></html>"));
}


void HandleRoot() {
  char dddx[256];
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  
 
  server.send ( 200, "text/html", F("<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style> body { font-family: Verdana, sans-serif; height:100%; padding:0; margin:0; width:100%; display:flex; flex-direction:column; } input[type=\"button\"] { width: 180px; height: 30px; background: #3498db; border: none; color: #fff; font-size: 14px; font-weight: bold; -webkit-border-radius: 5px; -moz-border-radius: 5px; border-radius: 5px; } </style></head><body><table border=0 width=\"100%\" cellspacing=\"10\"><tbody><tr><td align=\"center\">"));

  //LOGO OPS
  //server.sendContent(F("<tr><td style='width: 70%;'><img style='width: 70%;' src='data:image/png;base64,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cAAAAAAAPD94cGFja2V0IGJlZ2luPSLvu78iIGlkPSJXNU0wTXBDZWhpSHpyZVN6TlRjemtjOWQiPz4KPHg6eG1wbWV0YSB4bWxuczp4PSJhZG9iZTpuczptZXRhLyIgeDp4bXB0az0iWE1QIENvcmUgNC40LjAtRXhpdjIiPgogPHJkZjpSREYgeG1sbnM6cmRmPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5LzAyLzIyLXJkZi1zeW50YXgtbnMjIj4KICA8cmRmOkRlc2NyaXB0aW9uIHJkZjphYm91dD0iIgogICAgeG1sbnM6eG1wTU09Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9tbS8iCiAgICB4bWxuczpzdEV2dD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3NUeXBlL1Jlc291cmNlRXZlbnQjIgogICAgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIgogICAgeG1sbnM6R0lNUD0iaHR0cDovL3d3dy5naW1wLm9yZy94bXAvIgogICAgeG1sbnM6dGlmZj0iaHR0cDovL25zLmFkb2JlLmNvbS90aWZmLzEuMC8iCiAgICB4bWxuczp4bXA9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC8iCiAgIHhtcE1NOkRvY3VtZW50SUQ9ImdpbXA6ZG9jaWQ6Z2ltcDozZDJlMWMzNy1mNGEyLTQyMzYtODFkMi0yMWFjM2Q0NWYyZTMiCiAgIHhtcE1NOkluc3RhbmNlSUQ9InhtcC5paWQ6MmQ4YjEzNDctZjEwZi00MGY2LWJhOTQtNmM4YWY5NmVkYTNmIgogICB4bXBNTTpPcmlnaW5hbERvY3VtZW50SUQ9InhtcC5kaWQ6YzE1M2MwZGItZWRmYS00NjgyLThhODMtOGRlMjUzYmJiYTkxIgogICBkYzpGb3JtYXQ9ImltYWdlL3BuZyIKICAgR0lNUDpBUEk9IjIuMCIKICAgR0lNUDpQbGF0Zm9ybT0iTGludXgiCiAgIEdJTVA6VGltZVN0YW1wPSIxNjM5NTg0ODEyMTMyOTM1IgogICBHSU1QOlZlcnNpb249IjIuMTAuMjgiCiAgIHRpZmY6T3JpZW50YXRpb249IjEiCiAgIHhtcDpDcmVhdG9yVG9vbD0iR0lNUCAyLjEwIj4KICAgPHhtcE1NOkhpc3Rvcnk+CiAgICA8cmRmOkJhZz4KICAgICA8cmRmOmxpCiAgICAgIHN0RXZ0OmFjdGlvbj0ic2F2ZWQiCiAgICAgIHN0RXZ0OmNoYW5nZWQ9Ii8iCiAgICAgIHN0RXZ0Omluc3RhbmNlSUQ9InhtcC5paWQ6ZGE3ZWRlN2MtMDQyYi00MGJiLWI4ZWEtZmM3MGJhYWE4Nzk2IgogICAgICBzdEV2dDpzb2Z0d2FyZUFnZW50PSJHaW1wIDIuMTAgKExpbnV4KSIKICAgICAgc3RFdnQ6d2hlbj0iMjAyMS0xMi0xNVQxMToxMzozMi0wNTowMCIvPgogICAgPC9yZGY6QmFnPgogICA8L3htcE1NOkhpc3Rvcnk+CiAgPC9yZGY6RGVzY3JpcHRpb24+CiA8L3JkZjpSREY+CjwveDp4bXBtZXRhPgogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgCjw/eHBhY2tldCBlbmQ9InciPz5V3WIGAAABhWlDQ1BJQ0MgcHJvZmlsZQAAKJF9kT1Iw0AYht+miiKVCnYQcchQdbEgKuKoVShChVArtOpgcukfNGlIUlwcBdeCgz+LVQcXZ10dXAVB8AfEzc1J0UVK/C4ptIjx4O4e3vvel7vvAKFeZprVMQ5oum2mEnExk10Vu14RQhh9tI7KzDLmJCkJ3/F1jwDf72I8y7/uz9Gr5iwGBETiWWaYNvEG8fSmbXDeJ46woqwSnxOPmXRB4keuKx6/cS64LPDMiJlOzRNHiMVCGyttzIqmRjxFHFU1nfKFjMcq5y3OWrnKmvfkLwzl9JVlrtMcQgKLWIIEEQqqKKEMGzHadVIspOg87uMfdP0SuRRylcDIsYAKNMiuH/wPfvfWyk9OeEmhOND54jgfw0DXLtCoOc73seM0ToDgM3Clt/yVOjDzSXqtpUWPgPA2cHHd0pQ94HIHGHgyZFN2pSBNIZ8H3s/om7JA/y3Qs+b1rXmO0wcgTb1K3gAHh8BIgbLXfd7d3d63f2ua/fsBV55ynACL/AcAAAMAUExURf////r////++///+v/+//z///v///7//////f3/////+/n///j///b///f////9//7+////+f/+/fP///T///D9//7//fD///7+/P39//7/+/78//3//fr+//78/f/9+nGXv+3//3CYw26YyG6Uwv/8+P3+/2qWyWuXxWySv/799XGVvGqYzm2WwmqUxG+TvmyWxmuYy2+Zy/3+/XCWxfb9/2iW0fT8/2yUu3uexXCSuXqUrf78+muZ0nGbx3GStPz7/3WYwGiVzHebwnyUqOf2/2qUwfD9/22azeXz/+H2//j7/6fA1aXB2/v8/GeY1XWWuf/9/O35/3ucv22Wy2eRwXqZufz//HSZxmyPtv//9nyhyWqTyHadyGaTxqq/0YCgwdHh7tbl8nSVtYClzoWgub3S5pmvwoms0uv2/+n//4WpzmmPvN3y/s3h9H+cuMne8tvq9XGczeLx/qrA2Mzc69Xq/oWjwImjvJ+0x4yjtqO4y3efzNvt/oyrzN7t+oCixdjq+o6w0Z+40X6ZsneZvdfw/pKsxvH6/8Tb8JSy0W+c0Z692/z9+YGlypKovICfvcXZ6nCOroyx2Iypx5a21nqiz6nE3a/F2o2nv/7//un7/3aTsLTL4JGvzf369YSds7HD07/W64qmw6m8zKS80rrO4bLO6cfT3vv6+Zm10paxyc/k+LDI3pG02ez9/5q0zeX7/7jS67zX8aO914eoyaTE5IWlxb3a95a53cPW5Y+qwsTe94Go02STzcni+63P8NTm+LrL2dr3/6jJ6sro/tDs/vj+//f6/KSzwczZ5arH4q7L5ouerp6/4XmXtejx+aPM94Wt2Jytu5y51uPt9rXI2MDR4KPA34224LTS8LfP5XePqLPAy8/n/cTl/vH3/d3l67/f/W6d15zC6pW95KLH7mmb2qy6xr7O25Kkspiot9Hx/rXV9uD8/7fc/YOYq4Sw4uLq8NXe5nil17nG0YKUoe7z9sPN1XSg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' alt='digesa' /></td></tr>"));
  //LOGO MINSA DIGESA
  //server.sendContent(F("<tr><td style='width: 70%;'><img style='width: 70%;' src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAUoAAAAvCAIAAAAKBRQPAAAQYnpUWHRSYXcgcHJvZmlsZSB0eXBlIGV4aWYAAHjapZlrciu7DYT/cxVZAgm+l0OQRFV2kOXnw0j2PfZxUjk3dlkjj+YBAo3uxijcf/3Twj/4KXGOUGofbbYW+SmzTFm8GfH1s57XFMvz+vyU9X6Xvu4PYu+ThF2ZbX79O9prmz72v0/42Ca/YP3lQmO/P9CvH8zyvv74diF5bbJH5O/P+0LzfaEsrw/S+wLrtazY5ui/LkHva3ve5z1p4C/4Sxlfw/7t/072TuU+WeTmlCOvkssrgOx/KeTFB4nXmLsfmPPzvvBa88dSvAI/JTz/tT88x34kPP4S7rdqfb77Vi15pyJ8r1aRj7t9S3L73P64P6T67YP8eX/5spzxfidf92uO9xVR/Jp9/zM7w+y+VrdKI9XtvaiPJT7vOE65hd96BEJrsfNXuUR/fie/A1RvoHDijsrvTjMJ5bJU0kkrWbrPdqdNiEVuEGolIpsK+s5B7aZs6paoHL/JpOeZTx5UdFP2zF75jCU9t51xh+dugzufxKGSuFjilD/+DX96gplDOiXPJaVPr/qKeLIJwyvnrxxGRZK9k1qfBH/8fv/xumYqWD3L3iKTxOrrElrTX0yQn0JnDqxsX8BL/bwvQIq4dSUYOqMkqpZyTS3FLtJTIpGDAi1Cp5lEqUCqVQ5BSsm5UZshfmtO6ek5VKqwO7AfMqMSNbfcqc3Mi2KVUsFPLwMMrZprqbW22uuos66WW2m1tdabk+LquZfQa2+999FnXyOPMupoo48x5lhTZoY062yzzzHnXIt7Lq68OHtxwFoqmrVoDdq069CpawOfXXbdbfc99tzryMkH/jjt9DPOPOumC5RuufW22++48y4DapaDFavWrNuwaeuzau+y/vb7B1VL76rJUyk/sH9Wjb29f1wiOZ1UrxkFk1ASFe9eAgAtXrM4UinilfOaxelcV6GsVL1mJ3nFqGC5Saqlj9oFeVXUK/d/1S308qVu8ncrF7x0f1i53+v2U9WOy9B+KvbqQk9qzHSftazjlMuJ16RpOqKQ2ShXqo7Qb63FrBcOM26q6/C2sJh2rDciv+dkaGiSExI9CLSbXOODbrtlLnNIadDj2p/pOMLWMqGydPbOsu/RLjftNcuN6RyFOPK8c+Z9mlzty8YhweWQFAtah7J47tNuLmdWz3CHEbujKe0u8PCqZWo7UrumVdsqdxwZp07uXPigphsGrT/PbfVEPZoPxZixkYnCNnvSY+wlsZ5CiQ6I0kuVdtFIBWo9c/dL3sOSQqlYVQQ1N9qde6R9eqqrFMtt1D7XuGu2vCpXLR3mXkKm11nFZs5oRp87IDoZ6iEwm1zf4u4srRa9Bv0/C6FeXbZJmo3MQnj35sgJwM0INQr4aKHnq1XWWu1uGGyzcyEL5ZKmvW+xK3ZX6trmlT3gOGBzBQZsG1iSQ5XBccFWWpav3bNOVbt0S6eDaTkDOzfXAzRbM5sJR+P7ltVqtzWd4MiitnVYfSCb3WhjaKUMMJPWINnW4yqjSXJNGzutG5cS4MqarOtspglP12jDRe+BgiCrWztc1aykosfvuePovk0rG+1DALbiqveJEVn0LcWnU5KAgX73joFC2JxgJTtoLhA0IJ60s3CKMVbb5yaa7UIWbHXlgXp7q49b4ANiLDiJsAfpjd43UC9orpRlGUlENqZ9BOva84T7CpalDqBklAifRouTI+H+unAL3DruItvbozgpnb4O3uLeSfq8zyoFAQJFOx1W81ZxA73pcpIZiHG7SxQ/je4hLTdbLhCBxkYdiUaoHUhTy1mn5bMLHHNHgUC7s5sfQ9VArSc6/5To7sywIj30NdG917XvPDDiXDvftcNpRuqBWzOReqG/AaVG7E2pNJvpmVhiIoUS9Sw5FxCpsD4YFmhOjuDEGkBzKxUEg2V1llvXCTDeov8looQ/ygvknFiu+RKCpGdBTdcavZSTZ69VCwRoVJE0HjJLW2+Y8VY35ReOwpHeDNvUOXTVg0DQ/UoXFGCOsESlP5AM+ogwj0FTFUJrFANPOCgIQwbSYr9nNvxPqa3a6ckLEOnwdYBoGUCesDOdilWtEsbY1fSiiGflA6N3pRaVkmBYAJfrT7XSqPrZNcHPvREQbdNoiZtb24rGFfgI8qMNCPnJ1t+EA7Zmwf210XEoFnoU664wim4YbeG1QAIdiAEA7unQkvAgyLEUkQFYOrFSiC8GVQ6mYlfV5QqujgfJPJsP+51uNz9z6x2Xf+04TRDwbqudBfnjr9akuWNreI4EfU9jSCNVqP3Qh8zgzOrq2RtLO/AsiuCCV0hpJucdD1lyT7ldpITO7/tI7AdI2q74yezdjS9afWLc0zgqsx2Uw1ObboF+pjfmoddoxY1doBXaxWEhNnuufI9lmYN0DizGRFuXTwCJmcDQgQty3Wu7NMKgxXqom77w5azIvxf56HSUDzSodEb85+0wHupadq4VmZ0FL5HRi6nUDTFHt/NhgnTNLLTd7GhuvsA4whBSvK5IicXbBuMKYWHcGtPNhUdREaLDicEx3ERWgAI4SVqF54ULTKhrWgO6V/E1GIW4JrTTZ/JRgb/iCJCNTRhgND1IihLsyJyQX4FHgKqciKZSWMJFgDiJ22Nn7vCwMuXp3CPiatbmnhiGBkYGY1az7XPzwVQZWWBxsKq6lWAgnzg4mpV04oHZ0h20LfSA0XDn1iLEwGUtnVALPud4MoFbUbk+8Lnp6j5cu67NNDzdeJjoVogWpOAACG+C7l3D/JTbAz7Q8Ek2EW6cJbU3cSUZDRSd7CF6d8GKeBkYGjeFMXUnByoXCbkMmnQWVaOGRFNxtNQNDLe73HT4hWrTix2pncrd3B/eApRj63TLMDpYbBAEHihc9wEkZiGD3LY/RfD746sxEAhlNZwDV2OFcCcuHKaEN6FFPEqCdE2YtQM+D39DDikOpRhoKJU4afa+6WWuhpOs0+0Jtu8a+gALVVcMTGNBQclK7BqWRjqCDigu0mQSQN443cKiBVgLRt3qlgaH4VMzQ6UzR6av0yBNJO4AkQkgDdJLmzzUvnwAyLevrrAFhpEl9nis6mYEOvQGthshVRhIV8eeI5TIAukPB02vW3deisNikbB+SqLFJ1+5G3/X+kExcOeHot3Un7ThFOBycqsbiZMT1qFHqakMCkPCaAoKRjuKDtHpUdDxHVi0M5ePSyAIlWJMve6XgUtnORpyHwlerRkx3SsNBVJUIWHorNGgte05MazOsRjMyXEOJpjL8LbQ3W4UYGDY4xmNfoEEIFj8BlQCJ7FJSl8jMDnj8Ac2FV3tC55k6pqFkYXuXM1GU/DZ/EK6jcmAaXoLtcB+ASpmCs7dAt8IrsM8GZagMbBnA8eiQAuHhv+cC7NAsjH12NzVN2tGqX1MASlbq2Edqah5DbDpkfGoe2bglJXxoEBOmRRxkQd3HmJ2DWnx3A0cwBNUw1SaVsM3Qez5sSp3LKPRzCeVsQUZEpGlDDvLfWufl4hAE8OiKZaJ+Qhy0jopHn5nIzwbk1cZM8g8seAT6A7D9GMpIHYoYGfGlH7oNSSZScuNO36aqSCVBzK4WG7n2otvg7LgiLqZLRhgBOcE8FDWBBvh7sFZOHMwEwARyrJ8oOsAxyM8kyEfX9qtk2PHETBnkKS5SvId6iMbaeaViTzAe3Ca3Y6XuQKqYpkOwQwH8ElNzBnMRAx/Lnrq+hfhwrWwJGix+wi6Z2locWJjKTrsXuk1N3m7Cfll9GBEu5gBbAgL6wclwW7ViEoBfgZNOLsycoJmf1qjoFELu0fz5gTn5AoMYCqNUfS6wK6YIG7Y/jWbjGiKaaCDsuGUik9HePpFmsHyRkrATFJ/9tPKk2gWQMZw3bB/MbwhJEQYjNGkykMtHZwwAe7QYcLCuHoBNyMF6cXhwQ+cC7dg1D2j0x8ETGdsxrnOAIl9wLMTYsHSwkxVg0RX0NjQkZ7wtHAjMEhNcAQVf4NeYogwvEMYouooOBT8KaAdCh9jK6EURuoQE7KRIELGCiwh3ugelupIqD52UxtAQ7wd66NOTAjd4sx9XP6AJIN8Uw3TbTUmIU65FBnNK5sJc2UvdnGId98/rj9pcX+wKKZ0yGxdLyrDKnHrDFQZFob1qzjrMGHC/rhKsslZsLSkISdduoN5j1pxHhznD2QWRnZhntH/Y0xHT4rESQjnBiPvhOvpGJTbsLozij9dreSyuoeE6hmZoAzkZja8hcsOGAxAn5/MrRkfhK6B7VEM1+uNApJTN/PwjwqvowDi85MhDJ+OcMOdE+lBSOE/GlHhYyZBfrCtCBpzv9VBRffacMveA/FDdZwTsX7XH19kR4z4oxUyNn0GJGyGcvAHmmk3nBSD0HaECOSFM4IIKBrDNKrFAHMC3XjWHANjhXr7yFiN+Z7xeUQ6BhFtM7okQaowLk6CDCA6pBQHSZmo3ygMfjRgj+6d3Qpaxl+4CRmK38CjJuAK0g7ZR3lwqMSK1VyTMQFlLM5Bgo0rPQC9y4oLliijb8CTdPinzMJa6cjNeAepIGJ4r05GMkXCPDstdwwwRh3FtxCnP9dmiIuQccNhgwkGIbw23nUhNeCI0FpjZMPtaHwc3DlkzCc2VAyOx4FCtSmTOJoSC1ET6GH8oKuwz/AmvnC4a4Clu1zaj+uqjkGTI7nYW9JkE/2G/A+XAFS94JjqeB7Evb5WYVwdI7NmQa5IK4nBb0HczW0q17LhEDpZ0VcJe3t9sn9dwUyppG0r/IMMM54LLr6Q7+4PsqX6E2fzJq2yp/phrg+JMX/UgG6U453vj+AEA568rA04yWLmAK+wAfAUVNwvRTMDhIYI1ciIK7Q0g5SRbMZc2x4Uhq11HNRpCOWNeDouX5gPUTJodUGwgBA6ca72L6HwFLjx+jop/HUW14IWCiyGhYOzq38ZyNyWnB9RWBqtosiMzESV3W7QPZD88kFx0WuEm5PrFsK2/InZ8lEF3oG/2jiSXFKAAHMjXYWHGh5nh6y0KAMynQJdB5AMXVSsJ12AVFwcqTPQdf5LOCU31qOl4o/U8HjcjCGaynAfPp57eUPiarfqo4uoNMs1f4iEfYfjFn2GpaCb/Hs0yohwTdgDKOHdDWW0/ozNjGX2SnbGNWIlMDgIFX4Waiy3+DdGZVp5mOfjbDjodf5zNmLMyXCWsLTF7g0VqGHOYDAb06MeWB8fAXEpSIIx8UIW6gbrcj0GEPz3ZUYCoGruXwJjCgV2S7YH82r1kjYVIkFNaZkK5CEweAzvl0HUZFCsqKN/+8P1oSG2mNHzPCbAYTyPnHY5z2MEZho6xN/lc18PCxgOnq2ADFG4FoLxjGV4xR9E/Zo0eU79WDZQto9V+9eq8znqtXJ/wv0RDheHJAMBvcJ5B/MKxT3NeWVjvbLhuIrvxD9B/Jp4eapmX0JgeR+pn5+p/zUAv/33dJDm4M9NmZ1Q6NgRutPwYOiAYHLxwpB7cY3x4XhRIHUPwlCJdWTi2Iw/UDjtUsmRveTRQVD8cQBkzMmp4GztDET1lXLBm/o8Wv/D3uA3/Pzsp+N/3ecHo5O/31gt/MnhP+2r/i06tjr8EGab/liICRhhoLs3o6uiQMmfVT8nUl1ML9j5N2KzjEBIkCC6AAAO6WlUWHRYTUw6Y29tLmFkb2JlLnhtcAAAAAAAPD94cGFja2V0IGJlZ2luPSLvu78iIGlkPSJXNU0wTXBDZWhpSHpyZVN6TlRjemtjOWQiPz4KPHg6eG1wbWV0YSB4bWxuczp4PSJhZG9iZTpuczptZXRhLyIgeDp4bXB0az0iWE1QIENvcmUgNC40LjAtRXhpdjIiPgogPHJkZjpSREYgeG1sbnM6cmRmPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5LzAyLzIyLXJkZi1zeW50YXgtbnMjIj4KICA8cmRmOkRlc2NyaXB0aW9uIHJkZjphYm91dD0iIgogICAgeG1sbnM6eG1wTU09Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9tbS8iCiAgICB4bWxuczpzdEV2dD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3NUeXBlL1Jlc291cmNlRXZlbnQjIgogICAgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIgogICAgeG1sbnM6R0lNUD0iaHR0cDovL3d3dy5naW1wLm9yZy94bXAvIgogICAgeG1sbnM6cGhvdG9zaG9wPSJodHRwOi8vbnMuYWRvYmUuY29tL3Bob3Rvc2hvcC8xLjAvIgogICAgeG1sbnM6dGlmZj0iaHR0cDovL25zLmFkb2JlLmNvbS90aWZmLzEuMC8iCiAgICB4bWxuczp4bXA9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC8iCiAgIHhtcE1NOkRvY3VtZW50SUQ9InhtcC5kaWQ6MEU5QjQyNDYxRUU2RTUxMThCRkFCQjg5NzcxMjNDNzQiCiAgIHhtcE1NOkluc3RhbmNlSUQ9InhtcC5paWQ6OGZhYzEyNTAtZWJiNC00YTRjLTkwZTktMGYxMTZmYjhjZjMxIgogICB4bXBNTTpPcmlnaW5hbERvY3VtZW50SUQ9InhtcC5kaWQ6MEU5QjQyNDYxRUU2RTUxMThCRkFCQjg5NzcxMjNDNzQiCiAgIGRjOmZvcm1hdD0iaW1hZ2UvanBlZyIKICAgR0lNUDpBUEk9IjIuMCIKICAgR0lNUDpQbGF0Zm9ybT0iTGludXgiCiAgIEdJTVA6VGltZVN0YW1wPSIxNjM2MzQ0MjkyNTU0MTIwIgogICBHSU1QOlZlcnNpb249IjIuMTAuMjQiCiAgIHBob3Rvc2hvcDpDb2xvck1vZGU9IjMiCiAgIHBob3Rvc2hvcDpJQ0NQcm9maWxlPSJBZG9iZSBSR0IgKDE5OTgpIgogICB0aWZmOk9yaWVudGF0aW9uPSIxIgogICB4bXA6Q3JlYXRlRGF0ZT0iMjAxNi0wMy0wOVQxNTo0NDo0Mi0wNTowMCIKICAgeG1wOkNyZWF0b3JUb29sPSJHSU1QIDIuMTAiCiAgIHhtcDpNZXRhZGF0YURhdGU9IjIwMTYtMDMtMDlUMTU6NDQ6NDItMDU6MDAiCiAgIHhtcDpNb2RpZnlEYXRlPSIyMDE2LTAzLTA5VDE1OjQ0OjQyLTA1OjAwIj4KICAgPHhtcE1NOkhpc3Rvcnk+CiAgICA8cmRmOkJhZz4KICAgICA8cmRmOmxpCiAgICAgIHN0RXZ0OmFjdGlvbj0iY3JlYXRlZCIKICAgICAgc3RFdnQ6aW5zdGFuY2VJRD0ieG1wLmlpZDowRTlCNDI0NjFFRTZFNTExOEJGQUJCODk3NzEyM0M3NCIKICAgICAgc3RFdnQ6c29mdHdhcmVBZ2VudD0iQWRvYmUgUGhvdG9zaG9wIENTNSBXaW5kb3dzIgogICAgICBzdEV2dDp3aGVuPSIyMDE2LTAzLTA5VDE1OjQ0OjQyLTA1OjAwIi8+CiAgICAgPHJkZjpsaQogICAgICBzdEV2dDphY3Rpb249InNhdmVkIgogICAgICBzdEV2dDpjaGFuZ2VkPSIvIgogICAgICBzdEV2dDppbnN0YW5jZUlEPSJ4bXAuaWlkOjQ0YWRkYzNlLTYxOTUtNGRmYy1hOTQzLWU2ZmRkOGUyNmQ3YyIKICAgICAgc3RFdnQ6c29mdHdhcmVBZ2VudD0iR2ltcCAyLjEwIChMaW51eCkiCiAgICAgIHN0RXZ0OndoZW49IjIwMjEtMTEtMDdUMjM6MDQ6NTItMDU6MDAiLz4KICAgIDwvcmRmOkJhZz4KICAgPC94bXBNTTpIaXN0b3J5PgogIDwvcmRmOkRlc2NyaXB0aW9uPgogPC9yZGY6UkRGPgo8L3g6eG1wbWV0YT4KICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAKICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIAogICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgCiAgICAgICAgICAgICAgICAgICAgICAgICAgIAo8P3hwYWNrZXQgZW5kPSJ3Ij8+cQc3QAAAASRpQ0NQSUNDIHByb2ZpbGUAACiRY2BgMnB0cXJl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' alt='digesa' /></td></tr>"));
  
  if(strlen(rtcData.fname_logo)>4 && LittleFS.exists(rtcData.fname_logo)) {
    server.sendContent(F("<img src='/logo'>"));
  } else
    server.sendContent(F("&nbsp;"));
	
  if(strlen(rtcData.dname)>0) {
  
    
  } else
    server.sendContent(F("ESTACION SIN NOMBRE"));
  server.sendContent(F("</td></tr><tr><td>Status : "));
  if((tipo_de_error & ERR_TEMP)!=0)
    server.sendContent(F("(NO SE DETECTO DS18B20) "));
	
    if((tipo_de_error & ERR_RTC)!=0)
    server.sendContent(F("(NO SE DETECTO RTC) "));
  if((tipo_de_error & ERR_FS)!=0)
    server.sendContent(F("(ERROR EN MEMORIA, FORMATEAR?) "));
  if(tipo_de_error==0)
    server.sendContent(F("(TODO OK) "));
	
   if(vbat<MIN_BAT_STATUS1)
    server.sendContent(F("<span style=\"color:white\">BAJO</span>"));
  else if(vbat<MIN_BAT_STATUS2)
    server.sendContent(F("NORMAL"));
  else
    server.sendContent(F("<span style=\"color:white\">ALTO</span>"));
    server.sendContent(F("</td></tr><tr><td>     <tr><td style='width: 50%;font-size: 2.5em'>Estacion 021 </td></tr>     </td></tr><tr><td><br />"));
  server.sendContent(F("</td></tr><tr><td>Hora RTC : <span id='re'></span></td></tr><tr><td>Intervalo : <input id='t5' size='6'> min</td></tr><tr><td>Ajuste Temperatura : <input id='t2' size='6'> &deg;C</td></tr> <tr><td><br />"));
  

  uint32_t lll = 0;
  if(LittleFS.exists("/data")) {
    File file = LittleFS.open("/data", "r");
    lll = file.size()/10;
    sprintf(dddx,PSTR("Existen %lu datos guardados"),lll);
    server.sendContent(dddx);
  } else {
    server.sendContent(F("No existen datos guardados"));
  }
  server.sendContent(F("</td></tr><tr><td><br /><input type='button' value='Sincronizar Hora' onclick='ch()' /> <input type='button' value='Guardar Parametros' onclick='mx()' /></td></tr><tr><td><input type='button' value='Descargar Datos' onclick='dd()' id='b1' /> <input type='button' value='Borrar Datos' onclick='bd()' id='b2' /></td></tr></tbody></table><script> function cx(aa){var fo=\"<>@!#$%^&*()_+[]{}?:;|'\\\"\\,/~`=\";for(i=0;i<fo.length;i++) if(aa.indexOf(fo[i])>-1) return true; return false;}"));
  server.sendContent(F("function mx(){var a1=document.getElementById('t1').value;if(cx(a1)){alert('Nombre del dispositivo invalido, utilice caracteres simples'); return;}var a2=document.getElementById('t2').value;if(cx(a2)||isNaN(parseFloat(a2))){alert('Tara de temperatura DS18B20 invalido, utilice caracteres simples'); return;}"));
  server.sendContent(F("var a5=document.getElementById('t5').value;if(cx(a5)||parseInt(a5)<2){alert('La toma de datos tiene que ser mayor a 2 minutos'); return;} location.href = '/cz?a1='+a1+'&a2='+parseFloat(a2)+'&a5='+parseInt(a5);}"));
 
  epoch = RTC.getEpoch();
  sprintf(dddx,PSTR("document.getElementById('t5').value='%u';document.getElementById('t2').value='%0.02f';"), rtcData.sleepy, rtcData.t_delta/100.0f);
  server.sendContent(dddx);
  sprintf(dddx,PSTR("var d=new Date(%lu000);var vbat=%u;"), epoch, vbat);
  server.sendContent(dddx);
  
  
  server.sendContent(F("function r_(){document.getElementById('re').innerHTML=(\" \"+d.getFullYear()) + \"/\" + (\"0\"+(d.getMonth()+1)).slice(-2) + \"/\" +(\"0\"+(d.getDate())).slice(-2) + \" \" + (\"0\" + d.getHours()).slice(-2) + \":\" + (\"0\" + d.getMinutes()).slice(-2) + \":\" + (\"0\" + d.getSeconds()).slice(-2);}setInterval(function(){d=new Date(d.getTime()+1000);r_();},1000);r_();function ch(){var dx=new Date();location.href='ch?d='+(dx.getTime()/1000);}function bd(){location.href = '/bd';}function dd(){location.href='/dd';}"));
  

  if(lll==0)
    server.sendContent(F("document.getElementById('b1').disabled=true; document.getElementById('b2').disabled=true;"));
  server.sendContent(F("</script></html>"));
  server.sendContent("");
}

void getVbat() {
  vbat = 0;
  for(uint8_t ixw=0;ixw<10;ixw++) {
    vbat += analogRead(A0);
    delay(10);
  }
  vbat /= 10;
  #ifdef DEBUG_SERIAL
    Serial.print(F("VBAT:"));
    Serial.println(vbat,DEC);
  #endif
}

bool initFS() {
  if(init_fs)
    return true;
  intentos = 0;
  while(!LittleFS.begin()) {
    if(intentos>=MAXTRY) {
      intentos=100;
      break;
    }
    intentos++;
    delay(10);
  }
  if(intentos==100){
    #ifdef DEBUG_SERIAL
      Serial.println(F("ERROR FILESYSTEM"));
    #endif
    tipo_de_error |= ERR_FS;
    return false;
  }
  init_fs = true;
  return true;
}

bool initRTC() {
  digitalWrite(RtcPin, HIGH);
  intentos = 0;
  Wire.begin();
  Wire.beginTransmission(DS3231_ADDR);
  while(Wire.endTransmission()!=0) {
    if(intentos>=MAXTRY) {
      intentos=100;
      break;
    }
    intentos++;
    delay(10);
    Wire.beginTransmission(DS3231_ADDR);
  }
  if(intentos==100) {
    #ifdef DEBUG_SERIAL
      Serial.println(F("\r\nERROR RTC"));
    #endif
    tipo_de_error |= ERR_RTC;
    digitalWrite(RtcPin, LOW);
    return false;
  } else {
    if (RTC.isRunning()) {
      epoch = RTC.getEpoch();
    } else {
      RTC.setHourMode(CLOCK_H24);
      RTC.setDateTime(__DATE__, __TIME__);
      RTC.startClock();
      epoch = RTC.getEpoch();
    }
    #ifdef DEBUG_SERIAL
      char xxx[32];
      memcpy(xxx,&epoch,4);
      convierte_epoch(xxx);
      Serial.printf(PSTR("\r\nRTC = %s\r\n"),xxx);
    #endif
  }
  digitalWrite(RtcPin, LOW);
  return true;
}

void getDATA() {

 
  tipo_de_error = tipo_de_error & 0xFC;
  
  
  digitalWrite(SensorPin, HIGH);
  
  intentos = 0;
  sensors.begin();
  while(sensors.getDS18Count()<1) {
    if(intentos>=MAXTRY) {
      intentos=100;
      break;
    }
    intentos++;
    delay(10);
    sensors.begin();
  }
  if(intentos==100) {
    tipo_de_error |= ERR_TEMP;
    #ifdef DEBUG_SERIAL
      Serial.println(F("ERROR DS18B20"));
    #endif
  } else {
    sensors.requestTemperatures();
    temperatureC1 = sensors.getTempCByIndex(0);
    #ifdef DEBUG_SERIAL
      Serial.printf(PSTR("DS18B20 = %0.02f°C\r\n"),temperatureC1);
    #endif
  }
  

  digitalWrite(SensorPin, LOW);
 
  digitalWrite(SensorPin2, HIGH);

  delay(400);
  
 
  intentos = 0;
  do {
    if(intentos>=MAXTRY) {
      intentos=100;
      break;
    }
	
   
    intentos++;
    delay(10);
  } while(isnan(humity) || isnan(temperatureC2));
  if(intentos==100) {
    tipo_de_error |= ERR_TEMP2;
    #ifdef DEBUG_SERIAL
	
      
    #endif
  } else {
    #ifdef DEBUG_SERIAL
	
    #endif
  }
  
   digitalWrite(SensorPin2, LOW);
}

void putDATA(File file) {
  char temp[10];
  
    uint16_t dd = temperatureC1 * 100;
  if(tipo_de_error == ERR_TEMP)
    dd = 0xFFFF;
  memcpy(&temp[0],&epoch,4);
  memcpy(&temp[4],&dd,2);
  dd = temperatureC2 * 100;
  if(tipo_de_error == ERR_TEMP2)
    dd = 0xFFFF;
  memcpy(&temp[6],&dd,2);
  dd = humity * 100;
  if(tipo_de_error == ERR_TEMP2)
    dd = 0xFFFF;
  memcpy(&temp[8],&dd,2);
  
  
  uint32_t wpt = file.size();
  file.seek(wpt,SeekSet);
  file.write(temp,10);
  #ifdef DEBUG_SERIAL
    Serial.print(F("DATO GUARDADO POSICION="));
    Serial.println(wpt/10, DEC);
  #endif
}

void modo_WiFi() {

  
  digitalWrite(RtcPin, HIGH);
 
  char ddd[16];
  const char* eee = "1234567@";
  
  
  sprintf(ddd,"TEADES%08X",ESP.getChipId());
  WiFi.forceSleepWake();
  delay(1);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ddd,eee);
  dnsServer.start(DNS_PORT, "*", apIP);
  #ifdef DEBUG_SERIAL
    server.on ( "/ff", []() {
      LittleFS.format();
      handleRedirect();
    });
  #endif
  
  
  server.on("/upload", HTTP_POST,[](){ server.send(200); }, handleFileUpload);
  server.on("/upload", HTTP_GET, []() {
    server.send(200, "text/html", F("<html><body><form method=\"post\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"name\"><input class=\"button\" type=\"submit\" value=\"Upload\"></form></body></html>"));
  });
  server.on ( "/list", handleFileList);
  
   server.on ( "/ch", []() {
    char px[32];
    memset(px,0,32);
    if(server.hasArg("d")) {
      String d = server.arg("d");
      d.toCharArray(px,31);
      uint32_t dx = atol(px);
      RTC.setEpoch(dx);
    }
    handleRedirect();
  });
  
 
  server.on ( "/cz", []() {
    char px[32];
    #ifdef DEBUG_SERIAL
      Serial.print(F("CH_CONFIG() -> "));
    #endif
    if(server.hasArg("a1")) {
      String d = server.arg("a1");
      d.toCharArray(rtcData.dname,sizeof(rtcData.dname));
      #ifdef DEBUG_SERIAL
        Serial.print(F("dname="));
        Serial.print(rtcData.dname);
        Serial.print(F(","));
      #endif
    }
    if(server.hasArg("a2")) {
      String d = server.arg("a2");
      d.toCharArray(px,sizeof px);
      float dx = atof(px);
      rtcData.t_delta = dx * 100;
      #ifdef DEBUG_SERIAL
        Serial.print(F("t_delta="));
        Serial.print(rtcData.t_delta,DEC);
        Serial.print(F(","));
      #endif
    }
    if(server.hasArg("a3")) {
      String d = server.arg("a3");
      d.toCharArray(px,sizeof px);
      float dx = atof(px);
      rtcData.t2_delta = dx * 100;
      #ifdef DEBUG_SERIAL
        Serial.print(F("t2_delta="));
        Serial.print(rtcData.t2_delta,DEC);
        Serial.print(F(","));
      #endif
    }
    if(server.hasArg("a4")) {
      String d = server.arg("a4");
      d.toCharArray(px,sizeof px);
      float dx = atof(px);
      rtcData.h_delta = dx * 100;
      #ifdef DEBUG_SERIAL
        Serial.print(F("h_delta="));
        Serial.print(rtcData.h_delta,DEC);
        Serial.print(F(","));
      #endif
    }
    if(server.hasArg("a5")) {
      String d = server.arg("a5");
      d.toCharArray(px,sizeof px);
      rtcData.sleepy = atoi(px);
      #ifdef DEBUG_SERIAL
        Serial.print(F("sleepy="));
        Serial.println(rtcData.sleepy,DEC);
      #endif
    }
    File file = LittleFS.open("/config", "w");
    if(file){
      file.write((uint8_t *)&rtcData,sizeof(rtcData)-12);
      file.close();
    }
    handleRedirect();
  });
  
  
  server.on ( "/bd", []() {
    LittleFS.remove("/data");
    handleRedirect();
  });
  
  
  server.on ( "/dd", []() {
    File file = LittleFS.open("/data", "r");
    if(file){
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.sendHeader("Content-Disposition", "attachment; filename=data_021.csv");
      uint8_t temp[64];
      bool primer = true;
      uint32_t jx = file.size()/10;
      for(uint32_t ix=0;ix<jx;ix++) {
        file.read(temp,10);
        convierte_data((char *)temp);
        if(primer) {
          server.send ( 200, "application/octet-stream", (char *)temp);
          primer = false;
        } else {
          server.sendContent((char *)temp);
        }
        yield();
      }
      file.close();
      server.sendContent("");
    } else
      handleRedirect();
  });
  
  
  server.on ( "/logo",[]() {
    String contentType = mime::getContentType(rtcData.fname_logo);
    if (strlen(rtcData.fname_logo)>4 && LittleFS.exists(rtcData.fname_logo)) {
      File file = LittleFS.open(rtcData.fname_logo, "r");
      server.streamFile(file, contentType);
      file.close();
    }
  });
  server.onNotFound( HandleRoot );
  
 
  server.begin();
}

void setup() {
  #ifdef DEBUG_SERIAL
    Serial.begin(115200);
  #else
    Serial.end();
  #endif
  
  
  pinMode(RtcPin, OUTPUT);
  digitalWrite(RtcPin, LOW);
  pinMode(SensorPin, OUTPUT);
  digitalWrite(SensorPin, LOW);
  pinMode(SensorPin2, OUTPUT);
  digitalWrite(SensorPin2, LOW);
  
  
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
  
 
  if(!initRTC()) {
  
    
    #ifdef DEBUG_SERIAL
      Serial.println(F("INICIO ERRONEO, NO SE DETECTO RTC"));
    #endif
    modo_WiFi_count = 60;
    modo_WiFi();
    return;
  }
  
  
  bool instancia = false;
  if (ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData))) {
    uint32_t crcOfData = calculateCRC32((uint8_t*) &rtcData, sizeof(rtcData)-4);
    if (crcOfData == rtcData.crc32) {
      #ifdef DEBUG_SERIAL
        Serial.println(F("--- INSTANCIA ANTERIOR ENCONTRADA ---"));
      #endif
      instancia = true;
      if((epoch - rtcData.last_epoch) < 2) {
        #ifdef DEBUG_SERIAL
          Serial.println(F("BOTON PRESIONADO"));
        #endif
        if(!initFS()) {
		
         
          #ifdef DEBUG_SERIAL
            Serial.println(F("INICIO ERRONEO, ERROR EN EL SISTEMA DE ARCHIVOS"));
          #endif
        }
        modo_WiFi_count = 60;
        modo_WiFi();
        return;
      }
    }
  }
  
 
  if(!instancia) {
    #ifdef DEBUG_SERIAL
      Serial.println(F("INICIANDO INSTANCIA NUEVA"));
    #endif
    
    if(!initFS()) {
      
      #ifdef DEBUG_SERIAL
        Serial.println(F("INICIO ERRONEO, ERROR EN EL SISTEMA DE ARCHIVOS"));
      #endif
      modo_WiFi_count = 60;
      modo_WiFi();
      return;
    }
  
    if(!LittleFS.exists("/config")) {
      File file = LittleFS.open("/config", "w");
      if(file) {
        #ifdef DEBUG_SERIAL
          Serial.println(F("CREANDO ARCHIVO CONFIG"));
        #endif
        memset(&rtcData,0,sizeof(rtcData));
        memcpy_P(rtcData.magic,PSTR("PE"),2);
        rtcData.sleepy = 5;
     
        file.write((uint8_t *)&rtcData,sizeof(rtcData)-12);
        file.close();
      }
    } else {
	
      
      File file = LittleFS.open("/config", "r");
      if(file) {
        file.read((uint8_t *)&rtcData,sizeof(rtcData)-12);
        file.close();
        if(rtcData.magic[0]=='P'&&rtcData.magic[1]=='E') {
		
          
        } else {
          File file = LittleFS.open("/config", "w");
          if(file) {
            #ifdef DEBUG_SERIAL
              Serial.println(F("RE-CREANDO ARCHIVO CONFIG"));
            #endif
            memset(&rtcData,0,sizeof(rtcData));
            memcpy_P(rtcData.magic,PSTR("PE"),2);
            rtcData.sleepy = 5;
			
            
            file.write((uint8_t *)&rtcData,sizeof(rtcData)-12);
            file.close();
          }
        }
      }
    }
  }
  
  bool graba_dato = false;
  if(!instancia || (instancia && rtcData.next_epoch>epoch && (rtcData.next_epoch-epoch)<VENTANA_DATOS) || (instancia && epoch>=rtcData.next_epoch)) {
   
    if(!initFS()) {
	
    
      #ifdef DEBUG_SERIAL
        Serial.println(F("INICIO ERRONEO, ERROR EN EL SISTEMA DE ARCHIVOS"));
      #endif
      modo_WiFi_count = 60;
      modo_WiFi();
      return;
    }
    
    getDATA();
    
    if(tipo_de_error == 0 || tipo_de_error == ERR_TEMP || tipo_de_error == ERR_TEMP2) {
      File file = LittleFS.open("/data", "a+");
      if(file){
	  
       
        if((file.size()%10)!=0) {
          file.close();
          file = LittleFS.open("/data", "w+");
        }
        uint32_t px = epoch;
		
        
        if(instancia) {
          epoch = rtcData.next_epoch;
          rtcData.next_epoch += rtcData.sleepy * 60;
        } else {
          epoch = epoch + (60-(epoch % 60));
          rtcData.next_epoch = epoch + (rtcData.sleepy * 60);
        }
       

        putDATA(file);
        epoch = px;
        file.close();
        graba_dato = true;
      }
    } else {
      #ifdef DEBUG_SERIAL
        Serial.println(F("INICIO ERRONEO, NO SE DETECTO NINGUN SENSOR"));
      #endif
      modo_WiFi_count = 60;
      modo_WiFi();
      return;
    }
  }
  
  
  if(!instancia && !graba_dato)
    rtcData.next_epoch = epoch + (rtcData.sleepy * 60);
  #ifdef DEBUG_SERIAL
    uint32_t lx = rtcData.last_epoch;
  #endif
  rtcData.last_epoch = epoch;
  
  
  int32_t px = rtcData.sleepy * 60;
  if(instancia) {
    if(rtcData.next_epoch>epoch) {
      px = rtcData.next_epoch - epoch;
    }
    if(epoch>rtcData.next_epoch) {
      px -= (epoch - rtcData.next_epoch);
    }
  }
 

  if(px < 0) {
    #ifdef DEBUG_SERIAL
      Serial.println(F("‎¿‎¿‎¿‎¿CUELGUE????"));
    #endif
    px = rtcData.sleepy * 60;
    rtcData.next_epoch = epoch + (rtcData.sleepy * 60);
  }
  
  
  rtcData.crc32 = calculateCRC32((uint8_t*) &rtcData, sizeof(rtcData)-4);
  ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));
  #ifdef DEBUG_SERIAL
    Serial.printf(PSTR("last_epoch=%lu, epoch=%lu, next_epoch=%lu\r\n"), lx, epoch, rtcData.next_epoch);
  #endif
  
  
  LittleFS.end();
  
  #ifdef DEBUG_SERIAL
    Serial.printf("deepSleep(%u)",px);
  #endif
  ESP.deepSleep(px * 1e6);
}

uint32_t prev_millis = 0;
uint32_t prev_millis_vbat = 0;

void loop() { 
 
  if((millis()-prev_millis)>=1000) {
    prev_millis=millis();
    epoch = RTC.getEpoch();
    if(modo_WiFi_count == 0) {
	
    
      WiFi.mode(WIFI_OFF);
      WiFi.forceSleepBegin();
      delay(1);
	  
      
      digitalWrite(RtcPin, LOW);
	  
      
      if((epoch>rtcData.next_epoch) || (rtcData.next_epoch>epoch && (rtcData.next_epoch-epoch)<VENTANA_DATOS)) {
        getDATA();
        if(tipo_de_error == 0 || tipo_de_error == ERR_TEMP || tipo_de_error == ERR_TEMP2) {
          File file = LittleFS.open("/data", "a+");
          if(file){
            putDATA(file);
            file.close();
            rtcData.last_epoch = epoch;
            rtcData.next_epoch = epoch + rtcData.sleepy * 60;
           

            rtcData.crc32 = calculateCRC32((uint8_t*) &rtcData, sizeof(rtcData)-4);
            ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));
          }
        }
      }
      #ifdef DEBUG_SERIAL
        Serial.printf(PSTR("epoch=%lu,next_epoch=%lu\r\n"),epoch,rtcData.next_epoch);
      #endif
	  
      
      LittleFS.end();
	  
      
      uint32_t px = rtcData.next_epoch - epoch;
      #ifdef DEBUG_SERIAL
        Serial.printf("deepSleep(%u)",px);
      #endif
      ESP.deepSleep(px * 1e6);
    } else {
	
    
      if(WiFi.softAPgetStationNum() == 0)
        if(tipo_de_error == 0 || tipo_de_error == ERR_TEMP || tipo_de_error == ERR_TEMP2)
          modo_WiFi_count--;
    }
    if(epoch >= rtcData.next_epoch) {
      getDATA();
      if(tipo_de_error == 0 || tipo_de_error == ERR_TEMP || tipo_de_error == ERR_TEMP2) {
        File file = LittleFS.open("/data", "a+");
        if(file){
          putDATA(file);
          file.close();
          rtcData.last_epoch = epoch;
          rtcData.next_epoch = epoch + rtcData.sleepy * 60;
		  
          
          rtcData.crc32 = calculateCRC32((uint8_t*) &rtcData, sizeof(rtcData)-4);
          ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));
        }
      }
    }
  }
  
  
  if((millis()-prev_millis_vbat)>=5000) {
    prev_millis_vbat=millis();
	
    
    getVbat();
  }
  
}