#include <ezTime.h>

#include <ArduinoJson.h>

// builtin ESP32 libs
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include <SPIFFS.h>
#include <FS.h>

// WAVESHARE EPD LIBS
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"

#include <stdlib.h>

#include "StringStream.h"
#include "Credentials.h"

UBYTE *BlackImage, *RYImage;

const char *ssid = SSID;
const char *password = PASS;
int textCursor = 10;

WebServer server(80);
Timezone myTimeZone;

// location for timezone
#define LOCATION "Europe/Berlin"
// how many hours in the past should be shown in the grid
#define HISTORY_HRS 2
// how many hours should be shown in the grid
#define SHOW_HRS 8

#define HOUR 3600
#define MINUTE 60

#define GRID_LINE_OFFSET_LEFT 45
#define GRID_LINE_OFFSET_TOP 100
#define EVENT_OFFSET_LEFT (GRID_LINE_OFFSET_LEFT + 5)
#define EVENT_OFFSET_RIGHT (WIDTH - 5)

// Space each hour takes up
#define GRID_SPACING_H (HEIGHT - GRID_LINE_OFFSET_TOP) / SHOW_HRS
// space each minute takes up
#define GRID_SPACING_M GRID_SPACING_H / 60.0
// display rotation in degrees
#define ROTATION 90

// use specific width and height for your display
#define EPD_WIDTH EPD_7IN5B_HD_WIDTH
#define EPD_HEIGHT EPD_7IN5B_HD_HEIGHT

// WIDTH and HEIGHT will be set according to ROTATION
#if ROTATION == 90 || ROTATION == 180
#define WIDTH EPD_HEIGHT
#define HEIGHT EPD_WIDTH
#else
#define WIDTH EPD_WIDTH
#define HEIGHT EPD_HEIGHT
#endif

byte eventOverlap[HEIGHT];
byte eventOverlapDrawn[HEIGHT];

time_t gridStart = 0;

// create an empty array
String str_events = "{}";
boolean eventActive = false;

String readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: % s\r\n", path);
  File file = fs.open(path, "r");
  if (!file || file.isDirectory())
  {
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while (file.available())
  {
    fileContent += String((char)file.read());
  }
  Serial.println(fileContent);
  return fileContent;
}
void writeFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: % s\r\n", path);
  File file = fs.open(path, "w");
  if (!file)
  {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- file written");
  }
  else
  {
    Serial.println("- write failed");
  }
}

