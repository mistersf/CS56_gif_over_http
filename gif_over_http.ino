/*
This example creates a client object that connects and transfers
data using always SSL.

It is compatible with the methods normally related to plain
connections, like client.connect(host, port).

Written by Arturo Guadalupi
last revision November 2015

*/

#include <SPI.h>
#include <WiFiNINA.h>
#include <Adafruit_Protomatter.h>
#include <AnimatedGIF.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>

#include "flash_config.h"
Adafruit_SPIFlash flash(&flashTransport);

// file system object from SdFat
FatVolume fatfs;

uint8_t rgbPins[] = { 7, 8, 9, 10, 11, 12 };
uint8_t addrPins[] = { 17, 18, 19, 20, 21 };
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;


Adafruit_Protomatter matrix(
  64,                         // Width of matrix (or matrix chain) in pixels
  4,                          // Bit depth, 1-6
  1, rgbPins,                 // # of matrix chains, array of 6 RGB pins for each
  5, addrPins,                // # of address pins (height is inferred), array of pins
  clockPin, latchPin, oePin,  // Other matrix control pins
  false);                     // No double-buffering here (see "doublebuffer" example)


#include "arduino_secrets.h"
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;           // your network key index number (needed only for WEP)

int status = WL_IDLE_STATUS;
// if you don't want to use DNS (and reduce your sketch size)
// use the numeric IP instead of the name for the server:
IPAddress server(34, 94, 111, 104);  // numeric IP for Google (no DNS)
// char server[] = "http://34.94.111.104/";  // name address for Google (using DNS)

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
WiFiClient client;

// Auxiliar variables to store the current output state
String output26State = "off";
String output27State = "off";

// Assign output variables to GPIO pins
const int output26 = 26;
const int output27 = 27;

AnimatedGIF GIF;
File GIFfile;
int16_t xPos = 0, yPos = 0;  // Top-left pixel coord of GIF in matrix space

// FILE ACCESS FUNCTIONS REQUIRED BY ANIMATED GIF LIB ----------------------

// Pass in ABSOLUTE PATH of GIF file to open
void *GIFOpenFile(const char *filename, int32_t *pSize) {
  printf("GIFOpenFile");
  GIFfile = fatfs.open(filename);
  if (GIFfile) {
    *pSize = GIFfile.size();
    return (void *)&GIFfile;
  }
  return NULL;
}

void GIFCloseFile(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  if (f) f->close();
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
  int32_t iBytesRead = iLen;
  File *f = static_cast<File *>(pFile->fHandle);
  // If a file is read all the way to last byte, seek() stops working
  if ((pFile->iSize - pFile->iPos) < iLen)
    iBytesRead = pFile->iSize - pFile->iPos - 1;  // ugly work-around
  if (iBytesRead <= 0) return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

// Draw one line of image to matrix back buffer
void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y;

  y = pDraw->iY + pDraw->y;  // current line in image

  // Vertical clip
  int16_t screenY = yPos + y;  // current row on matrix
  if ((screenY < 0) || (screenY >= matrix.height())) return;

  usPalette = pDraw->pPalette;

  s = pDraw->pPixels;
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) {  // if transparency used
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + pDraw->iWidth;
    x = 0;
    iCount = 0;  // count non-transparent pixels
    while (x < pDraw->iWidth) {
      c = ucTransparent - 1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent) {  // done, stop
          s--;                     // back up to treat it like transparent
        } else {                   // opaque
          *d++ = usPalette[c];
          iCount++;
        }
      }              // while looking for opaque pixels
      if (iCount) {  // any opaque pixels?
        span(usTemp, xPos + pDraw->iX + x, screenY, iCount);
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
          iCount++;
        else
          s--;
      }
      if (iCount) {
        x += iCount;  // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate 8-bit pixels through RGB565 palette (already byte reversed)
    for (x = 0; x < pDraw->iWidth; x++)
      usTemp[x] = usPalette[*s++];
    span(usTemp, xPos + pDraw->iX, screenY, pDraw->iWidth);
  }
}

