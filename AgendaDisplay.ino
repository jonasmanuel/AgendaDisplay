#include <ezTime.h>

#include <ArduinoJson.h>

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// WAVESHARE EPD LIBS
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"

#include <stdlib.h>

#include "StringStream.h"
#include "Credentials.h"

UBYTE *BlackImage, *RYImage;

const char* ssid = SSID;
const char* password = PASS;
int textCursor = 10;

WebServer server(80);
Timezone Berlin;

#define HISTORY_HRS 2
#define SHOW_HRS 8
#define HOUR 3600
#define MINUTE 60

#define GRID_LINE_OFFSET_LEFT 45
#define GRID_LINE_OFFSET_TOP 100

#define GRID_SPACING_H (HEIGHT-GRID_LINE_OFFSET_TOP)/SHOW_HRS
#define GRID_SPACING_M GRID_SPACING_H / 60.0
#define ROTATION 90

#if ROTATION == 90 || ROTATION == 180
#define WIDTH EPD_7IN5B_HD_HEIGHT
#define HEIGHT EPD_7IN5B_HD_WIDTH
#else
#define WIDTH EPD_7IN5B_HD_WIDTH
#define HEIGHT EPD_7IN5B_HD_HEIGHT
#endif

time_t gridStart = 0;

// create an empty array
String str_events = "{}";
boolean eventActive = false;

int getY(time_t t) {
  int offset = t - gridStart;
  int offset_h = offset / 3600;
  int offset_m = offset / 60 - offset_h * 60;
  int pixelOffset = offset_h * GRID_SPACING_H + ((int)offset_m * GRID_SPACING_M);
  return 5 + GRID_LINE_OFFSET_TOP + pixelOffset;
}