int getY(time_t t)
{
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
    if (fill == DRAW_FILL_FULL)
    {
      Paint_DrawLine(x, y, px, py, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
    else
    {
      Paint_DrawPoint(px, py, color, DOT_PIXEL_1X1, DOT_FILL_AROUND);
    }
  }
}

void centerText(int y, String text, sFONT *Font = &Font12, UWORD Color_Foreground = BLACK, UWORD Color_Background = WHITE)
{
  int l = text.length();
  int x = (WIDTH / 2) - (l * Font->Width / 2);
  Paint_DrawString_EN(x, y, text.c_str(), Font, Color_Background, Color_Foreground);
}

void drawRoundedRect(int x_left, int y_start, int x_right, int y_end, int radius, UWORD color, DOT_PIXEL Line_width, DRAW_FILL Draw_Fill)
{
  if (y_end - y_start < radius * 2)
  {
    radius = (y_end - y_start) / 2;
  }

  //draw inner rects
  if (Draw_Fill == DRAW_FILL_FULL)
  {
    Paint_DrawRectangle(x_left, y_start + radius, x_right, y_end - radius, color, Line_width, Draw_Fill);
    Paint_DrawRectangle(x_left + radius, y_start, x_right - radius, y_end, color, Line_width, Draw_Fill);
  }
  else
  {
    // clear Background first
    //draw corners
    drawArc(x_right - radius, y_end - radius, radius, WHITE, 0, 90, DRAW_FILL_FULL);      // lower right
    drawArc(x_left + radius, y_end - radius, radius, WHITE, 90, 180, DRAW_FILL_FULL);     // lower left
    drawArc(x_left + radius, y_start + radius, radius, WHITE, 180, 270, DRAW_FILL_FULL);  // top left
    drawArc(x_right - radius, y_start + radius, radius, WHITE, 270, 360, DRAW_FILL_FULL); // top right
    Paint_DrawRectangle(x_left, y_start + radius, x_right, y_end - radius, WHITE, Line_width, DRAW_FILL_FULL);
    Paint_DrawRectangle(x_left + radius, y_start, x_right - radius, y_end, WHITE, Line_width, DRAW_FILL_FULL);
    // draw border
    Paint_DrawLine(x_left + radius, y_start, x_right - radius, y_start, color, Line_width, LINE_STYLE_SOLID);
    Paint_DrawLine(x_left + radius, y_end, x_right - radius, y_end, color, Line_width, LINE_STYLE_SOLID);
    Paint_DrawLine(x_left, y_start + radius, x_left, y_end - radius, color, Line_width, LINE_STYLE_SOLID);
    Paint_DrawLine(x_right, y_start + radius, x_right, y_end - radius, color, Line_width, LINE_STYLE_SOLID);
  }

  //draw corners
  drawArc(x_right - radius, y_end - radius, radius, color, 0, 90, Draw_Fill);      // lower right
  drawArc(x_left + radius, y_end - radius, radius, color, 90, 180, Draw_Fill);     // lower left
  drawArc(x_left + radius, y_start + radius, radius, color, 180, 270, Draw_Fill);  // top left
  drawArc(x_right - radius, y_start + radius, radius, color, 270, 360, Draw_Fill); // top right
}

int drawRoundedString(int x, int y, String str, sFONT *Font, UWORD color, DRAW_FILL Draw_Fill)
{
  int font_w = Font->Width;
  int font_h = Font->Height;
  int radius = font_h / 2;
  int w = str.length() * font_w;

  drawRoundedRect(x, y, x + w + font_h, y + font_h, radius, color, DOT_PIXEL_1X1, Draw_Fill);
  Paint_DrawString_EN(x + radius, y, myTimeZone.dateTime("H:i").c_str(), &Font16, color, WHITE);
  return x + w + font_h;
}

void drawCurrentTimeMarker()
{
  int y = getY(myTimeZone.now());
  int font_offs = Font16.Height / 2;
  if (eventActive)
  {
    //cut white outline into event
    Paint_SelectImage(RYImage);
    int x_line_start = drawRoundedString(5, y - font_offs, myTimeZone.dateTime("H:i").c_str(), &Font16, WHITE, DRAW_FILL_FULL);
    Paint_DrawLine(x_line_start, y, WIDTH, y, WHITE, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    //draw marker in black
    Paint_SelectImage(BlackImage);
    x_line_start = drawRoundedString(5, y - font_offs, myTimeZone.dateTime("H:i").c_str(), &Font16, BLACK, DRAW_FILL_FULL);
    Paint_DrawLine(x_line_start, y, WIDTH, y, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  }
  else
  {
    //cut white outline into grid
    Paint_SelectImage(BlackImage);
    drawRoundedString(5, y - font_offs, myTimeZone.dateTime("H:i").c_str(), &Font16, WHITE, DRAW_FILL_FULL);
    Paint_SelectImage(RYImage);
    int x_line_start = drawRoundedString(5, y - font_offs, myTimeZone.dateTime("H:i").c_str(), &Font16, BLACK, DRAW_FILL_FULL);
    Paint_DrawLine(x_line_start, y, WIDTH, y, BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  }
}

String removeHtml(String textWithHtml)
{
  int startBracket = 0;
  while ((startBracket = textWithHtml.indexOf('<', startBracket)) != -1)
  {
    int endBracket = textWithHtml.indexOf('>', startBracket);
    if (endBracket != -1)
    {
      textWithHtml.remove(startBracket, endBracket - startBracket + 1);
    }
    else
    {
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
              sFONT *Font = &Font12,
              UWORD Color_Foreground = BLACK,
              UWORD Color_Background = WHITE)
{
  int textCursor = start_y;
  int charLimit = (x_limit - start_x) / Font12.Width;
  while (text.length() > charLimit) //|| text.indexOf("\n") != -1)
  {
    String sub;
    int br = text.indexOf("\n");
    if (br != -1)
    {
      sub = text.substring(0, br);
      text = text.substring(br + 1);
      if (sub.length() > charLimit)
      {
        sub = sub.substring(0, charLimit);
        text = sub.substring(charLimit) + "\n" + text;
      }
    }
    else if (text.length() > charLimit)
    {
      int space = text.lastIndexOf(" ", charLimit);
      int breakIndex = space != -1 ? space : charLimit;
      sub = text.substring(0, breakIndex);
      text = text.substring(breakIndex);
    }
    sub.trim();
    if (sub.length() == 0)
    {
      continue;
    }
    if (textCursor + Font->Height < y_limit)
    {
      Paint_DrawString_EN(start_x, textCursor, sub.c_str(), &Font12, Color_Foreground, Color_Background);
      Serial.println(sub);
      textCursor += Font->Height;
    }
    else
    {
      return textCursor;
    }
  }
  text.trim();
  if (text.length() > 0 && textCursor + Font->Height < y_limit)
  {
    Serial.println(text);
    Paint_DrawString_EN(start_x, textCursor, text.c_str(), Font, Color_Foreground, Color_Background);
    textCursor += Font->Height;
  }
  return textCursor;
}

void computeOverlap()
{
  memset(eventOverlap, 0, sizeof(eventOverlap));
  memset(eventOverlapDrawn, 0, sizeof(eventOverlapDrawn));

  DynamicJsonDocument doc(2048);
  StringStream eventStream((String &)str_events);
  eventActive = false;
  if (eventStream.find("\"events\":["))
  {
    do
    {
      DeserializationError error = deserializeJson(doc, eventStream);
      if (error)
      {
        return;
      }
      else
      {
        JsonObject event = doc.as<JsonObject>();
        time_t start = event["start"].as<int>();
        time_t myTimeZone_start = myTimeZone.tzTime(start, UTC_TIME);
        int startY = getY(myTimeZone_start);
        time_t end = event["end"].as<int>();
        time_t myTimeZone_end = myTimeZone.tzTime(end, UTC_TIME);
        int endY = getY(myTimeZone_end);
        if (startY < GRID_LINE_OFFSET_TOP || endY > HEIGHT)
        {
          Serial.println("Event out of range");
          continue;
        }
        for (int i = startY; i <= endY; i++)
        {
          eventOverlap[i] = eventOverlap[i] + 1;
          Serial.printf("overlap %d = %d\n", i, eventOverlap[i]);
        }
      }
    } while (eventStream.findUntil(",", "]"));
  }
}

void drawEvents()
{
  computeOverlap();
  DynamicJsonDocument doc(2048);
  StringStream eventStream((String &)str_events);
  eventActive = false;
  if (eventStream.find("\"events\":["))
  {
    do
    {
      DeserializationError error = deserializeJson(doc, eventStream);
      if (error)
      {
        // if the file didn't open, print an error:
        Serial.print(F("Error parsing JSON "));
        Serial.println(error.c_str());
      }
      else
      {
        JsonObject event = doc.as<JsonObject>();
        String title = event["title"].as<String>();
        time_t start = event["start"].as<int>();
        time_t myTimeZone_start = myTimeZone.tzTime(start, UTC_TIME);
        time_t end = event["end"].as<int>();
        time_t myTimeZone_end = myTimeZone.tzTime(end, UTC_TIME);
        String description = event["description"].as<String>();
        if (description == "null" || description == "--")
        {
          description = "";
        }
        String location = event["location"].as<String>();
        drawEvent(title, myTimeZone_start, myTimeZone_end, description);
      }
    } while (eventStream.findUntil(",", "]"));
  }
}

int getOverlap(int y_start, time_t y_end)
{
  int overlap = 0;
  for (int i = y_start; i <= y_end; i++)
  {
    if (eventOverlap[i] > overlap)
    {
      overlap = eventOverlap[i];
    }
  }
  return overlap;
}

int getOverlapDrawn(int y_start, time_t y_end)
{
  int overlapDrawn = 0;
  for (int i = y_start; i <= y_end; i++)
  {
    if (eventOverlapDrawn[i] > overlapDrawn)
    {
      overlapDrawn = eventOverlapDrawn[i];
    }
  }
  for (int i = y_start; i <= y_end; i++)
  {
    eventOverlapDrawn[i] = overlapDrawn+1;
  }
  return overlapDrawn;
}

void drawEvent(String title, time_t start, time_t end, String description)
{
  int y_start = getY(start);
  int y_end = getY(end);
  if (y_start < GRID_LINE_OFFSET_TOP || y_end > HEIGHT)
  {
    Serial.println("Event out of range");
    return;
  }

  int x_left = EVENT_OFFSET_LEFT;
  int x_right = EVENT_OFFSET_RIGHT;
  int overlap = getOverlap(y_start, y_end);
  Serial.printf("Overlap: %d", overlap);
  if (overlap > 1)
  {
    int overlapDrawn = getOverlapDrawn(y_start, y_end);
    int eventWidth = (EVENT_OFFSET_RIGHT - EVENT_OFFSET_LEFT)/overlap;
    x_left = EVENT_OFFSET_LEFT + eventWidth * overlapDrawn;
    x_right = EVENT_OFFSET_LEFT + eventWidth * (overlapDrawn+1);
  }
  boolean active = start <= myTimeZone.now() && end >= myTimeZone.now();
  DRAW_FILL fill = DRAW_FILL_EMPTY;
  UWORD Color_Foreground = BLACK;
  UWORD Color_Background = WHITE;
  Paint_SelectImage(BlackImage);
  if (active)
  {
    eventActive = true;
    Paint_SelectImage(RYImage);
    fill = DRAW_FILL_FULL;
    Color_Foreground = WHITE;
    Color_Background = BLACK;
  }
  // draw background
  int borderRadius = 10;
  drawRoundedRect(x_left, y_start, x_right, y_end, borderRadius, BLACK, DOT_PIXEL_1X1, fill);
  //draw text
  int text_x = x_left + borderRadius;
  int textCursor = y_start + borderRadius / 2;
  int x_limit =  x_right - borderRadius;
  int y_limit = y_end;
  sFONT titleFont = Font20;
  // make titlefont smaller for short events
  if (textCursor + 20 > y_limit)
  {
    titleFont = Font16;
    if (textCursor + 16 > y_limit)
    {
      titleFont = Font12;
      if (textCursor + 12 > y_limit)
      {
        titleFont = Font8;
      }
    }
  }
  textCursor = printText(title, text_x, textCursor, x_limit, y_limit, &titleFont, Color_Background, Color_Foreground);
  textCursor = printText(myTimeZone.dateTime(start, "(H:i - ") + myTimeZone.dateTime(end, "H:i)"), text_x, textCursor, x_limit, y_limit, &Font12, Color_Background, Color_Foreground);
  textCursor = printText(removeHtml(description), text_x, textCursor, x_limit, y_limit, &Font12, Color_Background, Color_Foreground);
}

void drawAgenda()
{
  clear();
  Paint_SelectImage(BlackImage);
  gridStart = myTimeZone.now() - HISTORY_HRS * HOUR - myTimeZone.minute() * MINUTE - myTimeZone.second(); // 2h in the past
  Serial.println("GridStart=" + myTimeZone.dateTime(gridStart));
  centerText(10, myTimeZone.dateTime(myTimeZone.now(), "d.m.y (~C~W W)"), &Font24, BLACK, WHITE);
  centerText(35, "Last update: " + myTimeZone.dateTime("H:i:s"), &Font20, BLACK, WHITE);

  time_t t = gridStart;
  for (int i = 0; i < SHOW_HRS; i++)
  {
    Paint_DrawString_EN(8, GRID_LINE_OFFSET_TOP + i * GRID_SPACING_H, myTimeZone.dateTime(t, "H:00").c_str(), &Font12, WHITE, BLACK);
    t += HOUR;
    //full hour line
    int hour_y = GRID_LINE_OFFSET_TOP + 5 + i * GRID_SPACING_H;
    int half_hour_y = hour_y + GRID_SPACING_H / 2;
    Paint_DrawLine(GRID_LINE_OFFSET_LEFT, hour_y, WIDTH, hour_y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    // half hour line
    Paint_DrawLine(GRID_LINE_OFFSET_LEFT, half_hour_y, WIDTH, half_hour_y, BLACK, DOT_PIXEL_1X1, LINE_STYLE_DOTTED);
  }
  drawEvents();
  drawCurrentTimeMarker();
  submit();
  deleteEvent(drawAgenda);              // cancel previous event if any
  setEvent(drawAgenda, now() + 5 * 60); // draw again in 5 mins
}

void clear()
{
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_SelectImage(RYImage);
  Paint_Clear(WHITE);
}

void doClear()
{
  clear();
  submit();
}

String str_text;

void setText()
{
  String postBody = server.arg("plain");
  //Serial.println(postBody);
  str_text = postBody;
  //clear();
  //printText(postBody);
  //submit();
}

void submit()
{
  EPD_7IN5B_HD_Display(BlackImage, RYImage);
}

void setEvents()
{
  String postBody = server.arg("plain");
  Serial.println(postBody);
  str_events = postBody;
  writeFile(SPIFFS, "/events.json", str_events.c_str());
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
void restServerRouting()
{
  server.on("/", HTTP_GET, []() {
    server.send(200, F("text/html"),
                F("Welcome to the REST Web Server"));
  });
  // handle post request
  server.on(F("/setEvents"), HTTP_POST, setEvents);
  server.on(F("/clear"), HTTP_GET, doClear);
  server.on(F("/setText"), HTTP_POST, setText);
  server.on("/getEvents", HTTP_GET, []() {
    server.send(200, F("application/json"), str_events);
  });
  server.on("/getText", HTTP_GET, []() {
    server.send(200, F("application/json"), str_text);
  });
}

// Manage not found URL
void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup()
{

  DEV_Module_Init();
  Serial.println("EPD_7IN5B_HD_test Demo\r\n");
  Serial.println("e-Paper Init and Clear...\r\n");
  EPD_7IN5B_HD_Init();
  DEV_Delay_ms(500);

  //load events from file
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error occurred mouting SPIFFS");
  }
  else
  {
    str_events = readFile(SPIFFS, "/events.json");
  }

  //Create a new image cache
  /* you have to edit the startup_stm32fxxx.s file and set a big enough heap size */
  UWORD Imagesize = ((EPD_WIDTH % 8 == 0) ? (EPD_WIDTH / 8) : (EPD_WIDTH / 8 + 1)) * EPD_HEIGHT;
  if ((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL)
  {
    Serial.println("Failed to apply for black memory...\r\n");
    while (1)
      ;
  }
  if ((RYImage = (UBYTE *)malloc(Imagesize)) == NULL)
  {
    Serial.println("Failed to apply for red memory...\r\n");
    while (1)
      ;
  }
  Serial.println("NewImage:BlackImage and RYImage\r\n");
  Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, ROTATION, WHITE);
  Paint_NewImage(RYImage, EPD_WIDTH, EPD_HEIGHT, ROTATION, WHITE);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // Activate mDNS this is used to be able to connect to the server
  // with local DNS hostmane agenda.local
  if (MDNS.begin("agenda"))
  {
    Serial.println("MDNS responder started");
  }
  Serial.println("Waiting for time sync...");
  waitForSync();
  myTimeZone.setLocation(LOCATION);

  Serial.println("It is: " + myTimeZone.dateTime());

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

void loop(void)
{
  server.handleClient();
  events();
}