// Copy a horizontal span of pixels from a source buffer to an X,Y position
// in matrix back buffer, applying horizontal clipping. Vertical clipping is
// handled in GIFDraw() above -- y can safely be assumed valid here.
void span(uint16_t *src, int16_t x, int16_t y, int16_t width) {
  if (x >= matrix.width()) return;  // Span entirely off right of matrix
  int16_t x2 = x + width - 1;       // Rightmost pixel
  if (x2 < 0) return;               // Span entirely off left of matrix
  if (x < 0) {                      // Span partially off left of matrix
    width += x;                     // Decrease span width
    src -= x;                       // Increment source pointer to new start
    x = 0;                          // Leftmost pixel is first column
  }
  if (x2 >= matrix.width()) {  // Span partially off right of matrix
    width -= (x2 - matrix.width() + 1);
  }
  if (matrix.getRotation() == 0) {
    memcpy(matrix.getBuffer() + y * matrix.width() + x, src, width * 2);
  } else {
    while (x <= x2) {
      matrix.drawPixel(x++, y, *src++);
    }
  }
}

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }

  // Initialize matrix...
  ProtomatterStatus matrix_status = matrix.begin();
  Serial.print("Protomatter begin() status: ");
  Serial.println((int)matrix_status);
  if (matrix_status != PROTOMATTER_OK) {
    // DO NOT CONTINUE if matrix setup encountered an error.
    for (;;)
      ;
  }

  // Copy data to matrix buffers
  // Initialize the output variables as outputs
  pinMode(output26, OUTPUT);
  pinMode(output27, OUTPUT);
  // Set outputs to LOW
  digitalWrite(output26, LOW);
  digitalWrite(output27, LOW);

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  Serial.println("Connected to WiFi");
  printWiFiStatus();

  Serial.println("\nStarting connection to server...");
  // if you get a connection, report back via serial:
  if (client.connect(server, 8000)) {
    Serial.println("connected to server");
    // Make a HTTP request:
    client.println("GET /static/board/GIFONE.gif HTTP/1.1");
    client.println("Host: 34.94.111.104/");
    client.println("Connection: close");
    client.println();
  }

  GIF.begin(LITTLE_ENDIAN_PIXELS);


  // Initialize flash library and check its chip ID.
  if (!flash.begin()) {
    Serial.println("Error, failed to initialize flash chip!");
    while (1) {
    }
  }
  Serial.print("Flash chip JEDEC ID: 0x");
  Serial.println(flash.getJEDECID(), HEX);

  // First call begin to mount the filesystem.  Check that it returns true
  // to make sure the filesystem was mounted.
  if (!fatfs.begin(&flash)) {
    Serial.println("Failed to mount filesystem!");
    Serial.println("Was CircuitPython loaded on the board first to create the "
                   "filesystem?");
    while (1) {
    }
  }
  Serial.println("Mounted filesystem!");
  fatfs.remove("test.gif");
}

bool inGIF = false;
char separator[] = "\r\n\r\n";

void loop() {

  File32 data = fatfs.open("test.gif", FILE_WRITE);
  // if there are incoming bytes available
  // from the server, read them and print them:
  while (client.available()) {
    if (!inGIF) {
      inGIF = client.find(separator);
    } else {
      char c = client.read();
      Serial.write(c);
      data.write(c);
      // matrix.print(c);
      // matrix.show();
    }
  }


  // if the server's disconnected, stop the client:
  if (!client.connected()) {
    Serial.println();
    Serial.println("disconnecting from server.");
    client.stop();
    data.close();
    Serial.printf("\nOpening file '%s'\n", "test.gif");
    if (GIF.open("/test.gif", GIFOpenFile, GIFCloseFile,
                 GIFReadFile, GIFSeekFile, GIFDraw)) {
      Serial.println("File opened successfully.");
      matrix.fillScreen(0);
      Serial.printf("GIF dimensions: %d x %d\n",
                    GIF.getCanvasWidth(), GIF.getCanvasHeight());
      xPos = (matrix.width() - GIF.getCanvasWidth()) / 2;  // Center on matrix
      yPos = (matrix.height() - GIF.getCanvasHeight()) / 2;
    }
    // do nothing forevermore:
    while (true) {
      GIF.playFrame(true, NULL);  // Auto resets to start if needed
      matrix.show();
    }
  }
}


void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}