void drawArc(int x, int y, int radius, UWORD color, int startAngle, int EndAngle, DRAW_FILL fill)
{
  for (int i = startAngle; i < EndAngle; i++)
  {
    double radians = i * PI / 180;
    double px = x + radius * cos(radians);
    double py = y + radius * sin(radians);
    if (fill == DRAW_FILL_FULL) {
      Paint_DrawLine(x, y, px, py, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    } else {
      Paint_DrawPoint(px, py, color, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    }

  }
}

void centerText(int y, String text,  sFONT* Font = &Font12, UWORD Color_Foreground = BLACK, UWORD Color_Background = WHITE) {
  int l = text.length();
  int x = (WIDTH / 2) - (l*Font->Width / 2);
  Paint_DrawString_EN(x, y, text.c_str(), Font, Color_Background, Color_Foreground);
}

void drawRoundedRect(int x_left, int y_start, int x_right, int y_end, int radius, UWORD color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill) {
  if (y_end - y_start < radius * 2) {
    radius = (y_end - y_start) / 2 ;
  }
  // clear Background first
  Paint_DrawRectangle(x_left, y_start, x_right, y_end, WHITE, Line_width, DRAW_FILL_FULL);
  //draw inner rects
  if (Draw_Fill == DRAW_FILL_FULL) {
    Paint_DrawRectangle(x_left, y_start + radius, x_right, y_end - radius, color, Line_width, Draw_Fill);
    Paint_DrawRectangle(x_left + radius, y_start, x_right - radius, y_end, color, Line_width, Draw_Fill);
  } else {
    // draw border
    Paint_DrawLine(x_left + radius, y_start, x_right - radius, y_start, color, Line_width, LINE_STYLE_SOLID);
    Paint_DrawLine(x_left + radius, y_end, x_right - radius, y_end, color, Line_width, LINE_STYLE_SOLID);
    Paint_DrawLine(x_left, y_start + radius, x_left, y_end - radius, color, Line_width, LINE_STYLE_SOLID);
    Paint_DrawLine(x_right, y_start + radius, x_right, y_end - radius, color, Line_width, LINE_STYLE_SOLID);
  }

  //draw corners
  drawArc(x_right - radius, y_end - radius , radius, color,  0, 90, Draw_Fill); // lower right
  drawArc(x_left + radius, y_end - radius, radius, color,  90, 180, Draw_Fill); // lower left
  drawArc(x_left + radius, y_start + radius, radius, color,  180, 270, Draw_Fill); // top left
  drawArc(x_right - radius, y_start + radius, radius, color,  270, 360, Draw_Fill); // top right
}

int drawRoundedString(int x, int y, String str,  sFONT* Font, UWORD color, DRAW_FILL Draw_Fill) {
  int font_w = Font->Width;
  int font_h = Font->Height;
  int radius = font_h / 2;
  int w = str.length() * font_w;

  drawRoundedRect(x, y , x + w + font_h, y + font_h, radius, color, DOT_PIXEL_1X1, Draw_Fill);
  Paint_DrawString_EN(x + radius, y , Berlin.dateTime("H:i").c_str(), &Font16, color, WHITE);
  return x + w + font_h;
}

void drawCurrentTimeMarker() {
  int y = getY(Berlin.now());
  int font_offs = Font16.Height / 2;
  if (eventActive) {
    //cut white outline into event
    Paint_SelectImage(RYImage);
    int x_line_start = drawRoundedString(5, y - font_offs, Berlin.dateTime("H:i").c_str(), &Font16, WHITE, DRAW_FILL_FULL);
    Paint_DrawLine(x_line_start, y, WIDTH, y, WHITE, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    //draw marker in black
    Paint_SelectImage(BlackImage);
    x_line_start = drawRoundedString(5, y - font_offs, Berlin.dateTime("H:i").c_str(), &Font16, BLACK, DRAW_FILL_FULL);
    Paint_DrawLine(x_line_start, y, WIDTH, y, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  } else {
    //cut white outline into grid
    Paint_SelectImage(BlackImage);
    drawRoundedString(5, y - font_offs, Berlin.dateTime("H:i").c_str(), &Font16, WHITE, DRAW_FILL_FULL);
    Paint_SelectImage(RYImage);
    int x_line_start = drawRoundedString(5, y - font_offs, Berlin.dateTime("H:i").c_str(), &Font16, BLACK, DRAW_FILL_FULL);
    Paint_DrawLine(x_line_start, y, WIDTH, y, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  }
}


String removeHtml(String textWithHtml) {
  int startBracket = 0;
  while ((startBracket = textWithHtml.indexOf('<', startBracket)) != -1) {
    int endBracket = textWithHtml.indexOf('>', startBracket);
    if (endBracket != -1) {
      textWithHtml.remove(startBracket, endBracket - startBracket + 1);
    } else {
      break;
    }
  }
  return textWithHtml;
}

int printText(String text,
              int start_x = 0,
              int start_y = 0,
              int x_limit = WIDTH,
              int y_limit = HEIGHT,
              sFONT* Font = &Font12,
              UWORD Color_Foreground = BLACK,
              UWORD Color_Background = WHITE) {
  int textCursor = start_y;
  int charLimit = (x_limit - start_x) / Font12.Width;
  while (text.length() > charLimit || text.indexOf('\n') != -1) {
    String sub;
    int br = text.indexOf('\n');
    if (br != -1) {
      sub = text.substring(0, br);
      text = text.substring(br + 1);
    }
    if (text.length() > charLimit) {
      sub = text.substring(0, charLimit);
      text = text.substring(charLimit);
    }
    if (textCursor + Font->Height < y_limit) {
      Paint_DrawString_EN(start_x, textCursor, sub.c_str(), &Font12, Color_Foreground, Color_Background);
      Serial.println(sub);
      textCursor += Font->Height;
    } else {
      return textCursor;
    }

  }
  if (textCursor + Font->Height < y_limit) {
    Serial.println(text);
    Paint_DrawString_EN(start_x, textCursor, text.c_str(), Font, Color_Foreground, Color_Background);
    textCursor += Font->Height;
  }
  return textCursor;
}

void drawEvents() {
  DynamicJsonDocument doc(2048);
  StringStream eventStream((String &)str_events);
  eventActive = false;
  if (eventStream.find("\"events\":[")) {
    do {
      DeserializationError error = deserializeJson(doc, eventStream);
      if (error) {
        // if the file didn't open, print an error:
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str());
      } else {
        JsonObject event = doc.as<JsonObject>();
        String title = event["title"].as<String>();
        time_t start = event["start"].as<int>();
        time_t berlin_start = Berlin.tzTime(start, UTC_TIME);
        time_t end = event["end"].as<int>();
        time_t berlin_end = Berlin.tzTime(end, UTC_TIME);
        String description = event["description"].as<String>();
        String location = event["location"].as<String>();
        drawEvent(title, berlin_start, berlin_end, description);
      }
    } while (eventStream.findUntil(",", "]"));
  }
}




void drawEvent(String title, time_t start, time_t end, String description) {
  int y_start = getY(start);
  int y_end = getY(end);
  if (y_start < 0 || y_end > HEIGHT) {
    Serial.println("Event out of range");
    return;
  }
  boolean active = start <= Berlin.now() && end >= Berlin.now();
  DRAW_FILL fill = DRAW_FILL_EMPTY;
  UWORD Color_Foreground = BLACK;
  UWORD Color_Background = WHITE;
  Paint_SelectImage(BlackImage);
  if (active) {
    eventActive = true;
    Paint_SelectImage(RYImage);
    fill = DRAW_FILL_FULL;
    Color_Foreground = WHITE;
    Color_Background = BLACK;
  }
  int x_left = GRID_LINE_OFFSET_LEFT + 5;
  int x_right = WIDTH - 5;
  // draw background
  int borderRadius = 10;
  drawRoundedRect(x_left, y_start, x_right, y_end, borderRadius, BLACK, DOT_PIXEL_1X1, fill);
  //draw text
  int text_x = GRID_LINE_OFFSET_LEFT + borderRadius;
  int textCursor = y_start + borderRadius;
  textCursor = printText(title , text_x , textCursor, x_right - borderRadius, y_end - borderRadius, &Font20, Color_Background, Color_Foreground);
  textCursor = printText(Berlin.dateTime(start, "(H:i - ") + Berlin.dateTime(end, "H:i)"), text_x , textCursor, x_right - borderRadius, y_end - borderRadius, &Font12, Color_Background, Color_Foreground);
  textCursor = printText(removeHtml(description), text_x, textCursor, x_right - borderRadius, y_end - borderRadius, &Font12, Color_Background, Color_Foreground);
}

void drawAgenda() {
  clear();
  Paint_SelectImage(BlackImage);
  gridStart = Berlin.now() - HISTORY_HRS * HOUR - Berlin.minute() * MINUTE - Berlin.second(); // 2h in the past
  Serial.println("GridStart=" + Berlin.dateTime(gridStart));
  centerText(10,  Berlin.dateTime(Berlin.now(), "d.m.y (~C~W W)"), &Font24, BLACK, WHITE);
  centerText(35, "Last update: " + Berlin.dateTime("H:i:s"), &Font20, BLACK,  WHITE);
  time_t t = gridStart;
  for (int i = 0; i < 8; i++) {
    Paint_DrawString_EN(8, GRID_LINE_OFFSET_TOP + i * GRID_SPACING_H, Berlin.dateTime(t, "H:00").c_str(), &Font12, WHITE, BLACK);
    Paint_DrawLine(GRID_LINE_OFFSET_LEFT, GRID_LINE_OFFSET_TOP + 5 + i * GRID_SPACING_H, WIDTH, GRID_LINE_OFFSET_TOP + 5 + i * GRID_SPACING_H, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    t = t + HOUR; //+1h
  }
  drawEvents();
  drawCurrentTimeMarker();
  submit();
  deleteEvent(drawAgenda); // cancel previous event if any
  setEvent(drawAgenda, now() + 5 * 60); // draw again in 5 mins
}



void clear() {
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_SelectImage(RYImage);
  Paint_Clear(WHITE);
}

void doClear() {
  clear();
  submit();
}



void setText() {
  String postBody = server.arg("plain");
  clear();
  printText(postBody);
  submit();
}

void submit() {
  EPD_7IN5B_HD_Display(BlackImage, RYImage);
}

void setEvents() {
  String postBody = server.arg("plain");
  Serial.println(postBody);
  str_events = postBody;
  // Create the response
  // To get the status of the result you can get the http status so
  DynamicJsonDocument doc(2048);
  doc["status"] = "OK";
  Serial.print(F("Stream..."));
  String buf;
  serializeJson(doc, buf);
  server.send(201, F("application/json"), buf);

  drawAgenda();

  Serial.println(F("done."));
}

// Define routing
void restServerRouting() {
  server.on("/", HTTP_GET, []() {
    server.send(200, F("text/html"),
                F("Welcome to the REST Web Server"));
  });
  // handle post request
  server.on(F("/setEvents"), HTTP_POST, setEvents);
  server.on(F("/clear"), HTTP_GET, doClear);
  server.on(F("/setText"), HTTP_POST, setText);

}

// Manage not found URL
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup() {

  DEV_Module_Init();
  Serial.println("EPD_7IN5B_HD_test Demo\r\n");
  Serial.println("e-Paper Init and Clear...\r\n");
  EPD_7IN5B_HD_Init();
  DEV_Delay_ms(500);


  //Create a new image cache
  /* you have to edit the startup_stm32fxxx.s file and set a big enough heap size */
  UWORD Imagesize = ((EPD_7IN5B_HD_WIDTH % 8 == 0) ? (EPD_7IN5B_HD_WIDTH / 8 ) : (EPD_7IN5B_HD_WIDTH / 8 + 1)) * EPD_7IN5B_HD_HEIGHT;
  if ((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.println("Failed to apply for black memory...\r\n");
    while (1);
  }
  if ((RYImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.println("Failed to apply for red memory...\r\n");
    while (1);
  }
  Serial.println("NewImage:BlackImage and RYImage\r\n");
  Paint_NewImage(BlackImage, EPD_7IN5B_HD_WIDTH, EPD_7IN5B_HD_HEIGHT , ROTATION, WHITE);
  Paint_NewImage(RYImage, EPD_7IN5B_HD_WIDTH, EPD_7IN5B_HD_HEIGHT , ROTATION, WHITE);


  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");


  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane esp8266.local
  if (MDNS.begin("agenda")) {
    Serial.println("MDNS responder started");
  }
  Serial.println("Waiting for time sync...");
  waitForSync();
  Berlin.setLocation("Europe/Berlin");

  Serial.println("It is: " + Berlin.dateTime());

  // Set server routing
  restServerRouting();
  // Set not found response
  server.onNotFound(handleNotFound);
  // Start server
  server.begin();
  Serial.println("HTTP server started");
  //Paint_SetRotate(90);
  drawAgenda();

}

/* The main loop -------------------------------------------------------------*/

void loop(void) {
  server.handleClient();
  events();
}