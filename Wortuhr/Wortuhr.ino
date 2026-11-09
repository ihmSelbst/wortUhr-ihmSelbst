/*
 * Wortuhr von ihmSelbst – Firmware 4.0.2
 * Wortuhr mit 11x10 Buchstaben-Matrix + 4 Minutenpunkten (114x WS2812B)
 *
 * Copyright (c) 2026 ihmSelbst
 * Lizenz: MIT – Nutzung, Änderung und Weitergabe erlaubt, solange dieser
 * Hinweis erhalten bleibt. Es gibt keine Gewährleistung, Nutzung auf eigene Gefahr.
 *
 * Hardware
 *   Board:   ESP32-WROOM-32 oder ESP32-S3 N16R8 (wird beim Start erkannt)
 *   LEDs:    Daten GPIO 18, Strombegrenzung per Default 1000 mA (einstellbar)
 *   Taster:  GPIO 19 (Pin im Webinterface änderbar)
 *   LDR:     GPIO 34 für die automatische Helligkeit
 *
 * Funktionen
 *   - Uhrzeit per NTP (CET/CEST), Übergänge, Nachtprofil, Wecker mit Vorlauf
 *   - ca. 70 Effekte für Vorder- und Hintergrund, eigene Minutenpunkt-Effekte,
 *     Paletten, Favoriten, 8 eigene Presets
 *   - Wetter über Open-Meteo oder per MQTT aus Home Assistant
 *   - Hinweise für Waschmaschine, Trockner, Spülmaschine, Klingel, Müll,
 *     Feiertage und eigene Termine
 *   - Spiele: Pong, Tetris, Snake, Flappy, Breakout, Dino, Invaders,
 *     Connect4, Life, Simon, Frogger, Asteroids, Maze, 2048
 *
 * Bedienung
 *   Erster Start:  WLAN-Portal "Wortuhr_…" öffnet sich, Passwort steht auf der Uhr.
 *   Weboberfläche: http://wortuhr.local  (Konfiguration unter /konfiguration)
 *   Firmware-Update: /update (Benutzer/Passwort wie in der Konfiguration)
 *   WLAN-Reset:    3x kurz Strom aus/an innerhalb von 5 s nach dem Einschalten
 *
 * Home Assistant
 *   MQTT mit Auto-Discovery, Basis-Topic standardmäßig wortuhr/uhr
 *   Meldungen: <basis>/message/set, <basis>/event/set, <basis>/calendar/set
 *
 * Hinweis: Das Webinterface nutzt kein HTTPS. Uhr nicht aus dem Internet
 * erreichbar machen.
 */

#include <FastLED.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPUpdateServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <MQTT.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <time.h>

bool wifiPowerCycleArmed=false;
unsigned long wifiPowerCycleArmedAt=0;
bool wifiPowerCycleResetRequested=false;

#define NUM_LEDS       114
#define MATRIX_W       11
#define MATRIX_H       10
#define DATA_PIN       18
#define BUTTON_PIN     19
#define LDR_PIN        34
#define LED_TYPE       WS2812B
#define COLOR_ORDER    GRB
#define MILLI_AMPS     1000
#define EEPROM_SIZE    512

#if CONFIG_IDF_TARGET_ESP32S3
  #define WORTUHR_BOARD_NAME "ESP32-S3 N16R8"
#else
  #define WORTUHR_BOARD_NAME "ESP32-WROOM-32"
#endif
#ifndef WORTUHR_ENABLE_GAMES
  #define WORTUHR_ENABLE_GAMES 1
#endif
#ifndef WORTUHR_ENABLE_SOUND_REACTIVE
  #define WORTUHR_ENABLE_SOUND_REACTIVE 0
#endif
#define WORTUHR_FW_VERSION "4.0.2"

static const float WEATHER_LAT = 52.5200f;
static const float WEATHER_LON = 13.4050f;

WebServer server(80);
WebSocketsServer wsServer(81);
HTTPUpdateServer updater;
WiFiManager wifiManager;
WiFiClient mqttNet;
MQTTClient mqttClient(2048);
CRGB leds[NUM_LEDS];

const uint8_t matrixMap[11][11] = {
  {0,1,2,3,4,5,6,7,8,9,10},
  {21,20,19,18,17,16,15,14,13,12,11},
  {22,23,24,25,26,27,28,29,30,31,32},
  {43,42,41,40,39,38,37,36,35,34,33},
  {44,45,46,47,48,49,50,51,52,53,54},
  {65,64,63,62,61,60,59,58,57,56,55},
  {66,67,68,69,70,71,72,73,74,75,76},
  {87,86,85,84,83,82,81,80,79,78,77},
  {88,89,90,91,92,93,94,95,96,97,98},
  {109,108,107,106,105,104,103,102,101,100,99},
  {110,111,112,113,0,0,0,0,0,0,0}
};

enum Effect : uint8_t {
  FX_SOLID, FX_BREATHE, FX_RAINBOW, FX_WAVES, FX_TWINKLE,
  FX_MATRIX, FX_PLASMA, FX_FIRE, FX_SCANNER, FX_AURORA,
  FX_OCEAN, FX_LAVA, FX_CONFETTI, FX_COMET, FX_RIPPLE,
  FX_GRADIENT, FX_CANDLE, FX_STARS, FX_COLORSHIFT, FX_HEARTBEAT,
  FX_NOISE, FX_BLACK_HOLE, FX_BLOBS, FX_BPM, FX_COLORED_BURSTS,
  FX_COLOR_TWINKLES, FX_CRAZY_BEES, FX_DNA, FX_DNA_SPIRAL,
  FX_DRIFT, FX_DRIFT_ROSE, FX_FIRE_2012, FX_FIRE_NOISE,
  FX_FUNKY_PLANK, FX_LISSAJOUS, FX_METABALLS, FX_PLASMA_BALLS,
  FX_PULSER, FX_SINDOTS,
  FX_PRIDE, FX_COLORWAVES, FX_SINELON, FX_JUGGLE, FX_PACIFICA,
  FX_POLAR_LIGHTS, FX_LAKE, FX_FLOW, FX_SWIRL, FX_SQUARED_SWIRL,
  FX_HIPHOTIC, FX_DISTORTION_WAVES, FX_OCTOPUS, FX_SOAP, FX_JULIA,
  FX_METEOR, FX_SUNRISE, FX_FIREWORKS, FX_RAIN, FX_POPCORN,
  FX_DRIP, FX_BOUNCING_BALLS, FX_HALLOWEEN_EYES, FX_GAME_OF_LIFE,

  FX_GRADIENT_DYNAMIC, FX_CHASE, FX_THEATER_CHASE, FX_GLITTER,
  FX_LIGHTNING, FX_RUNNING_LIGHTS, FX_FAIRY,
  FX_COUNT
};
enum DisplayMode : uint8_t { MODE_CLOCK, MODE_TEMPERATURE, MODE_SCROLL };
enum GameMode : uint8_t { GAME_NONE, GAME_PONG, GAME_PONG2, GAME_TETRIS, GAME_SNAKE, GAME_FLAPPY, GAME_BREAKOUT, GAME_DINO, GAME_INVADERS, GAME_CONNECT4, GAME_LIFE, GAME_SIMON, GAME_FROGGER, GAME_ASTEROIDS, GAME_MAZE, GAME_2048 };
enum OverlayMode : uint8_t { OVERLAY_NONE, OVERLAY_WASHER, OVERLAY_DRYER, OVERLAY_DISHWASHER, OVERLAY_DOORBELL, OVERLAY_WASTE, OVERLAY_WEATHER, OVERLAY_ROOM_CLIMATE, OVERLAY_ALARM, OVERLAY_HOLIDAY };

struct Settings {
  uint8_t magic;
  uint8_t power;
  uint8_t brightnessMin;
  uint8_t brightnessMax;
  uint8_t foregroundEffect;
  uint8_t backgroundEffect;
  uint8_t legacyEffectSpeed;
  uint8_t scrollSpeed;
  uint8_t backgroundLevel;
  uint8_t showTemperature;
  uint8_t fgR, fgG, fgB;
  uint8_t bgR, bgG, bgB;
  uint8_t mqttEnabled;
  uint8_t mqttDiscovery;
  uint16_t mqttPort;
  char mqttHost[40];
  char mqttUser[24];
  char mqttPass[32];
  char mqttBase[32];
  uint8_t previewEnabled;
  uint8_t presetsEnabled;
  uint8_t notificationsEnabled;
  uint8_t calendarEnabled;
  uint8_t gamesEnabled;
  uint8_t highScoresEnabled;
  uint8_t weatherAnimationsEnabled;
  uint8_t roomClimateEnabled;
  uint8_t activePreset;
  uint16_t highSnake;
  uint16_t highTetris;
  uint16_t highPong;
  uint8_t weatherSource;
  float weatherLat;
  float weatherLon;
  char weatherLocation[32];
  uint8_t minuteSettingsVersion;
  uint8_t minuteEffect;
  uint8_t minuteLevel;
  uint8_t minuteR, minuteG, minuteB;
  uint8_t flappySettingsVersion;
  uint16_t highFlappy;

  uint16_t version;
  uint8_t speedSettingsVersion;
  uint8_t foregroundSpeed;
  uint8_t backgroundSpeed;
  uint8_t minuteSpeed;
  uint8_t finalSettingsVersion;
  char apPass[17];
  char adminPass[33];
  char hostName[33];
  uint16_t powerLimitMa;
  uint16_t gameTimeoutSec;

  uint8_t adminUserSettingsVersion;
  char adminUser[17];

  uint8_t ldrSettingsVersion;
  uint16_t ldrDarkRaw;
  uint16_t ldrBrightRaw;
  uint8_t ldrSmoothing;

  uint8_t v32SettingsVersion;
  uint8_t transitionEnabled;
  uint8_t transitionType;
  uint8_t circadianEnabled;
  uint8_t rotationEnabled;
  uint8_t rotationMinutes;
  uint8_t buttonMultiEnabled;
  uint8_t regionalEnabled;
  uint8_t regionalStyle;

  uint8_t fxParamSettingsVersion;
  uint8_t foregroundIntensity, foregroundTrail, foregroundSize;
  uint8_t backgroundIntensity, backgroundTrail, backgroundSize;
  uint8_t minuteIntensity, minuteTrail, minuteSize;

  uint8_t v4SettingsVersion;
  uint8_t foregroundPalette, backgroundPalette, minutePalette;
  uint8_t favoriteFx[12];
  uint8_t rotationFavoritesOnly, rotationNoRepeat;
  uint8_t adaptiveFps, gammaEnabled, ldrAdaptiveEffects, ambientMode;
  uint8_t ambientWordLevel;
  uint8_t calibrationR, calibrationG, calibrationB;
  uint8_t dayProfileEnabled, quietStartHour, quietEndHour;
  uint8_t soundReactiveEnabled;
};

struct SettingsV27Middle {
  uint8_t magic;uint16_t version;uint8_t power,brightnessMin,brightnessMax,foregroundEffect,backgroundEffect,effectSpeed,scrollSpeed,backgroundLevel,showTemperature;
  uint8_t fgR,fgG,fgB,bgR,bgG,bgB,mqttEnabled,mqttDiscovery;uint16_t mqttPort;char mqttHost[40],mqttUser[24],mqttPass[32],mqttBase[32];
  uint8_t previewEnabled,presetsEnabled,notificationsEnabled,calendarEnabled,gamesEnabled,highScoresEnabled,weatherAnimationsEnabled,roomClimateEnabled,activePreset;
  uint16_t highSnake,highTetris,highPong;uint8_t weatherSource;float weatherLat,weatherLon;char weatherLocation[32];
  uint8_t minuteSettingsVersion,minuteEffect,minuteLevel,minuteR,minuteG,minuteB,flappySettingsVersion;uint16_t highFlappy;
};

static_assert(sizeof(Settings)<=EEPROM_SIZE,"Settings sind groesser als der EEPROM-Bereich");
static_assert(sizeof(SettingsV27Middle)<=EEPROM_SIZE,"Alte Settings sind groesser als der EEPROM-Bereich");

Settings cfg;
const uint8_t SETTINGS_MAGIC = 0xA9;
const uint16_t SETTINGS_VERSION = 0x0300;
const uint8_t SPEED_SETTINGS_VERSION = 0xA5;
const uint8_t FINAL_SETTINGS_VERSION = 0x31;
const uint8_t ADMIN_USER_SETTINGS_VERSION = 0xA6;
const uint8_t LDR_SETTINGS_VERSION = 0x4C;
const uint8_t V32_SETTINGS_VERSION = 0x32;
const uint8_t FX_PARAM_SETTINGS_VERSION = 0x42;
const uint8_t V4_SETTINGS_VERSION = 0x40;

struct FeatureDef { const char* key; const char* name; uint8_t* value; };
FeatureDef newFeatures[] = {
  {"transitions","Übergangsanimationen",&cfg.transitionEnabled},
  {"circadian","Zirkadiane Farbtemperatur",&cfg.circadianEnabled},
  {"effect_rotation","Effekt-Rotation",&cfg.rotationEnabled},
  {"button_multi","Taster-Mehrfachfunktionen",&cfg.buttonMultiEnabled},
  {"regional_time","Regionale Zeitansage",&cfg.regionalEnabled}
};

bool activeMask[NUM_LEDS];
DisplayMode displayMode = MODE_CLOCK;
unsigned long lastFrameMs = 0;
unsigned long lastWeatherMs = 0;
unsigned long nextWeatherAttemptMs = 0;
unsigned long lastLdrMs = 0;
unsigned long temperatureCycleStartedMs = 0;
int32_t ldrAverage8 = -1;
uint16_t currentLdrRaw = 0;
uint8_t currentBrightness = 0;
unsigned long ledTestStartedMs = 0, ledTestUntilMs = 0;
unsigned long wordTestUntilMs = 0;
bool settingsSavePending = false;
unsigned long settingsSaveAtMs = 0;
uint16_t renderedFrames = 0;
uint8_t measuredFps = 0;
unsigned long lastFpsMeasureMs = 0;
bool wifiResetPending = false;
unsigned long wifiResetAt = 0;

struct tm nowTm;
bool nowTmValid = false;
unsigned long lastTmUpdateMs = 0;

enum WifiUiMode : uint8_t { WIFI_UI_CONNECTING, WIFI_UI_PORTAL, WIFI_UI_SUCCESS, WIFI_UI_NONE };
WifiUiMode wifiUiMode = WIFI_UI_CONNECTING;
String wifiApName;
String wifiUiText;
int wifiUiX = MATRIX_W;
uint8_t wifiPortalPage = 0;
unsigned long lastWifiUiStepMs = 0;
bool wifiPortalActive = false;
bool wifiWasConnected = false;
bool webServerStarted = false;
bool wifiSuccessShown = false;
unsigned long wifiSuccessUntilMs = 0;
bool mdnsStarted = false;
unsigned long lastPortalClientMs = 0;
bool restartPending = false;
unsigned long restartAtMs = 0;

unsigned long lastMqttAttemptMs = 0;
unsigned long mqttRetryDelayMs = 5000;
unsigned long lastDiagnosticPublishMs = 0;
bool mqttDiscoveryPublished = false;
enum MqttDirty : uint32_t {
  MD_POWER=1UL<<0,MD_TEMP_MODE=1UL<<1,MD_FG=1UL<<2,MD_BG=1UL<<3,MD_MIN_FX=1UL<<4,MD_MIN_LEVEL=1UL<<5,
  MD_WEATHER_SOURCE=1UL<<6,MD_GAME=1UL<<7,MD_PRESET=1UL<<8,MD_WEATHER=1UL<<9,MD_ROOM_TEMP=1UL<<10,MD_ROOM_HUM=1UL<<11,
  MD_F_PREVIEW=1UL<<12,MD_F_PRESETS=1UL<<13,MD_F_NOTIFICATIONS=1UL<<14,MD_F_CALENDAR=1UL<<15,MD_F_GAMES=1UL<<16,
  MD_F_HIGHSCORES=1UL<<17,MD_F_WEATHER_ANIM=1UL<<18,MD_F_ROOM_CLIMATE=1UL<<19,MD_HIGH_SNAKE=1UL<<20,
  MD_HIGH_TETRIS=1UL<<21,MD_HIGH_PONG=1UL<<22,MD_HIGH_FLAPPY=1UL<<23,
  MD_FG_SPEED=1UL<<24,MD_BG_SPEED=1UL<<25,MD_MIN_SPEED=1UL<<26,MD_BG_LEVEL=1UL<<27,MD_LIGHT=1UL<<28,MD_POWER_LIMIT=1UL<<29,MD_TIMER=1UL<<30,
  MD_EXTRA=1UL<<31
};
uint32_t mqttDirtyMask = 0;

uint32_t foregroundPhase = 0, backgroundPhase = 0, minutePhase = 0;
uint16_t foregroundPhaseRemainder = 0, backgroundPhaseRemainder = 0, minutePhaseRemainder = 0;
unsigned long lastEffectPhaseMs = 0;

GameMode gameMode = GAME_NONE;
CRGB gamePixels[MATRIX_H][MATRIX_W];
unsigned long lastGameStepMs = 0;
unsigned long lastGameInputMs = 0;
uint16_t gameScore = 0;
String gameStatus = "Uhr";
OverlayMode overlayMode = OVERLAY_NONE;
unsigned long overlayStartedMs = 0, overlayUntilMs = 0;
bool weatherScrollPending = false;
bool wasteScrollPending = false;
bool wastePending = false;
char wasteTypes[24] = "";
uint8_t wasteMask = 0;
unsigned long lastWasteReminderMs = 0;
float roomTemperature = NAN, roomHumidity = NAN;

int8_t snakeX[110], snakeY[110];
uint8_t snakeLength = 0, snakeDirection = 1, snakeNextDirection = 1;
int8_t snakeFoodX = 8, snakeFoodY = 5;

int8_t pongPaddle = 3, pongAi = 3, pongBallX = 5, pongBallY = 5, pongDX = 1, pongDY = 1;
uint8_t pongPlayerScore = 0, pongAiScore = 0;

int16_t flappyY4 = 16;
int8_t flappyVelocity4 = 0, flappyPipeX = 10;
uint8_t flappyGapTop = 3;
bool flappyPipeScored = false;

bool tetrisBoard[MATRIX_H][MATRIX_W];
uint8_t tetrisColor[MATRIX_H][MATRIX_W];
int8_t tetX = 3, tetY = -1;
uint8_t tetType = 0, tetRotation = 0;

bool weatherValid = false;
bool weatherRefreshPending = false;
float currentTemperature = 0;
float todayMin = 0, todayMax = 0, tomorrowMin = 0, tomorrowMax = 0;
int currentWeatherCode = -1, todayWeatherCode = -1, tomorrowWeatherCode = -1;
int sunriseMinutes = -1, sunsetMinutes = -1;
String weatherText = "WETTER WIRD GELADEN";
String weatherRuntimeLocation = "BERLIN";

String scrollText;
int scrollX = MATRIX_W;
unsigned long lastScrollStepMs = 0;

bool hardwareButtonEnabled=false;
uint8_t hardwareButtonPin=19;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long buttonChangedMs = 0;
unsigned long buttonPressStartedMs = 0;
bool buttonLongActionDone = false;
bool buttonPowerDone = false;
uint8_t buttonClicks = 0;
unsigned long lastClickReleaseMs = 0;

volatile bool weatherTaskRunning = false;
volatile bool weatherTaskResultReady = false;
volatile bool weatherTaskResultOk = false;
bool weatherShowAfterFetch = false;
uint8_t weatherFailureCount = 0;
struct WeatherTaskData {
  float lat,lon,currentTemp,todayMin,todayMax,tomorrowMin,tomorrowMax;
  int currentCode,todayCode,tomorrowCode;
  int sunriseMin,sunsetMin;
  char location[32];
};
WeatherTaskData weatherTaskRequest,weatherTaskResult;
portMUX_TYPE weatherTaskMux=portMUX_INITIALIZER_UNLOCKED;

struct QueuedMessage { char text[161]; uint8_t priority; };
QueuedMessage messageQueue[6];
uint8_t messageQueueCount = 0;

bool countdownActive = false;
unsigned long countdownEndMs = 0;
uint32_t countdownDurationMs = 0;

Preferences preferences;
struct AutomationConfig {
  uint16_t version;
  uint8_t alarmEnabled,alarmHour,alarmMinute,alarmWeekdays,alarmLeadMinutes;
  uint8_t holidaysEnabled,personalDatesEnabled;
  char personalDates[241];
};
struct UserPreset {
  uint8_t valid;
  char name[17];
  uint8_t foregroundEffect,backgroundEffect,minuteEffect;
  uint8_t foregroundSpeed,backgroundSpeed,minuteSpeed;
  uint8_t fgR,fgG,fgB,bgR,bgG,bgB,minuteR,minuteG,minuteB;
  uint8_t backgroundLevel,minuteLevel,brightnessMin,brightnessMax;
};
AutomationConfig automationCfg;
UserPreset userPresets[8];
const uint16_t AUTOMATION_VERSION=0x0101;
int32_t lastAlarmMinuteKey=-1,lastHolidayDayKey=-1;
String holidayText;

uint8_t fxLife[2][MATRIX_H][MATRIX_W];
uint8_t fxLifePage=0;unsigned long lastFxStateMs=0;
uint8_t miniGrid[MATRIX_H][MATRIX_W];
int8_t playerX=5,playerY=8,objectX=5,objectY=4,velocityX=1,velocityY=-1;
uint8_t gameLevel=1,gameLives=3,gameTurn=1;
uint8_t connectColumn=3,simonLength=1,simonInput=0;
uint8_t simonSequence[24];
bool gameRunning=false;
bool connect2P=false;
uint8_t connectEnd=0;

const unsigned long TRANSITION_MS = 900UL;
bool transitionRunning = false;
bool transFrom[NUM_LEDS];
bool transTo[NUM_LEDS];
bool lastClockMask[NUM_LEDS];
bool lastClockValid = false;
unsigned long transStartMs = 0;
uint8_t transTypeNow = 1;
uint8_t rotFg = 255, rotBg = 255;
unsigned long lastRotationMs = 0;
CRGB tintNow = CRGB(255,255,255);
unsigned long lastCircadianMs = 0;
Preferences runtimePrefs;
unsigned long lastRuntimePersistMs=0;
uint8_t rotationHistory[6]={255,255,255,255,255,255};
uint8_t rotationHistoryPos=0;
uint32_t skippedStaticFrames=0;
uint32_t renderedLedFrames=0;

void mqttPublishState();
void startGame(const String& name);
String jsonString(const String& body,const String& key,const String& fallback);
int jsonInt(const String& body,const String& key,int fallback);
void jsonColor(const String& body,const String& key,uint8_t& r,uint8_t& g,uint8_t& b);
String displayText(String text);
CRGB scaleColor(CRGB c,uint8_t amount);
const char* resetReasonName();
void enqueueMessage(const String& text,uint8_t priority);

bool updateTimeCache(unsigned long now){
  if(lastTmUpdateMs&&now-lastTmUpdateMs<1000UL)return false;
  lastTmUpdateMs=now?now:1;time_t e=time(nullptr);nowTmValid=e>=100000;if(nowTmValid)localtime_r(&e,&nowTm);return true;
}

const uint8_t font5x7[][5] PROGMEM = {
  {0,0,0,0,0}, {0,0,0x5F,0,0}, {0,7,0,7,0}, {0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,8,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0,5,3,0,0},
  {0,0x1C,0x22,0x41,0}, {0,0x41,0x22,0x1C,0}, {0x14,8,0x3E,8,0x14}, {8,8,0x3E,8,8},
  {0,0x50,0x30,0,0}, {8,8,8,8,8}, {0,0x60,0x60,0,0}, {0x20,0x10,8,4,2},
  {0x3E,0x51,0x49,0x45,0x3E}, {0,0x42,0x7F,0x40,0}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {1,0x71,9,5,3},
  {0x36,0x49,0x49,0x49,0x36}, {6,0x49,0x49,0x29,0x1E}, {0,0x36,0x36,0,0}, {0,0x56,0x36,0,0},
  {8,0x14,0x22,0x41,0}, {0x14,0x14,0x14,0x14,0x14}, {0,0x41,0x22,0x14,8}, {2,1,0x51,9,6},
  {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,9,9,9,1}, {0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,8,8,8,0x7F}, {0,0x41,0x7F,0x41,0}, {0x20,0x40,0x41,0x3F,1}, {0x7F,8,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40}, {0x7F,2,0x0C,2,0x7F}, {0x7F,4,8,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,9,9,9,6}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,9,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
  {1,1,0x7F,1,1}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,8,0x14,0x63}, {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43}
};

const uint8_t PAGE_GZ[] PROGMEM = {
  0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0x03,0xed,0x7d,0xdb,0x6e,0x1b,0xc9,0x92,0xe0,0xbb,0xbf,0x22,0x5b,0x9e,0x71,
  0x91,0x2d,0x92,0x22,0x29,0xc9,0x76,0x53,0xa2,0x04,0xc9,0x96,0x2f,0xa7,0x7d,0xd1,0x8a,0x72,0x7b,0xd1,0x86,0xb1,0x93,0x64,
  0x25,0xc9,0x6a,0x15,0xab,0xe8,0xac,0x2a,0x51,0xb2,0x4c,0xa0,0x1f,0x76,0xbf,0x60,0x80,0x9e,0x7d,0x38,0xd8,0xf3,0xd2,0xd8,
  0x0f,0x38,0x03,0xf4,0xbe,0xf8,0x69,0xf5,0x27,0xfe,0x92,0x8d,0x88,0xac,0x4b,0xd6,0x85,0x45,0xd2,0xee,0x33,0xc0,0x2e,0xd6,
  0x80,0x45,0x56,0x56,0x5e,0x22,0x23,0x23,0x22,0xe3,0x96,0xc9,0x3b,0xfb,0xdf,0x99,0xee,0xc0,0xbf,0x9e,0x0a,0x36,0xf6,0x27,
  0xf6,0xc1,0x3e,0xfe,0x65,0x36,0x77,0x46,0xdd,0x0d,0x53,0x6c,0xc0,0xb3,0xe0,0xe6,0xc1,0xfe,0x44,0xf8,0x9c,0x0d,0xc6,0x5c,
  0x7a,0xc2,0xef,0x6e,0x04,0xfe,0xb0,0xfe,0x70,0x23,0x2c,0x75,0xf8,0x44,0x74,0x37,0x2e,0x2d,0x31,0x9b,0xba,0xd2,0xdf,0x60,
  0x03,0xd7,0xf1,0x85,0x03,0xb5,0x66,0x96,0xe9,0x8f,0xbb,0xa6,0xb8,0xb4,0x06,0xa2,0x4e,0x0f,0x35,0xcb,0xb1,0x7c,0x8b,0xdb,
  0x75,0x6f,0xc0,0x6d,0xd1,0x6d,0x6d,0x1c,0xdc,0xd9,0xf7,0x2d,0xdf,0x16,0x07,0x6f,0xa1,0x69,0x30,0x96,0xec,0xd2,0x75,0x98,
  0x35,0x9e,0xf4,0x84,0xdd,0xf7,0xfc,0xfd,0x2d,0xf5,0x72,0xdf,0xf3,0xaf,0xe1,0xe3,0x4e,0x47,0xba,0xae,0x7f,0x33,0x70,0x6d,
  0x57,0x42,0x0f,0x63,0x31,0x11,0x1d,0x93,0xcb,0x8b,0xbd,0x7a,0xbd,0x3f,0xea,0xdc,0x6d,0xfe,0xd0,0x34,0x5b,0xdb,0xf0,0x30,
  0xe0,0xd2,0xec,0xdc,0x6d,0xb5,0x5b,0x3f,0xb4,0x77,0xe0,0xd1,0xb6,0x1c,0xd1,0xb9,0xdb,0xbe,0xbf,0xdd,0xda,0xc1,0xb7,0x7c,
  0x30,0x00,0xe8,0x3a,0x77,0x77,0x7e,0x10,0xf7,0x1f,0xf6,0xa1,0x60,0x12,0xf8,0x02,0xea,0xff,0xb0,0xcd,0x77,0xfa,0x0f,0xe7,
  0xdf,0xdf,0xf4,0xdd,0xab,0xba,0x67,0x7d,0xb4,0x9c,0x51,0xa7,0xef,0x4a,0x53,0xc8,0x3a,0x94,0xcc,0xef,0xf4,0x5d,0xf3,0xfa,
  0x66,0xc2,0xe5,0xc8,0x72,0x3a,0xcd,0xbd,0x21,0x4c,0xb2,0xd3,0xda,0x9d,0x5e,0x31,0xef,0xda,0xf3,0xc5,0xa4,0x1e,0x58,0x7b,
  0x7d,0x3e,0xb8,0x18,0x49,0x37,0x70,0xcc,0x8e,0xe4,0x26,0x4e,0x73,0x84,0x9f,0x30,0x5a,0x65,0x60,0xc9,0x81,0x2d,0x18,0xf7,
  0x99,0xef,0x4e,0x6b,0x77,0x5b,0x0f,0xda,0xed,0xed,0xed,0xda,0x25,0x97,0x15,0x84,0xbd,0xca,0x76,0x77,0xff,0xb9,0xba,0x47,
  0x13,0xeb,0xdc,0x15,0x62,0xb8,0x3b,0x1c,0xce,0x1b,0x33,0xc9,0xa7,0x30,0xe0,0x95,0x42,0x5d,0xe7,0x61,0xbb,0x39,0xbd,0xda,
  0x0b,0x01,0xe0,0x81,0xef,0xee,0x4d,0xb9,0x69,0x22,0x94,0xed,0xf6,0x14,0xe0,0x1b,0xb7,0x6e,0x10,0x28,0x04,0x5d,0x74,0xda,
  0xf7,0x93,0xba,0x4d,0xd6,0x64,0x00,0xe8,0xbc,0xe1,0x05,0x7d,0x85,0xbc,0x8e,0x1a,0x98,0x26,0x5e,0x0d,0xab,0xc1,0x24,0x7d,
  0xdf,0x9d,0x74,0x70,0x94,0x79,0x63,0x24,0x2d,0xf3,0xc6,0xb4,0xbc,0xa9,0xcd,0xaf,0x3b,0xf8,0xb0,0x87,0x7f,0xea,0x30,0x51,
  0x28,0xf1,0x45,0x1d,0xba,0x09,0x26,0x8e,0xd7,0x91,0x62,0x2a,0xb8,0x5f,0x41,0x70,0xea,0x43,0xcb,0xaf,0x4d,0x2c,0x07,0x20,
  0xae,0xb4,0xef,0x43,0x2f,0xb5,0xd6,0x50,0x56,0xab,0x7b,0x23,0x3e,0xed,0xb4,0x76,0x10,0xc2,0x06,0x2e,0xcc,0x8d,0x8e,0xa5,
  0x51,0x9f,0x57,0x5a,0x0f,0x6b,0xed,0xdd,0xda,0xf6,0xfd,0x5a,0xe3,0x87,0x9d,0xea,0x9e,0xc2,0x78,0xa7,0x85,0x88,0x75,0x6d,
  0xcb,0x64,0x0a,0x54,0x5c,0xc3,0xe8,0x65,0x1d,0x91,0x1a,0x78,0x9d,0xd6,0x03,0x98,0x63,0x84,0x03,0x7a,0xa0,0x95,0x1b,0x73,
  0xd3,0x9d,0xc1,0xa4,0x5b,0x80,0x02,0xb6,0x03,0x70,0xb0,0xbb,0xcd,0x66,0x73,0x77,0x4e,0xa3,0xb3,0x71,0x5b,0xc3,0x52,0x2b,
  0x83,0x25,0x5c,0xcf,0xf9,0x1d,0x9b,0xf7,0x85,0x1d,0x4f,0xbe,0x6f,0xbb,0x83,0x8b,0xbd,0x85,0x68,0xeb,0xb4,0x00,0xf9,0x11,
  0x82,0xa5,0x3b,0x8b,0xdb,0x0d,0x6d,0x71,0xb5,0xc7,0x6d,0x6b,0xe4,0xd4,0x2d,0x40,0x9b,0xd7,0x41,0xaa,0x13,0x52,0xa1,0x83,
  0x70,0xec,0xcd,0x2c,0x7f,0x30,0x4e,0x37,0xf8,0x25,0xf0,0x7c,0x6b,0x78,0x5d,0x0f,0x79,0xa8,0xe3,0x4d,0x39,0xf0,0x4e,0x5f,
  0xf8,0x33,0x21,0x9c,0xa2,0xee,0xd2,0x34,0x73,0xc7,0x13,0xb6,0x18,0xf8,0xc0,0x66,0xd3,0xc0,0x7f,0x87,0x4c,0xdd,0x95,0xc0,
  0xc9,0xe2,0xbd,0x5e,0x42,0x4d,0xde,0xdf,0x28,0xaa,0x6a,0x35,0x9b,0xff,0x3c,0x0f,0x5b,0xf5,0x03,0xa0,0x00,0x47,0x5f,0xa0,
  0xbb,0xcd,0x01,0xb0,0x51,0x3f,0xbf,0x2a,0x77,0xb7,0x77,0x76,0xda,0xbb,0x0f,0xa3,0xe1,0x87,0xc3,0x61,0x76,0x71,0x9a,0xfa,
  0xe2,0xe0,0x7c,0xef,0xe4,0x41,0x18,0x0b,0x6b,0x34,0xf6,0x3b,0x3b,0x6d,0x5a,0x3b,0x1a,0xa2,0xa9,0x73,0x91,0xe3,0x3a,0x62,
  0xde,0xb8,0xe4,0x76,0x20,0x6e,0x80,0xb2,0x42,0x46,0xd8,0x7e,0x08,0xd5,0x7d,0x71,0xe5,0xd7,0x09,0x1f,0x1d,0x89,0x9d,0xa4,
  0x56,0x48,0xb1,0x78,0x75,0x1e,0x4e,0x28,0x99,0xe9,0xde,0x20,0x90,0x1e,0x54,0x9b,0xba,0x16,0x61,0x2f,0x24,0x7d,0xe0,0x49,
  0x05,0xa3,0x6a,0xd0,0x98,0x4a,0x0b,0xde,0x5c,0xeb,0x98,0x48,0x75,0x1c,0x4d,0xbb,0xb9,0xd3,0xda,0x6d,0xf6,0x13,0xd0,0x89,
  0xb2,0x66,0x6a,0x52,0x0f,0x9a,0xcd,0x39,0x02,0xc9,0xa5,0xe0,0x3a,0xf6,0x7d,0x6b,0x22,0x74,0xe4,0xef,0xfd,0x83,0xd0,0xdd,
  0x00,0x8a,0xb9,0x30,0xf9,0xb5,0xb7,0x06,0x1b,0x3f,0x20,0x96,0x25,0x12,0xdd,0xd1,0xbb,0x60,0x8a,0x25,0x34,0x9c,0x87,0xd4,
  0xa7,0xb1,0x52,0x5b,0x71,0x38,0x02,0x38,0x70,0xa7,0x91,0xa4,0x24,0xd4,0xee,0x68,0xb0,0x21,0x9b,0xe6,0x5a,0x79,0x3e,0xf7,
  0x03,0x2f,0xcd,0x09,0x08,0xc4,0x0f,0x58,0x19,0x1e,0xea,0x28,0x0a,0x3b,0xf8,0x27,0xb5,0x62,0xc4,0x73,0x53,0xcb,0xb6,0x57,
  0x22,0x59,0x5d,0x90,0x68,0xd0,0x30,0x42,0x5d,0x1a,0x9b,0x3f,0xe0,0xc0,0x79,0x96,0x07,0xf6,0x9a,0x70,0x18,0xad,0xe0,0x8d,
  0x4e,0xdb,0x63,0x31,0xb8,0x00,0x49,0x14,0x2d,0x32,0x89,0xed,0x90,0xd2,0xe9,0xbb,0x22,0xa2,0x7a,0x11,0xc1,0xea,0x74,0x02,
  0xc8,0x4e,0xb1,0xed,0x94,0x7b,0xde,0x0c,0xc0,0x4c,0x15,0x3a,0xc1,0xa4,0x2f,0xe4,0x7f,0x04,0x3d,0xdd,0x69,0x8c,0x60,0x9f,
  0x4f,0x16,0x49,0xf1,0x26,0x95,0x35,0xdc,0xa9,0x70,0xd2,0x02,0x53,0xbd,0x99,0x5a,0x83,0x8b,0x5a,0x03,0x65,0x99,0x74,0xed,
  0x2c,0x21,0xc2,0x8a,0x3e,0xa4,0xad,0x26,0xac,0x78,0x53,0x4a,0x9a,0xdb,0x44,0x9a,0x49,0x6d,0x16,0xf2,0x76,0x9a,0x96,0x62,
  0x90,0x7f,0x40,0xd9,0x8f,0xbd,0xc7,0x83,0xaf,0xd0,0xfb,0x5e,0xb2,0xd9,0xb6,0x1f,0x6a,0x9b,0x2d,0x89,0x78,0xdc,0xe2,0x92,
  0xee,0xf2,0xc3,0xe3,0x26,0x1c,0xef,0x25,0x7b,0x28,0xad,0x22,0xe9,0x46,0xb3,0x84,0x6e,0x85,0x82,0x41,0x0d,0xdd,0x69,0x6d,
  0xed,0xcc,0x1b,0x26,0x0a,0x67,0x79,0x13,0xe2,0x3e,0x5c,0x8d,0x87,0xe6,0xb6,0xb9,0xf3,0x43,0xb2,0x36,0xfd,0x9d,0xbe,0x00,
  0xf4,0x4f,0xa5,0x40,0x1d,0x4b,0x5f,0x6a,0xee,0x4d,0x41,0x6e,0xc3,0x9a,0xf9,0x96,0xdb,0x69,0xb5,0xb6,0x5a,0xcd,0xf4,0xea,
  0x3f,0x68,0xf2,0x66,0x6e,0x65,0x77,0x12,0x61,0x5b,0xc8,0x1c,0xf3,0x06,0x00,0x6f,0x65,0x10,0x3b,0x6f,0x0c,0x01,0x4f,0x81,
  0x14,0x05,0x8a,0x01,0x6e,0x68,0x2d,0xda,0xd0,0x06,0xae,0x14,0x6b,0x88,0x9b,0xed,0x44,0xdc,0xec,0xa6,0x45,0x7a,0x28,0x5e,
  0xb2,0xe2,0x59,0x8d,0xc0,0x60,0x53,0x74,0x6e,0x74,0x81,0xb2,0x90,0xe4,0xa3,0x49,0xe3,0x12,0xdc,0xd9,0xdf,0x52,0x6a,0xe4,
  0xfe,0x96,0x52,0x69,0x51,0xa1,0x3b,0xd8,0x37,0xad,0x4b,0x36,0xb0,0x81,0xb7,0x40,0x5f,0x05,0x01,0x83,0x1a,0xe9,0xb8,0xb5,
  0x48,0x1d,0x85,0x37,0x7a,0x03,0x50,0xa8,0x36,0x0e,0x9e,0x58,0x72,0x32,0x03,0x29,0xcf,0x76,0x1a,0xcd,0x46,0x9b,0xfd,0xef,
  0xff,0xc5,0x4e,0x86,0x43,0x71,0xe1,0x8b,0x1a,0x3b,0x05,0x25,0xd7,0x87,0x7d,0xbc,0xc6,0xde,0xe2,0xa7,0xac,0xb1,0x1e,0x4c,
  0xc9,0x67,0xcf,0xdc,0x89,0x60,0x00,0x2d,0xeb,0x4d,0x2d,0xd8,0x7a,0xf7,0xb7,0xa0,0xcf,0x54,0xc7,0x88,0x36,0x84,0xc4,0x83,
  0xe5,0xb5,0x00,0x84,0xb0,0x18,0x35,0x18,0xd4,0xc8,0xdb,0x07,0x2f,0xac,0x4b,0x51,0x7f,0xc9,0x7d,0x69,0x5d,0x01,0x54,0xed,
  0x83,0xfd,0x01,0x77,0x2e,0xb9,0xc7,0x2c,0xb3,0xbb,0x11,0x12,0xca,0x46,0xd4,0x2a,0x7e,0x56,0xfa,0xf8,0xc6,0xf6,0x76,0x73,
  0x83,0x29,0xe2,0x84,0x87,0x66,0x13,0x7a,0xdc,0x52,0xcd,0x0f,0xf6,0x49,0xd0,0xc7,0xd3,0x23,0x05,0x05,0x5e,0x23,0xc2,0xd5,
  0x90,0x3f,0xb9,0x12,0x14,0x6f,0x1e,0x30,0x7e,0xe1,0x5b,0x97,0x80,0x51,0x7c,0xb3,0x4f,0x02,0x49,0x1f,0xfb,0xc4,0xe1,0x7d,
  0x5b,0x98,0x1b,0x8c,0x84,0xd4,0x46,0x24,0x12,0x71,0x24,0x1a,0x01,0x3e,0xc3,0xa9,0x95,0x4d,0xf2,0xc8,0xf9,0x08,0x50,0x0a,
  0x35,0xc1,0x12,0xc8,0xde,0xc0,0x42,0x09,0xa0,0x05,0x81,0xa0,0xd9,0xbe,0xf0,0x0b,0xe0,0x72,0x67,0x42,0x2e,0x06,0xe7,0x4e,
  0x59,0xf7,0xe7,0x40,0xbe,0x42,0x22,0xf5,0xb3,0x8f,0xd7,0x17,0xb6,0x05,0xa3,0x30,0xae,0x40,0x73,0xf2,0x23,0x79,0x63,0x77,
  0x96,0xb4,0x10,0x4b,0xc7,0x3c,0x78,0x26,0x6c,0xa0,0xf9,0x0b,0x61,0xf9,0xac,0x2f,0x2c,0x66,0x06,0xce,0x85,0x2d,0x26,0xec,
  0x8c,0x07,0x13,0x46,0x00,0x50,0xb7,0x7d,0xd2,0x72,0x1c,0xe1,0x79,0x2f,0x2d,0xe7,0xa7,0x78,0x6d,0x49,0x37,0xc2,0x3e,0x15,
  0x18,0x11,0x76,0x13,0x70,0x52,0xed,0x22,0x60,0x48,0x27,0xdc,0x60,0xc0,0xe7,0xdd,0x8d,0x16,0x7c,0xf2,0xab,0xee,0x46,0x7b,
  0x77,0x77,0x63,0x11,0x4c,0x63,0x78,0x2c,0x05,0x89,0x5f,0x7d,0x1d,0x48,0xfc,0x6a,0x29,0x48,0xfb,0xb4,0xe7,0x1e,0x3c,0x16,
  0x92,0xbd,0x78,0x7c,0xc6,0x40,0x02,0x09,0xdb,0x27,0x69,0x3c,0x01,0xb1,0x87,0x6b,0xf1,0x71,0x86,0x1f,0xc2,0x61,0x60,0x6a,
  0x79,0xf0,0x01,0x10,0x9b,0xf0,0xf1,0x56,0x48,0x60,0xbd,0x06,0xc0,0x41,0x1d,0x44,0x53,0x83,0x4e,0xea,0xf8,0x8a,0x7d,0xf9,
  0xf5,0x7f,0xd0,0x84,0x10,0xe1,0xc2,0xfe,0xf2,0xeb,0x5f,0xb5,0xa9,0xd9,0xa6,0x7c,0x0c,0x86,0xe5,0x19,0x9f,0xad,0x31,0xaf,
  0xa4,0x51,0xd1,0xa4,0x9a,0xe1,0xa4,0x5a,0xcd,0xf6,0x36,0xf4,0xa2,0xf6,0x0f,0x6a,0x37,0xe0,0x53,0xa4,0x94,0x17,0xaa,0xf9,
  0xc6,0xc1,0xd1,0x85,0x1f,0x20,0xc2,0x1d,0xf6,0x12,0x70,0x34,0x43,0x58,0x39,0xec,0x38,0x0a,0x4e,0x76,0xfb,0x19,0x36,0x7c,
  0x47,0x8c,0x27,0x48,0x7c,0xaa,0x97,0xc5,0x73,0xc3,0x85,0xcb,0xcd,0xec,0x98,0xf0,0xbf,0xf6,0xdc,0xe2,0x66,0x5f,0x3b,0x3b,
  0xd5,0xc1,0xc2,0xf9,0x21,0xac,0xa5,0xb3,0x3b,0x13,0x28,0x74,0x5c,0xc7,0x23,0x46,0x9f,0x59,0x8e,0x19,0x92,0x68,0x6a,0x76,
  0xbd,0x89,0xeb,0xfa,0x63,0x90,0x06,0xeb,0xcd,0x2e,0x6e,0x56,0x46,0x90,0xad,0xfb,0x11,0x3d,0x46,0xcd,0x1e,0xc1,0x7e,0xd5,
  0xa7,0xfd,0xd7,0xe9,0x91,0x12,0xbb,0x71,0xd0,0x02,0x1a,0xe5,0x23,0x0b,0xa7,0xe5,0xb9,0x43,0xd8,0x47,0x6a,0x60,0x8a,0x02,
  0x51,0x7a,0xae,0x03,0x3b,0x92,0xc7,0x64,0x30,0xb6,0x46,0x0d,0xf6,0xd8,0x12,0x34,0xff,0x29,0x2c,0xab,0x2f,0x98,0x79,0xfb,
  0x59,0x0e,0x01,0x25,0x3c,0x00,0x8a,0x0e,0x26,0x23,0x71,0x21,0xc6,0xd0,0x83,0x6d,0x01,0xbd,0x27,0x44,0xbc,0x9a,0xd8,0x44,
  0xd1,0x2c,0xd8,0xdb,0xdb,0x3f,0x80,0x01,0x24,0xdb,0x62,0x3f,0xd1,0x5e,0x38,0x92,0xb0,0xe3,0x68,0xb2,0xf4,0x40,0x6d,0x53,
  0x31,0x3a,0x94,0x11,0x48,0x13,0x03,0xa8,0x85,0xda,0x4f,0xb1,0xce,0xc0,0x27,0xdc,0xd1,0xeb,0xd4,0xd2,0x0e,0xf9,0xa5,0x2b,
  0xc1,0x1a,0x7d,0x12,0x57,0x8f,0x51,0x1e,0xdb,0x01,0x1b,0x07,0x5f,0xfe,0xfb,0x7f,0x63,0x4f,0x54,0x4d,0x98,0x58,0x28,0xa2,
  0x93,0xd5,0x0d,0x81,0x09,0xb7,0xca,0x72,0x68,0xc2,0x4a,0x3a,0x38,0xaa,0xfa,0x13,0x2e,0xfb,0xa2,0x60,0x61,0x93,0xa6,0x8f,
  0x10,0xa0,0x58,0x18,0xd3,0x43,0x4c,0x58,0x4f,0x17,0xd2,0x53,0xd2,0xbe,0x37,0x15,0xc2,0x5c,0x83,0xa4,0x32,0x2d,0x4b,0xa9,
  0x0a,0xb7,0xe0,0x08,0x96,0xe7,0x68,0xf7,0x7b,0x96,0x7f,0xfb,0x7b,0x31,0x1c,0xd1,0xfb,0xeb,0xaf,0x82,0x25,0x6e,0x5d,0xc6,
  0xc3,0xa9,0x9d,0xa0,0x07,0x98,0x11,0xd6,0xb0,0x10,0x96,0x73,0xc9,0x2d,0xfb,0xab,0xe0,0xa0,0x96,0x2b,0xc3,0xf0,0x54,0xde,
  0xfe,0x71,0xfb,0x37,0x51,0xbc,0x2e,0xa0,0xa1,0x7e,0xdd,0xb2,0x40,0xc3,0xa5,0xab,0xb2,0x12,0xbf,0xbd,0x71,0x60,0x10,0x01,
  0x7c,0x0b,0x9a,0x87,0x60,0xc7,0xf0,0x05,0xec,0xd9,0x3e,0xf0,0xf2,0x16,0x7b,0x46,0x6e,0x86,0x35,0x38,0x2f,0xd1,0x64,0x57,
  0xe2,0xbc,0xe3,0xb8,0xfa,0x3f,0x84,0xf3,0x12,0x68,0xd6,0xe6,0xbc,0xa4,0x69,0x11,0xe7,0xe9,0x73,0x49,0x6a,0xf6,0xc0,0xb6,
  0x2b,0x9a,0xc7,0x53,0x5b,0x58,0x20,0x15,0x53,0x62,0x2c,0xb3,0x37,0x2c,0x66,0x61,0xad,0xfb,0x35,0x59,0x38,0xd3,0xf2,0x1b,
  0x59,0x38,0xe9,0xed,0x6b,0x58,0xb8,0xa0,0xf5,0x37,0xb0,0x70,0xd2,0xdb,0xba,0x2c,0x9c,0x69,0xf9,0x0d,0x2c,0xac,0x61,0x77,
  0x3d,0x16,0x4e,0x37,0x5c,0x79,0x55,0x34,0x56,0xf4,0x60,0x69,0xe4,0x45,0x31,0x30,0x2f,0xc4,0xa5,0xf8,0x3a,0x84,0x50,0xcb,
  0x52,0xdd,0xe8,0xfe,0xaa,0x12,0x05,0x54,0xf5,0x00,0x56,0xb9,0xfe,0xe2,0xe4,0xb1,0xb7,0x9a,0xdc,0x98,0x50,0x8b,0xbc,0xcc,
  0x58,0xca,0xe2,0xaa,0xe1,0xda,0xec,0xad,0x9a,0x2d,0x63,0x6d,0x55,0xeb,0x1f,0xc2,0xd6,0x61,0xd7,0x6b,0xb2,0xb4,0xd6,0xea,
  0x1b,0xd9,0x59,0xf5,0xf4,0x35,0xac,0x9c,0x69,0xf9,0x0d,0x6c,0xac,0x7a,0x5a,0x97,0x85,0xb5,0x56,0xdf,0xc0,0xbe,0x21,0x26,
  0xd7,0x63,0xdd,0xa4,0xd1,0xca,0xd8,0x0f,0x59,0x21,0xcf,0xb2,0xaa,0xaf,0x75,0xd9,0x55,0x6b,0xb5,0x74,0xf2,0xa1,0xe5,0x79,
  0x64,0x63,0xe0,0x8e,0x74,0x6a,0xb0,0x58,0x14,0x38,0xa1,0xd2,0x6e,0xf3,0x00,0x75,0x76,0xef,0xda,0x19,0x8c,0xa5,0xeb,0x34,
  0xd8,0x11,0x6a,0xef,0x68,0x31,0x9f,0xa1,0xda,0xde,0x77,0xe1,0x0f,0x13,0x52,0xed,0xbb,0xcc,0x03,0x8d,0x5f,0x5c,0x41,0x4f,
  0xca,0x50,0xb5,0xfb,0x82,0x11,0x7b,0xad,0xa7,0xdd,0xff,0xe8,0x4e,0xd0,0xa8,0x60,0xf7,0x60,0x30,0x65,0x03,0x5f,0x90,0x88,
  0x28,0x75,0x61,0xdc,0xfe,0x15,0xcc,0xaa,0x11,0x4c,0xd4,0xe3,0x8e,0x35,0x21,0x73,0x05,0x00,0xaa,0x84,0xb3,0x99,0x09,0x50,
  0x59,0x84,0x5d,0xcd,0x3b,0x33,0x7c,0x40,0x0e,0x10,0x29,0x54,0x5f,0xea,0xd1,0x89,0x16,0x4c,0x1b,0x4a,0x16,0x8a,0xaa,0xa4,
  0xcf,0x73,0xe8,0x0a,0x3a,0x70,0xa7,0x34,0x53,0x5a,0x3b,0xa4,0x02,0xd5,0x05,0x8c,0x06,0xf6,0x12,0x68,0x2b,0xea,0x75,0xb6,
  0x5a,0x7b,0xe3,0xe0,0xad,0x32,0xfb,0x17,0xd5,0x00,0x3b,0x94,0x56,0x61,0xd1,0xfb,0x9d,0x8d,0x83,0x9f,0x83,0x21,0xe0,0x3d,
  0xa9,0x10,0x09,0xbf,0x52,0x5c,0xfe,0x6c,0xc9,0x0b,0x8c,0xee,0x3a,0x6a,0xf9,0xfc,0xd8,0xd7,0x93,0x47,0x1f,0x46,0x7d,0xa9,
  0xea,0x72,0x7f,0x98,0xa2,0x80,0x97,0xae,0x04,0x88,0x3d,0x76,0x71,0xfb,0x79,0x6c,0xd7,0x18,0x6a,0x91,0xa6,0xc7,0x66,0x5c,
  0x4e,0x1a,0xec,0x55,0xe0,0x7f,0xf4,0x59,0xcf,0x75,0x60,0xe9,0x80,0xee,0xea,0xe4,0x40,0xac,0x07,0xb4,0xab,0x01,0xba,0xc9,
  0x55,0xf9,0x7a,0x0a,0xbb,0xc6,0x4b,0x50,0x43,0xdd,0xac,0xdf,0xa3,0x78,0x2e,0x6a,0x43,0xa9,0x9f,0xb9,0x3e,0xd1,0x44,0x7e,
  0x06,0x32,0x7c,0xb3,0xf2,0xf2,0xbf,0x55,0xb4,0x04,0x16,0x3d,0x70,0x4d,0x44,0x60,0xd5,0x02,0x4e,0x8c,0x7a,0x56,0x55,0xbc,
  0xa8,0x67,0x15,0xd0,0xc8,0x3a,0x82,0x76,0x9a,0x31,0x3b,0x86,0x03,0xf8,0x0c,0x5d,0xb4,0x0a,0x09,0xda,0xd6,0x2e,0x68,0x42,
  0xec,0x63,0x30,0xbc,0xfd,0x1d,0x5d,0x58,0x60,0x67,0x0b,0x0b,0x56,0x6a,0x24,0xbc,0x29,0xee,0x36,0xe4,0x13,0x8a,0x1c,0xb4,
  0x0c,0xa6,0x64,0xa1,0xa2,0x1e,0x38,0x97,0x42,0xde,0xfe,0x8e,0xf6,0xb9,0xbf,0x1a,0xe2,0x5e,0x05,0x32,0x52,0xad,0xa1,0x3d,
  0xcc,0x05,0xec,0x7d,0x51,0x82,0xbf,0xa8,0xae,0xf7,0xda,0xb1,0xaf,0x57,0xc0,0xa2,0x25,0x00,0x96,0xb1,0x6b,0x07,0xc0,0x47,
  0x40,0xe5,0xb0,0xf4,0xac,0x12,0x42,0x5d,0x86,0xcc,0x57,0xee,0x19,0xb9,0xd6,0x0b,0xb1,0x19,0x09,0xb7,0xfb,0x1b,0x4b,0xfc,
  0x9d,0x1c,0xb0,0xf5,0x11,0xb6,0xdb,0xa9,0x74,0x87,0x96,0x9d,0x9f,0x94,0xc9,0xaf,0x4f,0xe9,0x95,0x58,0x4e,0xd7,0x9a,0x53,
  0x5b,0xba,0xb3,0x0d,0x2a,0x88,0x94,0x8b,0xb3,0x60,0x0c,0x92,0xb5,0x5f,0x30,0x9f,0x0f,0x81,0x25,0xfc,0x9e,0x0f,0x12,0xe4,
  0x99,0x1b,0xc8,0xd2,0xd9,0x90,0xbf,0x29,0x76,0xa0,0xa7,0xba,0xee,0x5b,0xde,0xa2,0xbe,0x4f,0x1c,0x73,0x9d,0x9e,0xe9,0x6f,
  0x29,0xd2,0x8e,0x26,0x7d,0x4c,0xf4,0xa8,0xbf,0x74,0xcd,0xc0,0xcb,0xa3,0x8c,0xab,0xd7,0xf0,0xb6,0xc4,0x23,0x1c,0x2d,0x3e,
  0x10,0x76,0xb4,0xdf,0x85,0xdd,0x6a,0xfb,0x5e,0xd8,0x13,0x54,0x5a,0x5b,0x57,0xcd,0x36,0x5d,0xaa,0xac,0x2e,0x21,0x13,0x0f,
  0xb8,0x0e,0xa4,0xcd,0x58,0x0e,0xf9,0x60,0x3c,0xc4,0x2d,0x91,0xf6,0x95,0xfc,0xec,0x95,0x56,0xf7,0x32,0xb0,0x7d,0x6b,0x55,
  0x49,0xd8,0xba,0x62,0xa6,0xbc,0xfd,0x3c,0xb8,0x10,0x4e,0x27,0x8c,0x9e,0x60,0x68,0xa5,0x7d,0xd5,0x61,0xce,0xed,0xef,0x68,
  0x62,0x43,0x81,0xe2,0x08,0x2c,0x6f,0x31,0x8f,0xa9,0x6d,0xb6,0xc3,0x4e,0x2c,0x67,0xeb,0x28,0xf0,0x58,0xc5,0x9a,0xa8,0x00,
  0x4b,0x07,0xb6,0x64,0xda,0x4c,0xaa,0xab,0x31,0x37,0x6c,0x1b,0x30,0x11,0x50,0x89,0xd9,0xcf,0xc0,0x05,0xb0,0x5b,0x71,0x0c,
  0x42,0xe4,0x58,0x3b,0xac,0xb5,0x82,0x68,0xd4,0x76,0xbf,0xa8,0x55,0x0f,0x43,0x51,0xb9,0xcd,0x0f,0x70,0x0e,0x54,0xef,0x98,
  0x98,0xa9,0x52,0xb9,0x44,0x1f,0x22,0xc0,0xe8,0x00,0x76,0xd9,0x16,0x8b,0x1e,0x41,0x90,0x54,0x17,0xed,0x69,0xb0,0x79,0xbe,
  0xf6,0xfc,0xa4,0xa9,0x29,0x41,0x17,0xd9,0xa2,0x0f,0xbd,0xa8,0x5a,0xb2,0xab,0xf6,0x6e,0x3f,0xc3,0xd8,0x1f,0xc5,0xd8,0xc1,
  0x91,0x10,0xa9,0x7d,0xe8,0x81,0x9e,0x09,0x10,0x2c,0xa8,0xe6,0xb7,0xcc,0x15,0x03,0x3a,0xa1,0xf2,0xa1,0x1b,0x35,0x2f,0x60,
  0x33,0x03,0x11,0x27,0xad,0xa1,0x5f,0x5f,0xac,0xef,0x7b,0x03,0xe9,0xda,0xf6,0xba,0xfa,0xbe,0xd6,0xaa,0x88,0xd8,0xdb,0xba,
  0xbe,0xc7,0x80,0xa4,0xa6,0xdd,0x8d,0x44,0xf1,0x3b,0xc6,0x68,0x0c,0x86,0x1d,0x12,0x00,0x81,0x90,0x4c,0x01,0xdb,0x95,0x8f,
  0xf1,0x26,0x76,0x61,0xe3,0xde,0x22,0xa1,0x0a,0x3a,0xdd,0x3b,0x0c,0x2a,0x39,0xe8,0xd8,0x96,0xab,0x91,0x19,0x62,0x83,0x16,
  0x25,0xa4,0x6f,0xef,0x7a,0xd2,0x77,0xed,0x02,0x4a,0x9b,0x81,0x44,0x87,0x9d,0x2b,0xc6,0x9e,0xb7,0x9c,0x89,0x34,0x23,0x2c,
  0x6c,0xad,0x85,0x03,0x29,0xb9,0x05,0xf4,0x27,0x1a,0x15,0x16,0x19,0xde,0x22,0x89,0x6b,0x41,0xad,0xc8,0x43,0xb4,0xca,0xa2,
  0x86,0xcc,0x79,0x8f,0x11,0xe5,0xba,0xa8,0xf4,0x25,0x8b,0xab,0x5e,0x7e,0x20,0x7f,0x7f,0x91,0x32,0x18,0x02,0xd7,0x03,0x59,
  0x3c,0x28,0x64,0x87,0xc7,0x96,0x44,0x16,0xff,0xf2,0xeb,0xbf,0x6a,0xca,0x0d,0xab,0xb8,0x80,0x69,0xf6,0xec,0xa8,0x8c,0x11,
  0x28,0xc8,0x7a,0xe4,0x79,0x96,0x07,0x70,0xf9,0x2a,0xb2,0xc0,0x5e,0xfe,0xa7,0xf3,0xf3,0x85,0xfa,0xde,0xc1,0x6b,0x10,0xbc,
  0x98,0xcd,0xc8,0x86,0xb7,0x9f,0x25,0x6a,0xe9,0xfa,0xda,0x17,0x90,0x57,0x08,0xfd,0x0b,0x77,0x40,0xeb,0x12,0xad,0x07,0x06,
  0xb2,0x89,0xb0,0x40,0x85,0x1d,0x51,0xc4,0x15,0x14,0x99,0xa9,0xcd,0x07,0x02,0x76,0x74,0xa0,0xa8,0xee,0xc6,0xb1,0x90,0xb6,
  0xe5,0xa0,0x80,0xd5,0x16,0xca,0x0e,0xbb,0xe9,0x09,0x2e,0x91,0x4a,0x5e,0x63,0x04,0x21,0x50,0x2a,0x6e,0x6c,0x17,0x97,0xed,
  0xa7,0xc7,0xc0,0xda,0x20,0x01,0x31,0xe9,0xb0,0x04,0xd8,0x05,0xda,0x41,0xfd,0x87,0x88,0x19,0xf0,0x8b,0xe2,0x85,0x66,0xa3,
  0x09,0xff,0x5a,0x45,0x1b,0xec,0x0b,0x50,0x97,0x46,0x4b,0xc7,0x4a,0x70,0x92,0x1e,0xab,0xf5,0x30,0xde,0x67,0x1e,0x2e,0x1c,
  0x2d,0xdc,0x74,0x35,0x0c,0x8d,0xa6,0x5e,0x9e,0x8c,0x23,0xb2,0x03,0x71,0x35,0x61,0xcf,0xe0,0xfb,0x75,0x61,0x14,0x49,0x8b,
  0xdb,0x40,0x37,0x51,0xb0,0xe6,0xe9,0x69,0x8f,0xcd,0x2c,0x90,0xb6,0x0e,0x28,0x74,0x24,0xdf,0x4c,0xe2,0x69,0xf6,0x04,0x70,
  0x39,0x82,0x69,0x31,0xd8,0x44,0x8e,0x01,0xd3,0x1e,0x94,0xf1,0xfe,0x48,0x0c,0x25,0x1f,0xf9,0x0d,0xd0,0xfd,0x6c,0x0c,0xc9,
  0x41,0x61,0xf4,0x32,0x02,0xe3,0x63,0x30,0x02,0x6a,0x19,0x86,0xf4,0x66,0xbb,0x17,0xb0,0x8f,0x78,0xec,0xd9,0xf9,0xf9,0x29,
  0x03,0x15,0x54,0x62,0x38,0xe8,0xe2,0xf6,0x0f,0x54,0xe2,0x89,0xc0,0x7e,0x74,0x61,0x37,0xb6,0x1c,0x8e,0x6a,0xe4,0x84,0x3b,
  0xc8,0x28,0x2a,0x96,0x0d,0x66,0x12,0x5a,0x8f,0x33,0x21,0x4d,0x3d,0x00,0xd4,0x97,0xb1,0xad,0x00,0xa4,0x5c,0x57,0xfc,0xe5,
  0xbb,0x53,0x6b,0xd0,0xd1,0xa4,0x65,0x88,0xff,0x73,0x2c,0xdf,0x38,0xf8,0xf2,0xeb,0xff,0x0c,0x85,0x0a,0x5a,0x11,0x52,0x99,
  0x9f,0x4c,0x67,0x4d,0x90,0x6b,0xce,0xed,0x1f,0xbe,0x35,0xf2,0x89,0x43,0xb6,0x9e,0x1d,0xed,0xe9,0xec,0x66,0xdf,0xfe,0x1e,
  0x0c,0x51,0xe6,0x8d,0x94,0x25,0x8e,0xf2,0x79,0x3d,0xab,0xf5,0xdc,0x9a,0x50,0x28,0xea,0x91,0x0b,0x06,0x8b,0xe9,0xce,0x52,
  0x3b,0x40,0x4f,0x5c,0x04,0xca,0xd6,0xcb,0x11,0x12,0x66,0xcb,0xc9,0x9e,0x18,0xb8,0x60,0x07,0x95,0x9a,0x08,0x0f,0xef,0xef,
  0x34,0x81,0x92,0x22,0xdb,0x8f,0x9c,0xf7,0x1a,0xe5,0xa8,0x7e,0x50,0xa1,0xcc,0x13,0x50,0x0c,0x13,0xd0,0x21,0x97,0x29,0x0f,
  0x79,0xb6,0x87,0x47,0xdc,0x19,0x80,0xde,0xa4,0x35,0xe1,0xfd,0xbe,0x14,0x29,0x1e,0xd5,0x08,0x2d,0x1a,0x95,0x48,0xed,0x47,
  0xdc,0x32,0x14,0x1e,0xa2,0xe4,0x89,0xd5,0xf1,0xd7,0xfb,0x28,0x90,0x62,0xee,0xb1,0x53,0x09,0x6b,0xe7,0x7b,0x4b,0x73,0x22,
  0xc2,0x7a,0x6a,0xa8,0x05,0xc6,0xc9,0x54,0xd5,0x59,0xbe,0x9b,0x84,0xcb,0x84,0x30,0x14,0x09,0x71,0x3e,0x40,0xe7,0x88,0x1a,
  0xb1,0x48,0x86,0xbf,0x72,0x25,0xcc,0xb4,0x44,0x54,0x47,0x89,0x2c,0x0b,0x95,0x12,0x54,0xe7,0x4b,0xec,0xfc,0x53,0x58,0xb6,
  0xeb,0x12,0x3b,0xff,0xad,0xb0,0xc6,0xc8,0xdc,0xfe,0x62,0x6f,0xc0,0x6e,0xb4,0x1f,0x16,0xa8,0x36,0x1a,0x15,0xf0,0xe9,0xd4,
  0xbe,0x0e,0x67,0x9a,0x97,0x44,0x88,0x20,0xd8,0x45,0x67,0xa1,0xe7,0x62,0x9d,0x5d,0xf4,0x04,0x79,0x4b,0x14,0x2c,0xef,0x41,
  0x2f,0x34,0x5c,0x31,0x85,0xea,0x63,0x11,0xfa,0x03,0x10,0x3f,0xaa,0x5d,0xcf,0x76,0x0b,0x17,0xa0,0x55,0x82,0xfb,0x76,0x09,
  0xda,0xb7,0x4b,0x70,0xbe,0x53,0x82,0xef,0xdd,0x12,0x2c,0xdf,0x5f,0xf4,0x0e,0xcc,0xd2,0x07,0x8b,0xde,0x3d,0xd8,0x38,0x78,
  0x58,0xb0,0x30,0x0a,0x17,0xaf,0x60,0xc7,0x2e,0x10,0x1d,0x09,0x5a,0x5e,0x91,0x03,0x7a,0xc1,0xde,0xdc,0xba,0x9f,0xd9,0x9b,
  0x5f,0x22,0x9f,0xc6,0xc4,0xac,0xad,0xbd,0xc7,0x2f,0xc5,0x9b,0xb8,0xd3,0xfc,0xf2,0x47,0x79,0x0d,0x68,0x80,0xc0,0x8e,0x66,
  0xa3,0xf1,0x8e,0x6e,0xbf,0x70,0xfd,0x8a,0xc5,0x8a,0xed,0x72,0x53,0xeb,0x34,0xe4,0x5b,0x66,0x73,0x73,0x81,0x1c,0x32,0x61,
  0xee,0x7e,0x11,0x1c,0x2a,0x97,0x30,0xe9,0xe1,0xf6,0x0f,0x6f,0x91,0x5c,0xd2,0x28,0x26,0x14,0x4e,0x6b,0x09,0xa3,0xb7,0x20,
  0x20,0x48,0xe3,0x8b,0x92,0x1a,0x04,0x3b,0xe7,0x2b,0xe4,0x69,0xf5,0x02,0x47,0x5a,0x9e,0xa8,0xab,0xf6,0x05,0x46,0xb2,0xcd,
  0xe5,0x64,0x55,0x69,0x84,0x9d,0xa0,0x93,0xa2,0xc8,0xce,0xc5,0x7e,0x50,0xd2,0xc6,0x8b,0x4e,0xdf,0x23,0x7e,0x78,0xd0,0xa1,
  0xdd,0x21,0xe4,0xae,0x10,0xa6,0x9f,0x5c,0x89,0xde,0x5c,0xdd,0xd0,0xc6,0x5e,0x5e,0x08,0xbe,0xd0,0xde,0x88,0x9c,0xc1,0x8b,
  0x20,0xc0,0xb6,0x65,0x7e,0xee,0xfb,0xc9,0x7e,0xd5,0x6e,0x6e,0xa4,0x33,0x11,0xc3,0xc4,0xeb,0x18,0xcc,0x97,0x6e,0xd8,0xbb,
  0xc9,0x7d,0x5e,0x87,0x57,0x64,0xa9,0x2f,0x41,0xd1,0x63,0x2b,0xd7,0xa8,0xb5,0xb4,0xd1,0xcb,0x7c,0xa3,0xf6,0xf2,0x91,0xf2,
  0xe0,0x6d,0x2f,0x6d,0xf4,0x44,0xe6,0x1a,0xed,0x2c,0xdf,0x84,0x78,0xae,0xd1,0xee,0xf2,0x46,0x79,0xf0,0xee,0x97,0xe5,0x26,
  0x26,0x7a,0xee,0x02,0xef,0xdf,0xd9,0xdb,0xfa,0x13,0x81,0xb6,0x5b,0xa1,0x67,0x60,0x8c,0x29,0xb5,0xb0,0x7a,0x2b,0x92,0x72,
  0xf1,0x18,0x4f,0x45,0x3f,0x40,0x3f,0x10,0x1a,0x66,0x5b,0xec,0x2f,0x7c,0x0c,0xfc,0x5a,0x3c,0x1c,0x68,0x95,0x1e,0xba,0x14,
  0x1e,0x83,0x12,0xb9,0xf2,0x66,0x7e,0x0a,0x8d,0x40,0x0d,0xb5,0x51,0x34,0x11,0xf7,0xb2,0x0a,0x39,0x4a,0x7f,0x16,0x16,0x48,
  0xb0,0x5f,0xa0,0x4c,0x48,0xa0,0xd6,0xc4,0xd9,0x18,0x9d,0x65,0xc8,0x0f,0xb9,0xc1,0x50,0x05,0xa6,0x75,0xd0,0xe4,0x2a,0xfa,
  0x6c,0xd3,0x82,0xb5,0xb9,0x5d,0x6f,0xed,0x24,0xd9,0xb4,0x2c,0x99,0xe1,0xbd,0xbb,0xad,0xe6,0x5e,0xf3,0x61,0xbd,0xdd,0x64,
  0xcf,0xdc,0xc1,0x18,0xd9,0x1a,0x8b,0x11,0xe8,0x68,0xd8,0x9c,0x24,0x8e,0xa2,0x1f,0x68,0x62,0xe4,0x2d,0xdb,0x50,0x40,0xa9,
  0x49,0x88,0x22,0x19,0x9c,0x48,0x43,0x1e,0xf7,0xf4,0x55,0xd2,0x50,0x4b,0xe9,0xdd,0x0a,0xed,0xcb,0x25,0x72,0x10,0x2b,0x95,
  0x2a,0x66,0x93,0x0f,0xbe,0xff,0x4d,0xc4,0x83,0xd0,0xd4,0x63,0xdb,0xb7,0xfe,0xd8,0xf2,0x06,0xee,0xa5,0x90,0xd7,0xc5,0x43,
  0xc5,0xaf,0x97,0x3b,0xa6,0xc1,0xda,0x01,0xcc,0x6e,0x3d,0x3f,0x2d,0x8a,0xad,0x41,0x57,0xcf,0x5c,0xcf,0x4f,0x6f,0xb5,0x29,
  0x12,0xc0,0x2a,0x0d,0xb4,0x73,0xed,0x8d,0x72,0xd7,0xf0,0xa9,0x2b,0xfd,0x05,0x43,0x9c,0xd2,0x21,0xc3,0x12,0x53,0xe0,0xfe,
  0xee,0xee,0xf6,0x6e,0x91,0xcd,0x7a,0x2c,0x40,0x5e,0x7f,0xc4,0x9d,0xa7,0xb0,0x63,0xdc,0x53,0x53,0xb0,0x17,0x79,0x7f,0x41,
  0xd7,0xa4,0x63,0x18,0x0b,0xa1,0x83,0xd7,0x51,0x27,0xd1,0x89,0x8d,0x0c,0x12,0xf4,0x68,0x03,0xc3,0xf9,0x0b,0x27,0x96,0xf3,
  0xc7,0x1c,0xd7,0x0c,0xcd,0xb6,0x05,0xfd,0x43,0x05,0x51,0x82,0xe0,0x99,0xca,0x5b,0xdf,0x82,0xff,0x5a,0x00,0x53,0x5c,0xf1,
  0x0e,0x3b,0x71,0x7c,0x0a,0x62,0x63,0x92,0xa1,0x03,0xaa,0x81,0x6d,0x89,0xdb,0xbf,0x81,0xa6,0xca,0x40,0xdd,0xc9,0xf8,0x4a,
  0xc8,0xfd,0x41,0xcd,0xd8,0x10,0xed,0x60,0xd1,0x5f,0x37,0x07,0x11,0x50,0xcd,0xd1,0x6d,0x02,0x3a,0xb7,0x35,0x52,0x7a,0xd0,
  0x3d,0xf6,0x23,0xa7,0x98,0x9e,0x54,0x0c,0xa2,0x2d,0xbf,0x76,0xb6,0x60,0xa3,0x9c,0xae,0x8f,0xea,0xf9,0x9e,0xf3,0x44,0xed,
  0xb8,0xbe,0x35,0xb4,0x06,0xab,0x3a,0xcb,0x4a,0x06,0x8c,0x40,0xde,0x0a,0x25,0x49,0x41,0x8c,0x8f,0x6a,0x70,0xf9,0x4d,0xc3,
  0x60,0xb2,0xee,0x85,0x0d,0xd2,0x8b,0xf1,0xc0,0x63,0xcf,0x8e,0x8a,0x02,0x49,0xee,0xe4,0x11,0xd6,0xf0,0x57,0x08,0xba,0x28,
  0xb2,0x0d,0x05,0x26,0xed,0x79,0xe2,0x52,0x9d,0xc8,0xe5,0x1e,0xba,0x02,0x0f,0xde,0x72,0x20,0x80,0x09,0xfe,0x41,0xe9,0x08,
  0x32,0xbc,0xc8,0xea,0xd5,0x1b,0x9a,0xf2,0x1a,0xdb,0x9d,0x4b,0x77,0x70,0x81,0xee,0x91,0x55,0x9a,0x58,0xde,0x38,0x1a,0xaf,
  0x37,0xbd,0xfd,0x6c,0xaf,0x35,0x9e,0xeb,0x4a,0x98,0x0a,0x88,0x89,0x1f,0x6d,0xf4,0x86,0xd8,0x8b,0x1a,0x45,0x29,0xf7,0x21,
  0x6e,0x36,0x74,0x54,0xe6,0xdc,0x99,0xa1,0x86,0x03,0xb0,0xd8,0x3e,0xc5,0x51,0xeb,0x27,0xd2,0x82,0x4f,0x09,0x74,0x54,0xe8,
  0x9d,0xc4,0x10,0x47,0x61,0x94,0xba,0x0f,0xca,0xe2,0xc6,0xc1,0x31,0xfc,0x85,0x9d,0x12,0xbb,0x5a,0x64,0xb9,0x00,0xe8,0xfd,
  0x0d,0xd8,0xcc,0x31,0xda,0x5f,0x5e,0x51,0x06,0x28,0x07,0x9e,0xca,0xdb,0xcf,0xce,0xd2,0xaa,0x38,0xf8,0x53,0xb9,0x74,0x70,
  0x84,0xb2,0xa6,0x20,0x40,0x50,0xd9,0x26,0x43,0x40,0x4a,0x21,0xa8,0x45,0x9d,0x03,0x18,0x58,0x1f,0x9e,0xca,0x2d,0x62,0x44,
  0xfe,0x5b,0xc4,0x13,0x50,0x07,0xe1,0x14,0x94,0x07,0x0c,0x54,0xe7,0xb1,0xaf,0x9b,0xd1,0x83,0x8b,0x74,0x1b,0x18,0x06,0xd3,
  0xba,0xd1,0x6a,0xf2,0x8b,0xf6,0x68,0x5a,0x09,0xdd,0x93,0x22,0x58,0xf1,0x32,0xaa,0x7d,0x35,0x11,0x56,0x89,0xaf,0xac,0xc3,
  0xf6,0x07,0xae,0x29,0xd0,0x1b,0xb6,0x45,0xdd,0xc1,0x6c,0x60,0x2c,0x2a,0x83,0x0d,0xc4,0x67,0x84,0x2d,0x86,0xe8,0x82,0xbf,
  0x88,0x0b,0xe6,0xa2,0x83,0x0f,0x11,0xd2,0x60,0x18,0xe6,0x42,0x43,0x87,0x32,0x36,0x1c,0xaa,0xff,0xa3,0x3b,0x01,0x2a,0xf3,
  0x61,0x07,0x77,0xd6,0x15,0x8f,0xd1,0xb9,0x9f,0x65,0x06,0x13,0x55,0x2b,0x55,0x15,0xe8,0x00,0xde,0xb7,0xe9,0x0a,0xd6,0x68,
  0x4c,0xe7,0xaa,0x3c,0x5d,0x4d,0xca,0xe9,0xb4,0x50,0xab,0x47,0xb5,0xd6,0x0a,0x3e,0x10,0x78,0xe7,0xee,0x68,0x64,0x8b,0xbc,
  0x9a,0xf6,0x14,0x5f,0xb2,0xdb,0x3f,0x86,0x43,0x47,0xa7,0x13,0xdc,0x18,0xe2,0xb6,0x71,0x2b,0xf5,0x94,0x3e,0x25,0x15,0x9e,
  0x05,0xdc,0x48,0x8b,0x90,0x11,0xdd,0x47,0x30,0x75,0x1d,0x50,0x20,0x4f,0xe1,0x2f,0x6b,0x9d,0x16,0xcb,0x9a,0xa4,0x62,0x3b,
  0xac,0xd9,0x2e,0xad,0xe9,0x0b,0x1f,0x6c,0x45,0xa0,0x59,0xfa,0x2c,0xab,0xe9,0x39,0xfc,0x02,0x88,0xbb,0x87,0x1f,0x65,0xf5,
  0x86,0x36,0x9f,0x62,0x2a,0xdc,0x13,0xfa,0x64,0xc7,0x96,0x34,0xcb,0xaa,0xf7,0x41,0x11,0xbe,0x70,0x03,0xd0,0x4c,0x8e,0xc3,
  0x6f,0x65,0xb5,0x4d,0xcb,0x71,0x31,0x80,0xe2,0xb8,0xec,0x2c,0x70,0xca,0x6a,0x5a,0xce,0x25,0xc7,0xb3,0x09,0x28,0xa8,0x41,
  0x99,0x60,0xcf,0xc3,0xe7,0xb2,0x36,0x03,0xe4,0xbc,0x81,0xbf,0xb3,0x71,0xf0,0x93,0x85,0x5c,0x22,0x66,0xc0,0x82,0xfe,0x2a,
  0x2d,0xea,0xed,0x69,0xba,0xd1,0x12,0xb4,0xdb,0xd6,0x50,0x28,0x62,0x61,0xee,0x90,0xbd,0x80,0xa7,0x52,0xd4,0x5b,0x13,0x30,
  0x09,0x0e,0x7a,0xf8,0xc1,0x7a,0x60,0x85,0x95,0xe2,0x5f,0x02,0x69,0xe2,0x06,0xf5,0x44,0x7d,0x29,0xab,0x4b,0xe1,0x6e,0xd7,
  0x32,0x01,0x4b,0x47,0xd1,0xd7,0xb2,0xfa,0x13,0xfe,0x11,0xc0,0x7e,0xc1,0xfb,0xd7,0x20,0x9c,0xfc,0x71,0x59,0xd5,0x76,0x73,
  0xe7,0xe1,0xc6,0x01,0xfe,0xd5,0x9c,0x86,0x4a,0xe9,0xd4,0xc8,0x3d,0x3a,0x7b,0x1a,0x31,0x6e,0xc4,0xa4,0x7a,0x7f,0x83,0x09,
  0xba,0x77,0x00,0xbf,0x5f,0x7e,0xfb,0xbb,0x26,0x47,0x4b,0xab,0xdb,0x62,0x08,0x14,0xf5,0xe5,0xdf,0x7e,0x2d,0x06,0x91,0xea,
  0x50,0x82,0x09,0x4c,0xe7,0xcb,0xdf,0xfe,0xbd,0xac,0x96,0x3a,0xe0,0xf3,0xe5,0xb7,0x3f,0x56,0x1d,0x1b,0x7d,0xe8,0xd8,0xe0,
  0x73,0x79,0x83,0xc8,0x2d,0x62,0x61,0xfa,0x84,0xd6,0x5a,0xba,0x30,0xd5,0x9e,0x0a,0xbb,0x82,0xf5,0x85,0x51,0x19,0xe1,0x28,
  0x95,0x3a,0x07,0xa6,0xd6,0x07,0x8b,0x44,0x90,0xd6,0x17,0xb2,0x22,0xd0,0xc1,0x8b,0xa3,0x53,0xe8,0xa8,0x77,0x7a,0xf6,0xfc,
  0xd5,0xd3,0x93,0x57,0x25,0x73,0x05,0x24,0xab,0x27,0x3c,0xec,0x8a,0xaa,0x37,0xca,0x90,0x36,0x5b,0x03,0xef,0x34,0xf7,0xe2,
  0x3e,0x3e,0x97,0x42,0x1f,0x7a,0xfa,0x74,0x0a,0xc2,0xd3,0xd8,0x1b,0x6a,0xbf,0x88,0x92,0x1d,0x32,0xa4,0xa4,0xc7,0xbf,0xa0,
  0x09,0xe6,0x4c,0x69,0x86,0xae,0x2e,0x73,0x9f,0x3b,0x43,0x37,0x11,0xd6,0x16,0x29,0x61,0xe8,0x13,0x55,0xbd,0x87,0xb1,0x8b,
  0xc4,0x30,0x5a,0xcd,0x1e,0xb0,0x6c,0xf3,0x43,0xc0,0x6d,0x95,0x46,0x7b,0x8f,0x9d,0x0a,0x39,0xc4,0xa8,0x80,0x33,0x10,0xcb,
  0xb3,0x17,0x8f,0x4c,0x3e,0xa5,0x23,0x4d,0x4f,0x4e,0x7b,0x05,0x5e,0xc3,0xf0,0xed,0x93,0xa9,0xf7,0x95,0x9e,0x16,0x0e,0xfb,
  0x78,0xfd,0x47,0x57,0x62,0xa4,0xb9,0x28,0x8d,0x6f,0x84,0x15,0xbe,0x69,0x8f,0xc5,0x93,0x8b,0x68,0x0d,0xfa,0x61,0xca,0x4a,
  0xf6,0x04,0x1b,0x2f,0xd8,0x71,0x6d,0x53,0x46,0x13,0x57,0x79,0xdd,0xde,0x72,0xf3,0xfc,0xcc,0xf5,0xeb,0x3f,0xd2,0x91,0x34,
  0x4b,0x69,0x43,0x89,0x3b,0x73,0x90,0x9c,0x54,0x3b,0x5b,0x23,0x83,0x42,0x6f,0xb6,0xfc,0xac,0x66,0x9c,0x2d,0xfc,0xd9,0x59,
  0x01,0x8e,0xa7,0x5f,0x07,0xc7,0xd3,0x55,0xe1,0x40,0x9d,0x77,0x05,0x30,0x8e,0xbf,0x0e,0x8c,0xe3,0x15,0xc1,0x58,0xa0,0xda,
  0xe1,0x91,0x01,0x49,0x27,0x1a,0x2f,0x59,0x05,0xa3,0xcf,0x17,0xee,0x64,0x0a,0x1d,0x03,0x91,0x49,0xd6,0xdb,0xae,0x1f,0x07,
  0xc0,0x32,0x05,0x29,0xb9,0x1e,0x36,0x3c,0x13,0x2a,0xe6,0xf6,0xe7,0x9c,0xb3,0x7e,0x6c,0xf1,0x91,0xe3,0x7a,0x02,0xf8,0x12,
  0x0f,0x14,0x05,0xd3,0xc4,0x40,0xa7,0x88,0x03,0xbc,0x8e,0x31,0x84,0x77,0x03,0x6c,0x24,0x2d,0x28,0x76,0x0e,0x2a,0x33,0x86,
  0x2b,0x18,0xc5,0x99,0x75,0xd3,0x53,0x25,0x3c,0x0d,0x41,0x69,0x1c,0x63,0x0b,0xad,0x1d,0xcc,0x1b,0xa5,0x81,0x17,0x6a,0xb5,
  0x05,0x26,0x82,0xb8,0xc2,0xdb,0xa4,0x7a,0xc2,0xf7,0xc1,0xfe,0x83,0x3d,0x2f,0x1d,0x54,0x51,0x6f,0x33,0xad,0x13,0x24,0x59,
  0x13,0x7c,0xfd,0xc4,0xb2,0x63,0x5f,0xc9,0x90,0xbe,0xe3,0x3d,0x1b,0x53,0x30,0x2f,0x1b,0x7d,0xcb,0xa9,0x61,0x2c,0x2f,0x74,
  0x12,0x6c,0xb9,0x03,0x50,0xee,0xea,0x1e,0x28,0xf3,0x7c,0x92,0x8e,0xfa,0xa8,0xae,0x16,0xc1,0xa1,0xde,0x66,0xe0,0x58,0xc9,
  0x02,0xa0,0x5b,0xa3,0x00,0xe3,0xaf,0xcf,0x8f,0xf4,0x68,0xdf,0x53,0x74,0x0d,0x81,0xa1,0xcb,0xc9,0x9d,0x38,0x79,0xfc,0xaa,
  0x57,0x40,0x85,0x63,0xd7,0x2b,0x0f,0x6a,0x6d,0xb7,0x8b,0xfd,0x42,0xb1,0x4f,0xe8,0x15,0xa6,0x3a,0xdc,0xfe,0x57,0x74,0x6d,
  0x20,0x5f,0xa8,0xc0,0x37,0x25,0xa6,0x33,0x3c,0x4d,0xaf,0x1f,0xaf,0x76,0x44,0x40,0x59,0xb6,0x16,0x48,0x2f,0xca,0x36,0x66,
  0x1a,0x8c,0xca,0x99,0xc7,0x04,0xc8,0x4e,0xb0,0x1c,0xfa,0x3c,0x49,0x7d,0x8a,0xf2,0x44,0x4e,0x1e,0xd7,0x7b,0xa0,0xb6,0x4c,
  0xc0,0x24,0x07,0x41,0x07,0xdb,0xc8,0xe4,0xa8,0x60,0x46,0x74,0x3e,0xff,0x05,0x56,0x79,0xc9,0x0b,0x9d,0x7b,0xbb,0xcd,0x28,
  0x63,0x04,0xbe,0xc5,0x29,0x23,0x2d,0x3d,0x92,0x83,0xdb,0x53,0x1d,0xc3,0x3e,0x6e,0x40,0x03,0x95,0xa4,0x15,0xe0,0x36,0x17,
  0xd6,0xec,0x89,0x41,0xe1,0x80,0xdb,0x5a,0x86,0x4a,0x32,0xc8,0x91,0x89,0x37,0x7b,0x44,0x8e,0x44,0xa7,0x38,0xf2,0xc8,0xb1,
  0x52,0xce,0x9b,0x88,0xdd,0xc6,0x41,0xc7,0xf2,0x10,0x64,0xb2,0x5a,0x61,0x84,0x53,0x04,0x60,0x28,0x1d,0x9d,0xd6,0x4b,0x9c,
  0x8f,0x7c,0x5a,0xec,0x7a,0xd4,0x46,0x7d,0x58,0x3e,0x6a,0x99,0x5f,0x32,0x84,0x80,0x66,0xbf,0x05,0x24,0x5b,0x0a,0x09,0xd6,
  0x5a,0x13,0x98,0x1c,0xc1,0x16,0x03,0x93,0xe8,0x30,0xc0,0x58,0x81,0xb4,0xfc,0xeb,0xc8,0x27,0xd0,0x23,0xc3,0x75,0x8c,0x91,
  0x03,0x93,0x92,0x69,0x0a,0x73,0xba,0x53,0x79,0x71,0x00,0xd1,0xb9,0xf0,0x40,0x0b,0xc2,0x84,0x5b,0x74,0x2f,0x75,0xd8,0x49,
  0x8f,0x3d,0xef,0x9d,0xb3,0x9f,0x4f,0x9e,0xbd,0x62,0x6f,0x9e,0x9d,0x15,0x47,0x6c,0x45,0xd8,0x0c,0x49,0xfb,0x19,0x30,0x34,
  0x5e,0xfe,0x81,0xcd,0x17,0xa6,0x8f,0xb8,0xce,0x00,0x04,0xcd,0x45,0x92,0xe1,0xd5,0x18,0x83,0x54,0xec,0x1a,0x5b,0xc1,0x14,
  0x61,0x35,0xf2,0x96,0x31,0x62,0x38,0xbe,0x57,0x64,0xec,0x0e,0xc6,0x8b,0xe3,0xc1,0x33,0x6b,0x68,0x9d,0x15,0x06,0x82,0xdf,
  0xbe,0x38,0x7a,0x45,0xdc,0x0b,0xca,0x1b,0xf9,0x4b,0x73,0xc1,0x60,0xba,0x55,0x21,0x9a,0x3d,0x9d,0x3a,0x1f,0xfb,0xc9,0xed,
  0x25,0x20,0x08,0x30,0x12,0xa3,0x52,0xec,0xe3,0x0b,0x21,0x70,0x25,0x3c,0x57,0x2a,0x1f,0x71,0x6b,0x37,0xe6,0xb2,0xa2,0x14,
  0x25,0x98,0x06,0xb5,0x46,0x48,0xea,0x04,0x24,0x83,0x45,0x51,0xe9,0x11,0x0c,0xff,0xab,0x5b,0x3b,0xc8,0xeb,0x03,0x4f,0x8a,
  0xbc,0x7e,0x0e,0xf0,0x38,0x43,0x81,0x3f,0xa5,0xc0,0x24,0xf2,0xa2,0x80,0x4d,0x92,0xf9,0x04,0xe8,0xc8,0x28,0xaf,0x84,0x87,
  0x24,0x07,0x4a,0x3b,0x42,0xf4,0xc1,0xf7,0x33,0x75,0x29,0x34,0x53,0x54,0x17,0x8f,0x79,0x64,0xfb,0x55,0x89,0x8f,0x45,0xb5,
  0x6d,0x32,0x84,0xd2,0xd5,0x5f,0xe0,0x12,0x14,0xd6,0xc6,0x70,0x96,0x99,0xa9,0x7d,0x2c,0x24,0x05,0xb7,0x43,0x45,0x44,0x53,
  0xb7,0xbd,0x81,0xb4,0xa6,0xfe,0xc1,0x1d,0x30,0x07,0x61,0xd1,0x84,0x52,0x0d,0xbb,0xef,0x0c,0xe4,0x04,0x14,0xd8,0x46,0xcd,
  0x38,0xf2,0x27,0xc2,0x81,0xcf,0xe4,0x14,0x12,0x3c,0xbc,0xa5,0x3b,0x17,0xe0,0xcb,0x13,0xba,0x48,0x02,0xbf,0xa9,0xbc,0x1b,
  0xf8,0x72,0x0a,0x43,0x4f,0x38,0xbe,0x03,0x3e,0x97,0xf0,0xd9,0x1b,0x70,0x74,0xa8,0x61,0x5f,0x81,0x74,0x25,0xbe,0x7a,0xfd,
  0x51,0x70,0x6c,0xf4,0x82,0x5f,0xe2,0xe3,0x8f,0xae,0x33,0xc4,0x4d,0x91,0xbe,0x4e,0x84,0x8f,0xbd,0x04,0xb6,0x37,0xc3,0x51,
  0xb0,0x23,0x2e,0xfb,0xb0,0xd6,0x18,0x53,0xc7,0x1a,0x02,0x48,0x89,0x82,0x8d,0x58,0xaf,0xe7,0x63,0x76,0x9e,0x33,0xb6,0x26,
  0x13,0x61,0x87,0x75,0xc3,0x33,0x48,0xf0,0xf4,0x0c,0xea,0x62,0x78,0x82,0x8f,0xe0,0xe1,0x95,0x6b,0x79,0xd8,0x1d,0xa8,0x72,
  0x83,0x0b,0xf6,0xcc,0xb5,0xd5,0x83,0xdb,0xf7,0xf0,0xf3,0xf4,0x25,0xfc,0xa5,0x43,0x88,0xc2,0x64,0xc7,0x81,0xf4,0x7c,0x2f,
  0x2a,0x38,0x07,0xed,0xfa,0xc2,0x16,0xf4,0x2c,0xf9,0xc7,0x6b,0x76,0x2c,0xe8,0xe1,0xf1,0xab,0x23,0xf5,0x17,0xcd,0x19,0xc9,
  0x71,0xc0,0xc7,0x98,0xdd,0x19,0x7d,0xb2,0x33,0x97,0x06,0x04,0xee,0x13,0xac,0xdd,0x6c,0xb5,0xc3,0xef,0x11,0x20,0x88,0xbc,
  0x6b,0x06,0xf8,0x72,0x2e,0x10,0x17,0x96,0xe7,0xf1,0x5f,0xdc,0x00,0x7b,0x7e,0x29,0x7c,0xde,0xc7,0xb4,0xc0,0x18,0x9f,0xa0,
  0x4d,0x85,0x8f,0x80,0x18,0x85,0x56,0xcb,0x79,0xec,0x12,0x90,0xa7,0x12,0x2d,0x39,0x18,0x60,0x37,0x82,0x78,0x06,0x54,0xe0,
  0xa9,0x3a,0xc2,0x76,0x11,0xd3,0x7f,0x09,0xd0,0x6f,0x86,0x95,0xf9,0x80,0xe2,0x19,0xf8,0xd5,0xb5,0xb9,0x64,0x2f,0x90,0xbe,
  0x3c,0x5a,0x8c,0x0b,0x82,0xca,0x76,0x67,0xd8,0x14,0xd4,0x31,0x9c,0x51,0x0f,0x4c,0x2d,0x44,0x49,0xf4,0xfc,0xcc,0x9a,0x8e,
  0x5d,0xdf,0x1a,0xe0,0x24,0x31,0xbc,0x24,0x49,0x27,0x79,0x1b,0x0e,0xf8,0x7a,0xe0,0xbb,0x53,0x9a,0x42,0xcf,0xe5,0x53,0x1a,
  0xd7,0xb6,0xb8,0x9a,0x91,0x70,0x09,0x6c,0x95,0x24,0x11,0xa2,0x02,0x04,0xe7,0x05,0xd6,0x3e,0xe3,0x96,0x43,0x10,0x4d,0x07,
  0xae,0x74,0x14,0x02,0xb1,0xf9,0x31,0x68,0xa8,0x03,0x60,0xec,0x78,0xfa,0xcf,0xe0,0xd3,0xc5,0x0b,0x0c,0xd9,0xc9,0x35,0x8d,
  0xa8,0xfb,0x72,0xd2,0xb4,0xc2,0xcc,0x6b,0xd8,0x54,0x43,0x32,0x7e,0x34,0xe6,0x34,0xe8,0xf9,0x58,0x70,0xe4,0xb4,0xe8,0xf9,
  0x29,0x98,0x91,0x3e,0xe1,0x93,0xf0,0xe0,0xc0,0x58,0x08,0x4e,0xe0,0xe0,0xb7,0x04,0x37,0x4f,0xb8,0x25,0xaf,0x8d,0xf7,0x7b,
  0x21,0xa7,0x4c,0xd5,0x19,0x57,0x64,0x95,0x30,0xd9,0x8a,0x7c,0xb9,0xe1,0x44,0xfa,0x84,0x40,0x4a,0x24,0x23,0x94,0x28,0x5a,
  0xc7,0x9b,0x2d,0x3c,0x3f,0x21,0x7a,0x40,0x84,0x47,0xa4,0xfe,0x7c,0x20,0x88,0x54,0xb9,0x4f,0x6c,0x06,0x0a,0x8e,0xce,0x2a,
  0xa7,0x9c,0x64,0x1a,0x8c,0x0d,0x46,0x6e,0x45,0x0d,0x6f,0x99,0x38,0xe3,0x77,0x46,0xee,0x36,0x0b,0xa8,0x9f,0x3b,0x67,0x0f,
  0x65,0xa9,0x83,0xb9,0xc6,0xfb,0x6a,0x04,0x7e,0x03,0x3a,0x38,0x01,0xed,0xad,0x52,0x71,0x6a,0x56,0xb5,0x7b,0x60,0xba,0x83,
  0x00,0xb8,0xdd,0x6f,0x7c,0x00,0xce,0xbd,0xee,0x91,0x83,0x1e,0x06,0x35,0xee,0x1a,0x9b,0x96,0x59,0x6d,0x70,0xd3,0xac,0x38,
  0x62,0xc6,0x5e,0x93,0x0b,0x9f,0xda,0x54,0xab,0x11,0x4a,0xc6,0x96,0x03,0xa2,0xe3,0xc6,0x40,0xdf,0xa7,0xd1,0x31,0xbe,0xfc,
  0xf6,0xf7,0x2f,0xbf,0x7d,0x66,0x3d,0x60,0xbe,0xdb,0xdf,0x61,0x0b,0x61,0x7d,0x31,0x13,0x4a,0x78,0x84,0xde,0x51,0xad,0x12,
  0xb9,0xa6,0x61,0x07,0xc0,0xd3,0x17,0xca,0xef,0xa1,0x17,0x23,0xdb,0x28,0x37,0x29,0x36,0xf9,0xb7,0x5f,0xbf,0xfc,0xf6,0x47,
  0xd4,0x1b,0x36,0xf8,0xf2,0xb7,0x7f,0xc7,0x83,0x08,0xe3,0xf0,0x09,0x1a,0xce,0x30,0x75,0x99,0x8e,0x78,0xe8,0x4e,0x1c,0x44,
  0x39,0xfa,0x50,0xe3,0x71,0xc3,0xae,0x00,0xbd,0x30,0x5b,0x5a,0xa1,0xc4,0x73,0x0a,0x95,0xc8,0x7d,0x43,0x5e,0xfb,0x68,0x63,
  0xae,0xab,0x63,0x2a,0x48,0x96,0xa1,0xcb,0x34,0x01,0x28,0x9e,0x28,0xf1,0x85,0x72,0x93,0x96,0x75,0xc2,0xba,0xcc,0x83,0x2d,
  0xda,0x51,0x28,0x49,0xbb,0x4a,0x8b,0xa7,0xf9,0xdb,0xdf,0x55,0x47,0xfa,0x9c,0xb0,0x97,0xc1,0x58,0x05,0x5f,0xa1,0x1b,0xdd,
  0x11,0xaa,0x81,0x36,0xc5,0xc3,0x2c,0x11,0x72,0x60,0x9b,0x04,0xf5,0x78,0x98,0xab,0x1f,0xae,0x48,0x69,0x13,0x56,0xe1,0x91,
  0x70,0x75,0xcc,0x6a,0x96,0xf9,0x32,0x78,0x7d,0x44,0xd7,0x63,0x66,0x57,0x61,0x0b,0x97,0xab,0xcb,0x7e,0xa6,0xe4,0xb6,0xe4,
  0x22,0x09,0x92,0x55,0x91,0x97,0x55,0xf5,0xc4,0xb0,0x13,0x1c,0x1e,0xfa,0x4b,0x92,0xe8,0x31,0x76,0x11,0x06,0x4e,0x70,0xc5,
  0x94,0xab,0x35,0x3b,0xf4,0xf8,0xf6,0xf3,0x54,0xcd,0x30,0xf6,0xae,0x2e,0xc4,0x69,0x1a,0x83,0xb1,0x8b,0x35,0xdb,0x25,0xd4,
  0x3e,0x0a,0x3c,0x3a,0x1c,0x89,0x86,0x8b,0xc3,0x30,0x53,0x97,0x44,0x03,0xba,0x5a,0xb3,0xb5,0x7f,0x04,0xa6,0x02,0x1c,0xa9,
  0xae,0x11,0xd4,0x79,0xc4,0x26,0x80,0x12,0x5f,0x84,0x17,0x72,0x78,0xdd,0x70,0xc7,0x6d,0x4c,0xf8,0x34,0xe2,0xc1,0xca,0x0d,
  0x7c,0xce,0xab,0xd5,0x06,0xd6,0xac,0x54,0x78,0xad,0x0f,0x85,0xbc,0xe1,0x28,0xeb,0x48,0x3c,0x42,0xcb,0x5e,0x8a,0x4a,0xbf,
  0xe1,0xd4,0x0c,0x13,0x98,0xfb,0xc6,0xa3,0xb3,0xdf,0xd6,0x25,0x28,0xb0,0x1d,0xe0,0x7e,0x90,0x6a,0xd0,0xba,0x54,0x58,0xa8,
  0xd1,0x53,0xb2,0x22,0x2e,0xd2,0x0f,0xff,0x83,0xa4,0x48,0x81,0x1b,0x8b,0x0b,0xb1,0xae,0xa8,0x10,0x00,0xad,0x68,0xe8,0xe2,
  0x02,0x96,0x04,0x84,0x27,0x99,0x6a,0x30,0x6a,0xe6,0xa2,0x33,0x04,0x4d,0xbf,0x6b,0x2c,0xfd,0xcc,0x51,0xcb,0x48,0x2e,0xc8,
  0x52,0x0f,0xf1,0x8d,0x52,0xea,0x31,0xbe,0x82,0x09,0x1e,0xbf,0x62,0xe2,0xb5,0xaf,0x14,0xad,0x7a,0x3b,0x3a,0x29,0x94,0x6a,
  0x15,0x95,0x68,0x37,0x07,0xa4,0x5a,0xc4,0xe7,0xf8,0x53,0xa5,0x74,0xba,0x3e,0xdd,0xb3,0xf5,0x31,0x0d,0x8e,0xde,0x30,0x73,
  0xab,0x46,0x1a,0x00,0xd5,0x30,0x73,0x6b,0x40,0x5c,0x12,0x35,0x48,0x8e,0xd6,0xe3,0xd2,0x24,0xc7,0x9e,0x52,0x7d,0xd1,0xa9,
  0xbf,0xb8,0x72,0xf4,0x94,0xb9,0x24,0x29,0xd5,0x22,0x2a,0xd1,0xee,0x7b,0x80,0xa7,0xd4,0xa9,0x9d,0xe4,0x39,0x3a,0x07,0xa3,
  0x95,0xd0,0xfe,0x98,0x9c,0x06,0xc1,0x9e,0x92,0xdc,0xa6,0xf0,0x29,0x4e,0x3f,0x0a,0x9f,0x31,0x87,0x28,0xfc,0x8a,0xb9,0x3e,
  0xe1,0xd7,0x37,0x4a,0x83,0x8a,0x72,0x6c,0xc2,0xaf,0xc7,0x4a,0x23,0x48,0x5f,0x3b,0xa8,0x0a,0xb4,0xd4,0x76,0x28,0x28,0x4a,
  0x09,0x81,0xe2,0x4c,0xf6,0x06,0x94,0xe8,0x01,0x55,0x78,0xcc,0xc5,0x3d,0x93,0xf9,0xe4,0xce,0x63,0xc1,0xab,0x7c,0x9a,0x06,
  0x14,0xea,0xf9,0xf1,0xd8,0x65,0xe8,0xbd,0x41,0x30,0x35,0xb7,0x47,0x38,0x76,0xe2,0x96,0xc0,0x96,0x91,0x27,0x01,0xbf,0x4f,
  0xc3,0x79,0xc7,0xf6,0x35,0x7c,0xcf,0x1d,0xd0,0x4f,0x95,0x61,0xea,0x02,0x4e,0x32,0x73,0x0c,0x9d,0xe0,0x4c,0x9d,0xeb,0xd6,
  0x4a,0xc2,0xf3,0xd8,0x5a,0x49,0xea,0xec,0xb2,0x56,0x1e,0x1d,0x36,0x86,0xa2,0xdc,0x89,0x60,0x28,0x4b,0x9f,0xe1,0x8d,0x0a,
  0xc2,0x83,0xb7,0x38,0x8d,0xe4,0x48,0x6c,0xf2,0x14,0x9f,0x4d,0xa5,0x79,0xc6,0x8e,0x7d,0x85,0x9b,0xd8,0x09,0xaf,0xe4,0x45,
  0xc6,0x3b,0xae,0x56,0x33,0x76,0x56,0xa7,0x1f,0x9f,0xa6,0x1f,0x8f,0x91,0x49,0x0a,0x1c,0xa9,0x48,0xfb,0xb9,0xb3,0xaa,0x38,
  0xe5,0xf4,0x59,0x4f,0xad,0x84,0xce,0x71,0xa2,0x92,0x09,0xc2,0x84,0x0d,0x02,0x29,0x61,0x12,0x47,0xd1,0xaa,0x75,0x8d,0xd0,
  0x93,0x63,0xc4,0x1a,0x97,0xb8,0xea,0x3a,0xdd,0x03,0x14,0xb9,0xef,0x9c,0x86,0xac,0x39,0x8d,0x11,0xfc,0xef,0xbf,0xa7,0xdd,
  0xe4,0xaa,0x7b,0x70,0xd5,0xf0,0xdd,0x9e,0x8f,0x3a,0x46,0xa5,0x75,0xbf,0xda,0x98,0x72,0x93,0x30,0x58,0x69,0xd7,0x8c,0xa6,
  0x01,0x1b,0xcc,0x2f,0xae,0xe5,0x54,0x0c,0x23,0x16,0xc9,0x72,0xd4,0xef,0x8e,0x71,0x03,0x92,0x9d,0x29,0xde,0xcd,0x0f,0x02,
  0xa2,0x32,0x6e,0x78,0x60,0x69,0x89,0x4a,0xab,0xb6,0x5d,0xad,0x41,0x2f,0xb5,0x51,0xfe,0xdd,0x76,0x6d,0x57,0xbd,0xeb,0xe7,
  0xdf,0xed,0xd6,0x1e,0xd0,0xbb,0x39,0x8c,0x32,0x04,0x55,0x9e,0x8c,0x05,0xf2,0xf2,0x78,0x95,0xea,0x4d,0x7e,0x93,0xfa,0x87,
  0x88,0xfd,0xff,0x47,0x24,0x71,0x01,0x59,0xaf,0x47,0xa4,0x6b,0x48,0xf2,0xf7,0xd5,0xd2,0x0d,0x7e,0xd3,0xf8,0xc9,0xa8,0x36,
  0xd0,0x15,0xf9,0x28,0xfc,0x81,0x86,0x25,0xfa,0x00,0x05,0x43,0xe6,0x77,0x38,0x5e,0x6f,0xc2,0x12,0x42,0x70,0xb9,0x09,0x64,
  0x10,0x6a,0x46,0x5d,0x3e,0xe3,0x96,0xcf,0x86,0xc2,0x07,0x15,0xc3,0xd8,0xe2,0x53,0x6b,0x0b,0x7d,0x32,0x02,0x47,0x02,0xed,
  0xbe,0x22,0xbb,0x07,0xb2,0xf1,0x8b,0x07,0xea,0x04,0xa8,0x11,0x39,0xf6,0xf0,0x1a,0xb1,0x80,0xfb,0xf4,0x29,0xfb,0x36,0xa7,
  0x0e,0x81,0xfa,0x51,0xbd,0xb1,0x86,0x95,0x77,0xfa,0x26,0x50,0x20,0x15,0xdf,0x37,0x2c,0x67,0x60,0x07,0xa6,0xf0,0x2a,0x30,
  0x8d,0x2a,0x86,0xcd,0x01,0x4b,0x62,0x0f,0x39,0xf4,0xb2,0xeb,0xbd,0xb3,0xcc,0xf7,0x7b,0xd0,0x8d,0x65,0x76,0xbb,0xdd,0xdc,
  0xe6,0xf7,0xe9,0x93,0x2a,0xcf,0x6e,0x81,0x51,0xb9,0xbe,0x11,0x56,0x2f,0xbb,0xc0,0xcf,0x95,0x4b,0xbc,0x0f,0x9e,0x7c,0x31,
  0x4b,0x30,0x8a,0xa3,0x0a,0xe8,0x24,0xae,0x15,0xca,0x1e,0x5b,0xe0,0x53,0x02,0xa8,0x68,0x90,0xdf,0x14,0x46,0x8b,0x02,0x3a,
  0xc6,0xa1,0x68,0xd0,0x77,0x61,0x76,0xbf,0xfb,0xee,0xb2,0x23,0xd4,0xe2,0x74,0x2f,0x0f,0x0f,0x0d,0x63,0x1e,0xf1,0xe6,0xde,
  0xc2,0xe1,0xd1,0x2b,0x96,0x59,0x7c,0x83,0xdc,0x62,0xc6,0xa6,0xd7,0xb0,0xa6,0x9b,0x06,0xea,0xca,0xf8,0x5d,0x7a,0x9e,0x05,
  0x4f,0xe6,0xf1,0xc4,0x58,0xdc,0x1b,0xa2,0x3f,0xdb,0x1b,0x39,0xce,0xb0,0x07,0x7c,0xa9,0x9c,0xb2,0x8b,0x3b,0x40,0x67,0x5a,
  0xa6,0x03,0xaf,0x11,0xee,0xaa,0x8b,0x5b,0x91,0x53,0x2d,0x3b,0x2e,0x46,0x56,0x71,0x58,0x10,0x24,0x6a,0x16,0xda,0xd5,0xb5,
  0x58,0x0e,0x28,0x0e,0xb8,0x7d,0x1c,0x8b,0xa4,0x64,0xaa,0xc3,0x29,0x3e,0x3c,0x39,0xed,0xa9,0x56,0x7c,0x4a,0xa5,0x63,0xf8,
  0x02,0xc5,0xc7,0x51,0xad,0x60,0x8a,0x87,0x47,0xa0,0xc4,0xc3,0x12,0xe5,0xc3,0x24,0x44,0xe1,0x37,0xd8,0x3d,0x80,0xb4,0x4b,
  0xd0,0xae,0x1d,0xc3,0xcc,0xcd,0x37,0x52,0x5f,0x36,0x8d,0xad,0xb0,0xde,0x16,0x06,0xf4,0x31,0x2b,0xad,0x04,0xf7,0x51,0x80,
  0x3e,0xd7,0x1d,0xbe,0x50,0x93,0x3b,0x55,0x97,0x13,0x21,0x94,0x58,0x48,0xda,0x8b,0x7a,0x73,0x26,0x2e,0xf0,0xb2,0x2d,0xd6,
  0x53,0x53,0x45,0xd5,0x06,0xad,0x6b,0x78,0xb9,0xc5,0xce,0xe3,0x32,0x65,0xb7,0x53,0xe1,0x69,0x5c,0x88,0x0e,0x00,0x2a,0x7a,
  0x12,0x17,0x29,0xd3,0xbb,0x1c,0x54,0x4c,0x33,0xc8,0x80,0x4a,0x4e,0x87,0x77,0x0a,0xb6,0xf7,0x9f,0x3e,0x19,0x25,0x93,0xd5,
  0xce,0x4f,0xe6,0xe6,0x4b,0xef,0xce,0xc4,0x84,0x5b,0xe8,0xf5,0x39,0x68,0x1e,0x1a,0xaf,0xdc,0xc1,0x98,0x80,0x4b,0xbf,0x02,
  0xa8,0x23,0x1f,0x35,0x98,0x7c,0xd9,0x53,0x98,0x25,0xa3,0x6b,0x39,0x87,0x79,0x62,0xc5,0x77,0xa7,0xa0,0x3a,0xc2,0x00,0x87,
  0x06,0xdd,0xb8,0xda,0xa1,0xb1,0xe9,0x85,0xc2,0xb6,0x9e,0x94,0x88,0x96,0x73,0xab,0x1d,0x9d,0x18,0x0a,0xe1,0x58,0x92,0xc3,
  0x68,0xcc,0x49,0xa9,0xa0,0xd9,0xec,0xc5,0xf2,0x17,0x8c,0x6f,0x67,0x24,0x50,0x04,0x2f,0x67,0x7b,0x72,0x2c,0xe7,0xf8,0x1e,
  0x43,0xb6,0xea,0x84,0xb4,0x3b,0x99,0xa8,0xb0,0xad,0xb1,0x37,0xb0,0x05,0x97,0xa1,0xee,0x59,0xa1,0x21,0xab,0x7b,0xf4,0xd1,
  0x05,0x7a,0x8c,0xca,0xb1,0xbf,0xda,0x83,0x66,0xb3,0x9a,0xdb,0x15,0xf0,0x4d,0xbc,0x2b,0x7c,0xe8,0xde,0xcc,0x8b,0xa5,0xf7,
  0x8a,0x72,0xf2,0x03,0xca,0xe8,0x6e,0x91,0x18,0xac,0xc4,0x72,0xf0,0xb0,0xd5,0x69,0x56,0x3b,0x15,0xad,0x12,0xc9,0xe4,0x43,
  0xd0,0x88,0x2a,0xa1,0x78,0x84,0xd7,0xda,0x7b,0x15,0xdd,0x03,0x41,0x9e,0x14,0x51,0xe4,0x5e,0x95,0xf0,0x11,0xaa,0xe4,0x58,
  0xd8,0x3b,0x79,0x71,0xf2,0xe8,0xdc,0xa8,0x1e,0x6e,0x86,0xdd,0x44,0xd2,0xb6,0x5a,0x9d,0x2b,0xf8,0xf1,0xf8,0xdc,0x23,0xb5,
  0x0c,0xdd,0x0f,0xc9,0x16,0xd6,0x00,0xb6,0x99,0x54,0xaa,0xdf,0x75,0xbb,0xd9,0xad,0xac,0x16,0x45,0xab,0x54,0xab,0xae,0xd6,
  0xc1,0xa7,0x4f,0xd0,0x03,0xed,0x61,0xf4,0x2d,0xda,0xc4,0x6a,0xe3,0xee,0x8d,0x11,0x2e,0x59,0x9d,0xd4,0xf8,0x8e,0xa1,0xc7,
  0xab,0x71,0x4b,0x35,0xe6,0x35,0x15,0xff,0x58,0x8c,0xce,0x74,0x94,0xcc,0xa0,0x1d,0x28,0x0d,0x4b,0xb4,0x26,0xd3,0xee,0x54,
  0xba,0x93,0xa9,0x5f,0x31,0xa2,0x63,0x8c,0x51,0xb8,0x2f,0x8a,0xf4,0x75,0x54,0xf3,0x29,0xe0,0xc8,0x09,0x6c,0xbb,0x7a,0xa3,
  0x06,0x4f,0x13,0x97,0x16,0x86,0x53,0xb1,0x37,0x22,0xe7,0xfe,0x48,0xf4,0xa5,0x8b,0xa7,0x11,0x1b,0xc6,0x9e,0x14,0x7e,0x20,
  0x9d,0xf9,0xb8,0x71,0x14,0xf8,0x63,0x30,0x2c,0x3e,0xd2,0x8c,0xba,0x06,0x1e,0x7d,0x18,0x00,0x0f,0xf5,0x7d,0x97,0x57,0xb2,
  0x18,0xdc,0x84,0xf9,0x6f,0x4e,0xa3,0x05,0x90,0x05,0x6a,0x07,0xbc,0x19,0x5a,0xa0,0x43,0xde,0x4c,0x04,0x74,0x6b,0x76,0x8c,
  0xd3,0xd7,0xbd,0x73,0xa3,0x86,0x57,0xfd,0x0b,0xe9,0x75,0xc6,0x35,0xbc,0xee,0xbf,0xf3,0x97,0xde,0xeb,0x57,0x0d,0x8f,0x14,
  0x6c,0x6b,0x78,0x5d,0xf9,0x80,0x4a,0x2e,0x32,0x19,0x47,0x92,0xf5,0xe5,0xf5,0x0d,0x0f,0xbb,0x8e,0xb4,0x96,0x39,0x20,0x1c,
  0xbd,0x27,0xd5,0x9b,0xf9,0x5a,0x6c,0x26,0x1b,0xee,0xc5,0xa1,0xf1,0x34,0xb9,0x76,0x8a,0x7d,0xf9,0xeb,0xbf,0xc2,0x24,0xa2,
  0xd3,0xbc,0x0e,0x00,0x3f,0xb6,0x29,0x0f,0xc8,0xc6,0x83,0xfe,0x06,0xe2,0xf6,0x3b,0x6c,0x55,0x88,0x58,0x19,0xfe,0x46,0x0a,
  0x20,0x7f,0xa7,0xd9,0x3a,0x2c,0xc6,0x73,0xba,0xc7,0x0e,0xde,0x7f,0x04,0x95,0x2c,0xf4,0xe4,0xea,0xa1,0x35,0x8a,0xe4,0x0f,
  0xb9,0x0d,0x15,0x1b,0x08,0xd1,0xf2,0xae,0x92,0x55,0x5b,0x48,0x3f,0x93,0xee,0xbb,0xf7,0x7b,0x93,0xc6,0x34,0xf0,0xc6,0x15,
  0x1e,0x52,0x34,0x46,0x58,0x43,0x32,0x07,0x19,0x99,0x44,0xaf,0x19,0x26,0x7f,0xd9,0x23,0x4a,0x19,0x60,0x23,0x11,0x05,0x69,
  0x01,0x16,0xbd,0x52,0x2a,0x80,0x0b,0xb4,0x17,0x76,0x8e,0xbd,0x47,0x54,0x11,0xf3,0x10,0x4f,0x38,0x47,0x1b,0xb4,0x0a,0xa3,
  0xea,0xf3,0x5e,0x3c,0xac,0x5e,0x2b,0x3b,0x2e,0x4c,0x99,0x37,0xe8,0xf0,0x9d,0x84,0x0d,0x1f,0x8c,0x58,0x09,0x3d,0x87,0xc0,
  0x18,0xaf,0x44,0x40,0x2f,0x30,0xdf,0x60,0x26,0x1c,0x8c,0x2b,0x24,0x11,0x51,0x68,0x5c,0xb0,0x94,0x93,0xd0,0x88,0x63,0x46,
  0x75,0x4e,0x7d,0x67,0xa7,0x53,0xcd,0xe9,0xc9,0x79,0x21,0xb3,0x58,0xe6,0x87,0x0a,0x71,0xa8,0xbe,0x77,0xcb,0xb6,0xd7,0x44,
  0x65,0x5e,0xa5,0x76,0xac,0x74,0x27,0x95,0xe7,0x77,0x16,0xd6,0x4e,0x79,0x36,0xaa,0xf8,0x93,0x22,0xdd,0x76,0xb3,0xb9,0x07,
  0xbb,0x40,0xec,0x91,0x04,0x85,0x7a,0x05,0x97,0xe4,0x09,0x1e,0x08,0x79,0x61,0xe1,0xe1,0x0f,0x01,0x2f,0xd4,0xf6,0x67,0xd4,
  0xc2,0x6d,0xb0,0xd0,0x6b,0xfa,0xff,0x0d,0xd2,0xff,0x2b,0x0c,0xd2,0xa2,0xe5,0xa5,0x2c,0x92,0x64,0x75,0xf7,0xb2,0x7a,0x46,
  0xf2,0x6b,0x02,0x40,0x3e,0x35,0xd2,0x80,0xbe,0xca,0x16,0xad,0xb9,0xa8,0x70,0x77,0x95,0x4d,0xa7,0xd1,0xc3,0x61,0x9a,0x1e,
  0x3a,0x29,0x5a,0xb9,0xec,0x2a,0x43,0x03,0x45,0xc2,0x4b,0xd0,0xd8,0x1b,0xbc,0xef,0x55,0x2e,0xeb,0x25,0x93,0xa4,0x51,0x42,
  0x7e,0xa9,0xee,0xb7,0xee,0x57,0x6f,0x16,0x5b,0x37,0x05,0x3f,0x26,0x90,0x33,0xb2,0x92,0xdf,0x0b,0x50,0x3f,0x0c,0xc0,0x3e,
  0x06,0xcc,0xa4,0xbc,0x82,0x8f,0x81,0xc7,0x51,0x9b,0x6b,0xd0,0xaf,0x74,0x44,0x17,0x92,0xa3,0x28,0xc7,0x0b,0xab,0xe8,0x98,
  0xaf,0x64,0xb1,0x60,0xd3,0x24,0xfa,0x2a,0x4e,0x80,0xee,0xe5,0xde,0x72,0x65,0x73,0x95,0x09,0x44,0xba,0x05,0xfd,0x96,0x87,
  0xfa,0xa9,0x0a,0x63,0xf3,0x12,0xd4,0x65,0xfc,0x01,0x08,0x63,0x93,0x06,0x81,0x27,0x4d,0x3b,0x05,0x40,0x63,0x8d,0xb7,0x44,
  0xdc,0xa4,0x7f,0x43,0x03,0x86,0x8d,0x72,0x6e,0x2a,0xd5,0xee,0x81,0x46,0x34,0x69,0xde,0x57,0x3f,0xa9,0x61,0x94,0x4c,0x2a,
  0xfb,0xeb,0x15,0xe5,0x3d,0xeb,0x82,0x04,0x7f,0xce,0x02,0xfd,0x74,0xcb,0x8c,0x43,0xad,0x47,0x45,0xeb,0xa2,0x7b,0x70,0x83,
  0x0a,0xa9,0x1c,0x09,0xbf,0x61,0x5a,0x1e,0x79,0x1c,0xbb,0xbe,0x0c,0xc4,0x5e,0x9e,0xc0,0xa3,0x4e,0x32,0x6a,0x0f,0x68,0x35,
  0xf9,0x2e,0x70,0xcb,0x17,0xf3,0x72,0x7b,0x2d,0x3c,0xab,0x93,0x99,0x66,0x79,0x13,0xa8,0x4c,0xa9,0x2b,0xc8,0xc5,0x0d,0x9f,
  0xda,0x57,0x0c,0xfc,0x49,0xb0,0xd2,0xe9,0x8f,0xa6,0x5e,0x66,0x94,0x98,0x8f,0xcb,0xda,0xe8,0x9a,0xec,0x77,0x0e,0xbf,0xb4,
  0x46,0x1c,0xde,0x35,0x46,0xc2,0x8d,0x92,0xab,0x40,0x7f,0x4a,0x13,0xdd,0x63,0xbc,0xee,0x28,0xb9,0xa8,0x89,0x72,0x18,0x3d,
  0xff,0xf6,0x33,0x9e,0x52,0xbb,0x20,0x33,0x2c,0xba,0xbb,0x89,0xf7,0xf1,0x7a,0x27,0x91,0x30,0x47,0x56,0xbb,0x8d,0xae,0x9a,
  0xa2,0xb4,0x57,0x3c,0x11,0xea,0xfb,0x78,0x15,0x29,0x59,0x50,0x85,0xc0,0xc0,0x77,0xff,0x91,0xda,0xce,0x4f,0x5d,0xe5,0x94,
  0x07,0x0d,0xfa,0xe0,0x66,0x19,0x55,0x60,0xe0,0x24,0x62,0xbe,0x69,0x63,0x80,0x97,0x44,0x81,0xe8,0x81,0x2e,0xfd,0xc0,0x84,
  0x95,0x75,0x9f,0x58,0x57,0xc0,0x11,0xbb,0xd5,0xa5,0xbe,0x07,0x0c,0xba,0xe4,0x3b,0x02,0x63,0x3e,0xd7,0x13,0x22,0x74,0x79,
  0x6f,0x61,0x84,0x27,0x92,0x69,0xeb,0x36,0xe8,0x1a,0x4f,0x4f,0x7b,0xf5,0xde,0xf9,0xd1,0xab,0xc7,0xaf,0xcf,0xce,0x8d,0xa5,
  0xe0,0x87,0x21,0xa6,0xb8,0x75,0x53,0x13,0x07,0x7b,0x99,0xd5,0xa1,0x9e,0xa3,0x15,0xd2,0x0d,0x5c,0x4c,0x41,0xd3,0x2e,0x80,
  0x05,0xc3,0xba,0x06,0x4c,0x96,0x6f,0xcd,0x1c,0x12,0xa5,0x20,0x25,0x87,0xb7,0x9f,0x47,0x7d,0x2e,0xd1,0xa4,0x17,0x0d,0x20,
  0x71,0xbc,0x1e,0xaf,0x76,0x23,0xc8,0xfb,0x8f,0xe7,0xe5,0x8e,0x06,0xa0,0xa3,0xf1,0xc1,0x75,0x07,0xd9,0xb2,0xe6,0x2b,0xf3,
  0xb8,0xd3,0x02,0x0d,0xa7,0x59,0x9b,0xf0,0x2b,0x6b,0x12,0x4c,0x8e,0x46,0xa2,0xb3,0x8d,0x17,0x9a,0x35,0xe7,0xd5,0x79,0x6e,
  0x1f,0x1b,0x5a,0xb0,0x65,0x86,0xb8,0xd1,0xec,0xe6,0x75,0xd1,0x19,0x6a,0x83,0xb5,0xd5,0x79,0xe6,0x43,0x43,0xa5,0x5c,0xee,
  0xb7,0xab,0x8a,0xc6,0xc9,0xea,0x51,0xe3,0xff,0x92,0xde,0x41,0xc7,0xbe,0x3f,0xf5,0x3a,0x5b,0x5b,0x40,0xce,0x03,0x17,0x1d,
  0x1d,0x75,0x90,0x38,0xf4,0x5b,0x7f,0xf5,0x09,0x5d,0x31,0x3c,0x70,0x27,0x5b,0x97,0xad,0x2d,0x8f,0xee,0xab,0x3b,0x1c,0xe0,
  0x9d,0x58,0xdd,0xd6,0x3d,0xfc,0xe9,0xe0,0x00,0xf0,0xd5,0x35,0xc5,0x3d,0x3a,0xe3,0xe1,0x77,0x71,0xd3,0xbd,0x47,0x3f,0x13,
  0x0c,0xf8,0x74,0xf0,0xc4,0xe5,0x9b,0xb3,0xe7,0x18,0x15,0xc7,0xeb,0x42,0x7d,0xb0,0xc2,0x8a,0xbc,0xc5,0x00,0xec,0x2f,0xa8,
  0x77,0x07,0xb6,0xef,0xdd,0xbb,0x17,0x7f,0x7d,0xd7,0x7c,0x9f,0xd8,0xac,0x7a,0xe9,0xde,0x7a,0x3c,0x15,0x31,0xd3,0x9a,0x1c,
  0x14,0xb3,0xce,0xde,0xda,0xa4,0x3f,0x6d,0x20,0x0a,0xfe,0x54,0x92,0xc7,0x2b,0x02,0x47,0x62,0x48,0xe6,0x06,0x12,0xab,0x1a,
  0x62,0xb3,0x32,0x55,0x06,0x03,0x18,0x8a,0x35,0x2a,0x55,0x4f,0x1d,0x03,0x4c,0x0e,0xd8,0xc5,0x05,0x2b,0xe8,0x46,0xd1,0x7e,
  0xd4,0x19,0xf0,0x48,0x6c,0xf8,0xe6,0x2b,0x7b,0x74,0x2d,0x21,0x9b,0xb8,0x38,0x0f,0xee,0x84,0x6d,0xb5,0x24,0x70,0x63,0xbe,
  0x58,0xaf,0x48,0x5f,0x73,0xa8,0xed,0x01,0x3a,0x4f,0x94,0x6c,0x1c,0xe9,0xdf,0x0b,0xd1,0xda,0xab,0x8d,0x13,0x43,0xc1,0xd0,
  0xec,0xb1,0x18,0x72,0x20,0x8c,0x32,0x85,0x25,0xeb,0xe2,0x8f,0x30,0xbe,0xb0,0x41,0x36,0x56,0x10,0x36,0xd0,0x14,0x94,0x12,
  0xeb,0x29,0xfe,0x29,0x84,0x6f,0x80,0x38,0x15,0x7c,0xf8,0x13,0xa0,0xbd,0xb3,0xd4,0xf3,0x2a,0xb3,0x1a,0x8f,0xae,0x7c,0x50,
  0xa5,0x43,0x4f,0xdd,0xce,0x07,0xac,0xbd,0xa4,0x37,0x55,0x2f,0x82,0x23,0xa7,0xb1,0x28,0x11,0x80,0xe1,0xa5,0xea,0x12,0x8f,
  0xb0,0xba,0x85,0x6f,0x19,0x60,0xf8,0x9b,0x86,0x58,0xad,0x74,0xa0,0x3b,0x25,0x76,0x74,0x7c,0xd3,0x5b,0xc9,0x48,0x2a,0xcf,
  0xe0,0x10,0xec,0x88,0x92,0xe9,0xa7,0xe2,0xff,0x5f,0x3f,0xfd,0x23,0xdb,0xae,0x18,0xef,0x92,0x5b,0x12,0xde,0x43,0x5f,0x91,
  0x2d,0xdd,0xef,0x1e,0xf4,0x17,0x02,0x49,0xb5,0x0f,0xc9,0xe9,0x69,0x6c,0xf6,0x1b,0xd8,0x03,0x40,0xd2,0xa0,0xe2,0x1c,0x1c,
  0x65,0xee,0xe4,0xe4,0x9e,0x85,0x12,0x94,0x0c,0xc2,0x1a,0x79,0x95,0xb4,0xb4,0x63,0xba,0x0f,0xa0,0xa4,0x5b,0x72,0xac,0x1f,
  0x86,0x32,0xb1,0x68,0x17,0x29,0xf7,0xe3,0x93,0x0f,0x35,0xd2,0x5a,0x36,0x8d,0x7b,0x38,0x64,0xb7,0x65,0x7c,0x1d,0x15,0x46,
  0xd7,0x17,0x2c,0x03,0x77,0x0b,0x2a,0x2e,0xa1,0x3f,0x74,0x3e,0x82,0x74,0xc5,0x94,0x78,0x0c,0xf5,0xfb,0xa2,0x7b,0x13,0xe6,
  0xae,0x74,0xde,0xbd,0x9f,0xef,0x15,0x04,0x5f,0x4f,0xc2,0xda,0xa0,0x36,0xa4,0x1b,0xe6,0xad,0x83,0xe8,0x7d,0x61,0x34,0x76,
  0xf1,0xec,0xb4,0x6b,0xd8,0x50,0xb7,0x8f,0xe3,0x8e,0xa9,0xe1,0x1a,0x7a,0xb5,0x25,0x9d,0x61,0xfc,0x20,0x16,0x58,0x61,0x7a,
  0x43,0x41,0x67,0x98,0x18,0x92,0xcb,0x79,0x20,0xcf,0xef,0xe2,0x36,0x2a,0xa2,0x92,0x6b,0xb5,0x04,0x20,0xbc,0x9a,0x2d,0x06,
  0xa8,0xa0,0x57,0x7c,0xbf,0x42,0x17,0xd9,0x80,0xfa,0xda,0x3d,0x65,0x6e,0x0a,0x2b,0xc1,0x76,0xa6,0xe6,0xe2,0x2e,0x8b,0x6e,
  0x03,0x2b,0xe9,0xb7,0xa8,0xfa,0x8a,0x9d,0x2f,0x40,0x60,0xaa,0x4e,0x49,0x7c,0x4f,0x13,0x67,0x30,0x31,0x5d,0x98,0xc1,0xb6,
  0xa8,0x87,0xbb,0x8b,0x96,0xfd,0x6d,0x78,0x41,0xde,0xbd,0x4a,0x6b,0x7f,0x1f,0x74,0xf4,0x48,0xa6,0x41,0x19,0x26,0x2c,0x22,
  0x77,0x27,0x77,0x3f,0xa2,0xe7,0x20,0x89,0x54,0x65,0x5e,0xc5,0xba,0x64,0x66,0x16,0x8a,0x09,0x0f,0x1b,0xef,0x16,0xcb,0xf5,
  0xf4,0xc5,0x9b,0x11,0x3a,0x4a,0x14,0xd1,0xf4,0x95,0x94,0x89,0x62,0x78,0x88,0x5f,0x2c,0xf3,0x50,0xa9,0x6f,0x9d,0x32,0x2f,
  0x6c,0xf6,0xea,0xc6,0x0c,0x05,0xc6,0x5d,0xe9,0xa1,0x87,0x44,0x33,0x4c,0xe2,0x0f,0x74,0x9b,0x28,0x45,0x02,0x6c,0x21,0x50,
  0x61,0x5b,0x7d,0x96,0x20,0xf2,0x54,0x18,0x2b,0x8d,0xca,0x92,0xbd,0x34,0x7d,0x73,0x66,0xd6,0xe3,0xa1,0xdb,0xfe,0x30,0x42,
  0x77,0x4d,0x7c,0xd7,0xc8,0xb0,0x58,0x13,0xe7,0x05,0xae,0x14,0xac,0x58,0x0f,0xf7,0x73,0x4e,0xb4,0xd2,0x45,0xb8,0xef,0x11,
  0x4c,0xc6,0x26,0x7e,0xc0,0xbe,0xb1,0xd8,0x8a,0xc1,0x37,0xd5,0xfc,0xa6,0xa7,0x06,0x4a,0x0b,0xee,0xaf,0x5e,0x5e,0x23,0xbc,
  0xdc,0x73,0x94,0x89,0x2b,0x95,0x28,0x9e,0xe9,0x0b,0x46,0x4b,0x76,0xac,0x82,0xf9,0x63,0xdb,0x68,0xfe,0xeb,0xae,0x4a,0xf1,
  0xae,0x87,0x83,0xaa,0x0c,0xa2,0x12,0x24,0x64,0xaf,0x38,0x2d,0x24,0x98,0x95,0x16,0x50,0xf5,0xf4,0x67,0x4d,0xa1,0x70,0x2d,
  0xcb,0xb4,0x68,0x7d,0xaf,0x71,0x1d,0xf2,0x6d,0x77,0xcb,0xf2,0xb0,0x17,0x6f,0x2c,0x91,0x33,0x4f,0x11,0x6f,0x29,0xa3,0x25,
  0x17,0x23,0x96,0x30,0xda,0xbb,0x71,0x6d,0xf2,0xbe,0xbb,0xfa,0xae,0xdd,0xf0,0xa6,0xb6,0xe5,0x57,0x60,0x2f,0xae,0x52,0xaa,
  0xe2,0x2b,0x0a,0xa6,0xab,0xd8,0x29,0xca,0xe1,0x6e,0xf3,0x6b,0xa4,0x3c,0x66,0x73,0xc5,0x92,0xbe,0x8a,0xfd,0x7c,0xea,0xe6,
  0xe4,0xf9,0x3c,0x4a,0xaf,0xfa,0xd0,0xbd,0xd1,0x75,0x8e,0xce,0x9a,0x0a,0x0c,0x26,0x0c,0xd4,0x62,0x3d,0xa3,0x33,0xae,0x69,
  0xfa,0x43,0x67,0x52,0x8b,0x71,0xdf,0xd9,0x5c,0x5d,0x77,0xa8,0xa5,0x36,0xa3,0x0e,0xfe,0xa9,0x65,0x76,0xea,0xce,0xfa,0x9b,
  0x3f,0x41,0x5a,0xb4,0x31,0x77,0xbe,0x72,0xd7,0xcf,0x77,0xd8,0x59,0x6f,0x8b,0x9f,0xef,0x2d,0x0e,0xbc,0x27,0xd7,0x67,0x2e,
  0x0c,0xbe,0xaf,0x92,0xca,0xb0,0x30,0x3c,0xbf,0x78,0x35,0x32,0xf7,0x76,0x7e,0x6b,0x04,0x7e,0x21,0x27,0x6b,0x34,0x4c,0x49,
  0x4b,0xab,0xda,0x5d,0x58,0xf9,0x30,0xdc,0x30,0x12,0xb3,0x0b,0x4b,0xcb,0x6c,0x82,0xd0,0x28,0x98,0x79,0x94,0x66,0x91,0x24,
  0xfe,0xcc,0x3c,0xbc,0x91,0x1f,0x14,0x16,0x74,0xd8,0xe1,0x5b,0x31,0x63,0x6f,0x45,0xbf,0xe7,0xc2,0x0a,0x03,0x67,0xce,0xd0,
  0x55,0x67,0x6c,0x26,0x47,0x8c,0x5d,0xcf,0x27,0x67,0x90,0xd1,0x79,0xd8,0xda,0x02,0x8d,0x78,0xe6,0x11,0xa0,0xae,0x27,0x94,
  0xb7,0x3d,0x1a,0x40,0xcb,0xef,0x51,0x43,0xd4,0xd0,0x6d,0x09,0x72,0x8d,0x1a,0x08,0x29,0x5d,0xa9,0x1a,0xa8,0x71,0x1b,0xd4,
  0x45,0x2a,0x31,0x62,0x9e,0x7c,0x0f,0x3b,0x9d,0xcf,0x23,0x68,0xb5,0x14,0x62,0x0f,0x24,0xe6,0xa3,0x89,0x59,0x19,0xd4,0xa6,
  0x94,0xc7,0x39,0xf3,0xee,0xdd,0x9b,0x61,0x26,0x1d,0x37,0xaf,0x95,0x05,0xd3,0xed,0xb6,0xaa,0x50,0x82,0x15,0x2b,0xc6,0xe0,
  0x93,0xb1,0x39,0xd8,0x34,0x3e,0x61,0xde,0xc7,0x1e,0x79,0xad,0x32,0xe9,0x1e,0x78,0x19,0xd2,0x21,0xde,0xa4,0x43,0xf5,0xee,
  0x85,0x77,0xe8,0x40,0xf5,0x1c,0x76,0xe7,0xcb,0x17,0x16,0xfa,0xc9,0xac,0xeb,0x0d,0x2e,0xec,0xd4,0xa5,0xdf,0x16,0xc3,0xab,
  0x7a,0x16,0x79,0x6b,0xa2,0x69,0x25,0x2b,0x0c,0x7d,0xd5,0x92,0x27,0x05,0xd7,0xa7,0x4f,0xad,0xea,0x7c,0xae,0x3b,0x1c,0xf2,
  0xc1,0xcf,0x0b,0x71,0x8d,0x03,0x19,0xb5,0x50,0x34,0x7e,0xb7,0x46,0xdc,0x05,0xf1,0xc1,0x2d,0xc7,0x8b,0x22,0x2f,0x9f,0x3e,
  0x6d,0x3d,0x7f,0x75,0xfa,0xe6,0xfc,0x93,0x4a,0x67,0xfa,0x74,0x7e,0xf2,0x9f,0xcf,0x8f,0xce,0x4e,0x8e,0xb6,0x1a,0x78,0xd6,
  0xbb,0x52,0x9c,0x82,0x1a,0xa5,0x41,0x55,0x23,0xdf,0x70,0x94,0xdb,0x71,0x73,0x04,0x64,0x30,0x7b,0x33,0xed,0x18,0xc1,0xd4,
  0xa8,0xd1,0xc3,0x63,0x80,0xb4,0x63,0x28,0x78,0xa9,0xe0,0x85,0x18,0xfa,0x1d,0x03,0x2f,0x93,0x0a,0x0b,0xce,0x30,0x18,0xd6,
  0x31,0x54,0xdc,0xac,0x66,0x30,0xe0,0x3d,0xbc,0x64,0xc9,0xa8,0xc9,0x8e,0x3a,0x43,0x20,0x8c,0xda,0x09,0x62,0x17,0xba,0x91,
  0xee,0xd4,0x98,0xbf,0x13,0x0d,0x40,0x01,0x25,0xe7,0x4e,0xaa,0x65,0xa8,0x9e,0xd4,0xb2,0xc8,0xcc,0x69,0x3d,0xea,0x90,0x7e,
  0x89,0xba,0x03,0x35,0xea,0x3e,0x1d,0xee,0xcc,0x6d,0xf3,0x25,0x31,0xbb,0xf0,0xce,0x80,0x32,0xc3,0x1f,0xaa,0x2c,0xec,0x38,
  0x63,0xce,0xab,0x60,0x59,0x74,0xbe,0x02,0xb3,0x95,0xe3,0x0c,0xb9,0xcd,0x55,0xe2,0xb2,0x05,0x5a,0x6c,0xf4,0xcb,0xd0,0x87,
  0xea,0x98,0x1a,0xaa,0xaa,0x65,0x7a,0x0c,0x5e,0xc8,0x52,0x1a,0x58,0xcd,0xff,0xc6,0xfb,0x72,0xff,0x65,0x66,0x5a,0xf9,0xc3,
  0x5c,0x65,0x8e,0xd2,0xfc,0x4f,0x5b,0xaf,0x3f,0x60,0xee,0xa8,0x58,0x41,0x04,0x26,0x99,0xfd,0x4d,0x12,0x01,0x31,0x0b,0xf6,
  0x37,0x53,0xdd,0x59,0xe3,0x5b,0x03,0x6f,0x3d,0x3f,0x0a,0x36,0xcc,0xea,0xef,0x4f,0xde,0x82,0x2d,0x66,0x36,0x2e,0x61,0x57,
  0x04,0x20,0xa2,0x64,0x64,0xb3,0xd1,0x77,0xb9,0x34,0xa3,0x04,0x66,0x4a,0x45,0x36,0x53,0xa9,0xc8,0xa0,0xa6,0xc4,0xe5,0x13,
  0xf5,0x3d,0x7a,0xf5,0x86,0x32,0x94,0xe9,0x4d,0x3a,0x59,0x19,0x53,0x9b,0xb1,0x54,0x25,0x3a,0x43,0x01,0x5e,0x58,0xf1,0x44,
  0xd2,0xa5,0x8b,0x58,0x0e,0x1c,0xa0,0x9e,0xa2,0xe4,0x60,0x95,0xdb,0x6c,0xaa,0xdc,0x66,0x55,0x78,0xec,0xba,0xbe,0x17,0x42,
  0x08,0xdf,0xa2,0xd4,0xa5,0x4f,0x98,0xbc,0x14,0x0b,0xfd,0xb5,0x30,0x10,0x5f,0x01,0x54,0x14,0x34,0x58,0x4c,0x87,0xda,0x35,
  0x42,0x1a,0x3d,0x44,0x8b,0xb8,0x78,0x11,0xd2,0xd7,0x08,0x95,0x68,0xc4,0x5a,0x02,0x64,0x2a,0xed,0x51,0x1d,0x63,0x3d,0xa1,
  0x6e,0xb2,0x29,0x90,0x29,0x29,0x59,0xa4,0x1b,0x79,0xe1,0xb8,0x5b,0x0a,0x0c,0x10,0x09,0xb1,0x4a,0x94,0xca,0x7e,0xec,0xac,
  0x96,0xfd,0x38,0xaf,0x6a,0x59,0x82,0xdc,0x06,0x9d,0xa6,0x62,0x28,0xc8,0x72,0x09,0x7b,0xd5,0x28,0x84,0xad,0x80,0xe3,0x89,
  0xae,0x3f,0x90,0x78,0x9a,0x3e,0x94,0xf8,0x15,0x83,0x43,0x55,0xae,0x2e,0x24,0x79,0x73,0xf6,0x22,0x7c,0xfb,0xba,0xff,0x0b,
  0x20,0x10,0x9e,0x2b,0x51,0x36,0x64,0xdf,0x76,0xfb,0x48,0xed,0xbc,0x81,0x22,0x1f,0xf1,0x1e,0x1f,0x6e,0xaa,0xef,0x34,0x9a,
  0x8d,0x66,0x3d,0x9a,0x2b,0xde,0xad,0x64,0x40,0x3d,0x42,0x33,0x30,0x27,0xf6,0x0a,0x0c,0xeb,0x5e,0xe8,0xbd,0xd2,0x80,0x65,
  0x62,0x20,0x7d,0xed,0x52,0xc9,0xba,0x0d,0xbb,0x4b,0xfa,0xc0,0x5b,0xa0,0x70,0x67,0x87,0x0f,0x0a,0x13,0x22,0x06,0x87,0xe9,
  0xa5,0x2b,0x5f,0xfb,0xe7,0x93,0x6f,0x5b,0x7b,0x05,0xc6,0x62,0xe5,0xf8,0x2b,0x28,0xa1,0x56,0xa6,0x50,0xeb,0x77,0x59,0x45,
  0x8a,0x75,0x08,0x5e,0x83,0x4b,0xc9,0xaf,0x8f,0x03,0x90,0x8a,0xb2,0x82,0xca,0xb5,0x22,0x22,0xa5,0x2a,0xab,0x79,0xa6,0xb2,
  0x24,0xf1,0x67,0xd7,0xe2,0x14,0x47,0x2c,0xa6,0x74,0xc9,0xb0,0x62,0x9e,0xe4,0xca,0x6c,0xe1,0xf8,0x9e,0x9a,0xc2,0xb5,0xc4,
  0x45,0xa1,0x0c,0x5e,0x39,0xa9,0xe8,0x3a,0x3b,0x88,0x2a,0x3a,0xa7,0x32,0xb3,0xe4,0x05,0x26,0x31,0xc5,0x3f,0x6b,0x42,0x41,
  0xfd,0x93,0xf0,0x62,0x1b,0xfc,0x95,0x54,0x04,0x89,0xdb,0xd1,0x1d,0x3c,0x87,0x46,0x75,0x9d,0x25,0x4e,0x2e,0xa8,0x59,0x7b,
  0x99,0x71,0x5e,0xf5,0xf0,0x68,0xe7,0x9f,0xb8,0xc2,0x21,0xaf,0x13,0xab,0x87,0x9c,0xfe,0x38,0xbc,0x9c,0x2b,0xba,0xac,0xcb,
  0x11,0x78,0x2f,0xb1,0xa5,0x7e,0x09,0x17,0xf8,0xf0,0xbf,0xbc,0xeb,0x09,0x69,0x09,0xc7,0x09,0x26,0x13,0x21,0xdf,0x63,0x1e,
  0x03,0xb0,0x22,0x06,0x73,0xd9,0x63,0x9e,0xba,0x3e,0x4a,0xe5,0xaf,0xd0,0x75,0x1b,0xf8,0x9b,0x78,0x74,0x0b,0x0c,0x43,0x97,
  0x1e,0x5e,0xd9,0x4c,0x49,0xaf,0xa4,0x6f,0x87,0xe3,0x3e,0xa1,0x54,0xe1,0x6c,0x12,0x31,0x92,0x86,0x5a,0x9a,0x00,0x8f,0x94,
  0x44,0xe1,0x63,0xb5,0x3e,0xd8,0xc7,0x7c,0x4f,0x39,0x7a,0xf6,0x32,0xbe,0xaf,0x64,0xe3,0x45,0xd3,0x03,0xd3,0x16,0x25,0x28,
  0x33,0x64,0xfb,0xd4,0x5a,0x98,0x2f,0x91,0x2f,0xc7,0xea,0x64,0x95,0x34,0xe3,0x83,0x8f,0x83,0xcb,0xc5,0x7c,0x1f,0x9e,0x06,
  0x36,0xaa,0xb5,0xc1,0x55,0x77,0x70,0x49,0x39,0x38,0xc8,0x2e,0x57,0x30,0x99,0xb6,0x89,0xf2,0x2e,0x93,0x8c,0x21,0x95,0x27,
  0xb8,0x54,0xe5,0xce,0x1c,0x31,0x8e,0x6d,0xeb,0x7c,0x1a,0x05,0x27,0xd3,0xec,0x0d,0x18,0x0e,0x0f,0x8f,0x90,0xdd,0x2a,0x05,
  0x5a,0x9a,0x54,0xbe,0xc8,0x44,0x9b,0x48,0x31,0x26,0x9e,0x95,0xbb,0x6a,0xd0,0x91,0x8c,0x33,0x18,0xbf,0xd2,0xac,0x35,0x6b,
  0xdb,0xdb,0x4d,0x44,0x81,0xca,0x77,0x45,0x03,0xf1,0xba,0xdb,0xdc,0xbb,0xde,0x6f,0xc1,0x9f,0xcd,0xcd,0x6a,0x54,0x78,0x05,
  0x85,0x57,0xfb,0xad,0xd6,0xde,0x15,0x14,0xde,0x60,0x89,0xd5,0xad,0x5c,0x7f,0xdf,0x6a,0x6d,0x5e,0x55,0xbf,0xdf,0xae,0x01,
  0x01,0xbf,0xb3,0xde,0xd7,0x46,0xf8,0xb1,0xd9,0x7a,0x5f,0xeb,0xd3,0x97,0xf6,0xfb,0xda,0xa4,0x4b,0x09,0x8d,0x13,0x7e,0x55,
  0x91,0xb5,0x51,0xad,0x4f,0xc4,0x37,0x39,0x68,0xde,0xbb,0x37,0xd9,0xdf,0x6e,0x56,0x63,0x59,0xbb,0xdd,0xdc,0x9a,0xec,0xc9,
  0xb0,0x32,0xa8,0x02,0xed,0xdd,0xdd,0x1a,0x3d,0x90,0xb6,0x55,0x91,0xdf,0x0f,0x01,0xf6,0xd1,0xc2,0xf7,0x23,0x7a,0xdf,0x5f,
  0xf8,0xbe,0x8f,0xef,0xe7,0x30,0xf7,0xbe,0x18,0x61,0x6a,0xb2,0x3f,0xae,0x10,0x2a,0xb8,0x1c,0x54,0x5a,0xbb,0x9b,0x57,0xdf,
  0x03,0x12,0xe0,0xf3,0xfa,0xfb,0xf6,0x43,0xa0,0x15,0xc0,0xca,0x03,0x7a,0x0d,0xb2,0x5d,0x1d,0xc4,0xed,0xfe,0x0b,0x1e,0xff,
  0xf8,0xa7,0x1b,0x39,0xaf,0xfd,0xd3,0xcd,0x08,0xff,0xf4,0xe7,0xd5,0x7f,0xc1,0x2a,0xde,0x98,0xc3,0xc6,0x75,0x6c,0x07,0xb2,
  0x0b,0x28,0x8b,0x0b,0x28,0x34,0xde,0xd5,0xbb,0x88,0xfa,0xab,0x10,0x1c,0x5a,0xb3,0xa6,0x6e,0xf1,0xea,0x14,0x4a,0x8b,0x59,
  0xdb,0x25,0xe2,0xdc,0xdf,0x0a,0xaf,0x67,0xda,0xdf,0x42,0xb9,0x0b,0x1f,0x63,0x7f,0x62,0x1f,0xfc,0x1f,0x5f,0x22,0x0f,0x33,
  0x65,0x9b,0x00,0x00,
};
const size_t PAGE_GZ_LEN = sizeof(PAGE_GZ);

CRGB fgBase() { return CRGB(cfg.fgR, cfg.fgG, cfg.fgB); }
CRGB bgBase() { return CRGB(cfg.bgR, cfg.bgG, cfg.bgB); }
CRGB minuteBase() { return CRGB(cfg.minuteR, cfg.minuteG, cfg.minuteB); }
String chipSerial(){
  uint64_t mac=ESP.getEfuseMac();
  char value[13];
  snprintf(value,sizeof(value),"%04X%08X",(uint16_t)(mac>>32),(uint32_t)mac);
  return String(value);
}

void initV32Defaults(){
  cfg.v32SettingsVersion=V32_SETTINGS_VERSION;
  cfg.transitionEnabled=1;cfg.transitionType=4;cfg.circadianEnabled=0;
  cfg.rotationEnabled=0;cfg.rotationMinutes=15;cfg.buttonMultiEnabled=0;
  cfg.regionalEnabled=0;cfg.regionalStyle=0;
}
void initFxParamDefaults(){
  cfg.fxParamSettingsVersion=FX_PARAM_SETTINGS_VERSION;
  cfg.foregroundIntensity=180;cfg.foregroundTrail=110;cfg.foregroundSize=50;
  cfg.backgroundIntensity=180;cfg.backgroundTrail=120;cfg.backgroundSize=50;
  cfg.minuteIntensity=180;cfg.minuteTrail=110;cfg.minuteSize=50;
}
void initV4Defaults(){
  cfg.v4SettingsVersion=V4_SETTINGS_VERSION;
  cfg.foregroundPalette=0;cfg.backgroundPalette=0;cfg.minutePalette=0;
  memset(cfg.favoriteFx,0,sizeof(cfg.favoriteFx));

  uint8_t favs[]={FX_SOLID,FX_BREATHE,FX_RAINBOW,FX_GRADIENT,FX_GRADIENT_DYNAMIC,FX_AURORA,FX_OCEAN,FX_PRIDE,FX_PACIFICA,FX_LAKE};
  for(uint8_t e:favs)cfg.favoriteFx[e>>3]|=(1<<(e&7));
  cfg.rotationFavoritesOnly=1;cfg.rotationNoRepeat=4;
  cfg.adaptiveFps=1;cfg.gammaEnabled=1;cfg.ldrAdaptiveEffects=1;cfg.ambientMode=0;cfg.ambientWordLevel=42;
  cfg.calibrationR=255;cfg.calibrationG=255;cfg.calibrationB=255;
  cfg.dayProfileEnabled=1;cfg.quietStartHour=22;cfg.quietEndHour=7;
  cfg.soundReactiveEnabled=0;
}
void defaultSettings() {
  memset(&cfg,0,sizeof(cfg));
  cfg.magic=SETTINGS_MAGIC;cfg.version=SETTINGS_VERSION;cfg.power=1;cfg.brightnessMin=4;cfg.brightnessMax=180;
  cfg.foregroundEffect=FX_SOLID;cfg.backgroundEffect=FX_MATRIX;cfg.legacyEffectSpeed=45;cfg.scrollSpeed=90;cfg.backgroundLevel=42;
  cfg.fgR=0;cfg.fgG=110;cfg.fgB=255;cfg.bgR=0;cfg.bgG=180;cfg.bgB=45;
  cfg.mqttPort=1883;cfg.mqttDiscovery=1;strlcpy(cfg.mqttBase,"wortuhr/uhr",sizeof(cfg.mqttBase));
  cfg.previewEnabled=1;cfg.presetsEnabled=1;cfg.notificationsEnabled=1;cfg.calendarEnabled=1;cfg.gamesEnabled=1;cfg.highScoresEnabled=1;cfg.weatherAnimationsEnabled=1;cfg.roomClimateEnabled=0;
  cfg.weatherSource=0;cfg.weatherLat=WEATHER_LAT;cfg.weatherLon=WEATHER_LON;strlcpy(cfg.weatherLocation,"BERLIN",sizeof(cfg.weatherLocation));
  cfg.minuteSettingsVersion=1;cfg.minuteEffect=FX_SOLID;cfg.minuteLevel=255;cfg.minuteR=0;cfg.minuteG=110;cfg.minuteB=255;
  cfg.flappySettingsVersion=1;cfg.highFlappy=0;
  cfg.speedSettingsVersion=SPEED_SETTINGS_VERSION;cfg.foregroundSpeed=45;cfg.backgroundSpeed=35;cfg.minuteSpeed=45;
  String serial=chipSerial(),suffix=serial.substring(serial.length()-8);
  String ap="WU"+suffix.substring(0,6),admin="WU-"+suffix+"!";
  cfg.finalSettingsVersion=FINAL_SETTINGS_VERSION;strlcpy(cfg.apPass,ap.c_str(),sizeof(cfg.apPass));strlcpy(cfg.adminPass,admin.c_str(),sizeof(cfg.adminPass));strlcpy(cfg.hostName,"wortuhr",sizeof(cfg.hostName));
  cfg.powerLimitMa=1000;cfg.gameTimeoutSec=150;
  cfg.adminUserSettingsVersion=ADMIN_USER_SETTINGS_VERSION;strlcpy(cfg.adminUser,"wortuhr",sizeof(cfg.adminUser));
  cfg.ldrSettingsVersion=LDR_SETTINGS_VERSION;cfg.ldrDarkRaw=0;cfg.ldrBrightRaw=1023;cfg.ldrSmoothing=4;
  initV32Defaults();
  initFxParamDefaults();
  initV4Defaults();
}
bool settingsPlausible(const Settings& s){return s.magic==SETTINGS_MAGIC&&s.power<=1&&s.foregroundEffect<FX_COUNT&&s.backgroundEffect<FX_COUNT&&s.legacyEffectSpeed>=1&&s.legacyEffectSpeed<=100&&s.scrollSpeed>=20&&s.mqttPort>0;}
bool settingsV27Plausible(const SettingsV27Middle& s){return s.magic==SETTINGS_MAGIC&&s.version==SETTINGS_VERSION&&s.power<=1&&s.foregroundEffect<FX_COUNT&&s.backgroundEffect<FX_COUNT&&s.effectSpeed>=1&&s.effectSpeed<=100&&s.scrollSpeed>=20&&s.mqttPort>0;}
void migrateV27Middle(const SettingsV27Middle& old){
  defaultSettings();
  #define COPY_SETTING(name) cfg.name=old.name
  COPY_SETTING(power);COPY_SETTING(brightnessMin);COPY_SETTING(brightnessMax);COPY_SETTING(foregroundEffect);COPY_SETTING(backgroundEffect);cfg.legacyEffectSpeed=old.effectSpeed;COPY_SETTING(scrollSpeed);COPY_SETTING(backgroundLevel);COPY_SETTING(showTemperature);
  COPY_SETTING(fgR);COPY_SETTING(fgG);COPY_SETTING(fgB);COPY_SETTING(bgR);COPY_SETTING(bgG);COPY_SETTING(bgB);COPY_SETTING(mqttEnabled);COPY_SETTING(mqttDiscovery);COPY_SETTING(mqttPort);
  memcpy(cfg.mqttHost,old.mqttHost,sizeof(cfg.mqttHost));memcpy(cfg.mqttUser,old.mqttUser,sizeof(cfg.mqttUser));memcpy(cfg.mqttPass,old.mqttPass,sizeof(cfg.mqttPass));memcpy(cfg.mqttBase,old.mqttBase,sizeof(cfg.mqttBase));
  COPY_SETTING(previewEnabled);COPY_SETTING(presetsEnabled);COPY_SETTING(notificationsEnabled);COPY_SETTING(calendarEnabled);COPY_SETTING(gamesEnabled);COPY_SETTING(highScoresEnabled);COPY_SETTING(weatherAnimationsEnabled);COPY_SETTING(roomClimateEnabled);COPY_SETTING(activePreset);
  COPY_SETTING(highSnake);COPY_SETTING(highTetris);COPY_SETTING(highPong);COPY_SETTING(weatherSource);COPY_SETTING(weatherLat);COPY_SETTING(weatherLon);memcpy(cfg.weatherLocation,old.weatherLocation,sizeof(cfg.weatherLocation));
  COPY_SETTING(minuteSettingsVersion);COPY_SETTING(minuteEffect);COPY_SETTING(minuteLevel);COPY_SETTING(minuteR);COPY_SETTING(minuteG);COPY_SETTING(minuteB);COPY_SETTING(flappySettingsVersion);COPY_SETTING(highFlappy);
  #undef COPY_SETTING
  cfg.version=SETTINGS_VERSION;cfg.speedSettingsVersion=SPEED_SETTINGS_VERSION;cfg.foregroundSpeed=old.effectSpeed;cfg.backgroundSpeed=old.effectSpeed;cfg.minuteSpeed=old.effectSpeed;
}
void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0,cfg);
  bool upgraded=false;
  if(!settingsPlausible(cfg)){
    SettingsV27Middle old;EEPROM.get(0,old);
    if(settingsV27Plausible(old)){migrateV27Middle(old);upgraded=true;}
    else{defaultSettings();upgraded=true;}
  }
  if(cfg.version!=SETTINGS_VERSION){cfg.version=SETTINGS_VERSION;upgraded=true;}
  if(cfg.brightnessMin<1){cfg.brightnessMin=1;upgraded=true;}if(cfg.brightnessMax<cfg.brightnessMin){cfg.brightnessMax=cfg.brightnessMin;upgraded=true;}
  if(cfg.weatherSource>1 || isnan(cfg.weatherLat) || isnan(cfg.weatherLon) || cfg.weatherLat < -90 || cfg.weatherLat > 90 || cfg.weatherLon < -180 || cfg.weatherLon > 180){cfg.weatherSource=0;cfg.weatherLat=WEATHER_LAT;cfg.weatherLon=WEATHER_LON;upgraded=true;}
  cfg.weatherLocation[sizeof(cfg.weatherLocation)-1]=0;cfg.mqttHost[sizeof(cfg.mqttHost)-1]=0;cfg.mqttUser[sizeof(cfg.mqttUser)-1]=0;cfg.mqttPass[sizeof(cfg.mqttPass)-1]=0;cfg.mqttBase[sizeof(cfg.mqttBase)-1]=0;
  if((uint8_t)cfg.weatherLocation[0]==0xFF || !cfg.weatherLocation[0]){strlcpy(cfg.weatherLocation,"BERLIN",sizeof(cfg.weatherLocation));upgraded=true;}
  if(cfg.minuteSettingsVersion!=1 || cfg.minuteEffect>=FX_COUNT){cfg.minuteSettingsVersion=1;cfg.minuteEffect=FX_SOLID;cfg.minuteLevel=255;cfg.minuteR=cfg.fgR;cfg.minuteG=cfg.fgG;cfg.minuteB=cfg.fgB;upgraded=true;}
  if(cfg.flappySettingsVersion!=1){cfg.flappySettingsVersion=1;cfg.highFlappy=0;upgraded=true;}
  if(cfg.speedSettingsVersion!=SPEED_SETTINGS_VERSION||cfg.foregroundSpeed<1||cfg.foregroundSpeed>100||cfg.backgroundSpeed<1||cfg.backgroundSpeed>100||cfg.minuteSpeed<1||cfg.minuteSpeed>100){
    cfg.speedSettingsVersion=SPEED_SETTINGS_VERSION;cfg.foregroundSpeed=cfg.legacyEffectSpeed;cfg.backgroundSpeed=cfg.legacyEffectSpeed;cfg.minuteSpeed=cfg.legacyEffectSpeed;upgraded=true;
  }
  if(cfg.finalSettingsVersion!=FINAL_SETTINGS_VERSION){
    String serial=chipSerial(),suffix=serial.substring(serial.length()-8),ap="WU"+suffix.substring(0,6),admin="WU-"+suffix+"!";
    cfg.finalSettingsVersion=FINAL_SETTINGS_VERSION;strlcpy(cfg.apPass,ap.c_str(),sizeof(cfg.apPass));strlcpy(cfg.adminPass,admin.c_str(),sizeof(cfg.adminPass));strlcpy(cfg.hostName,"wortuhr",sizeof(cfg.hostName));cfg.powerLimitMa=1000;cfg.gameTimeoutSec=150;upgraded=true;
  }
  cfg.apPass[sizeof(cfg.apPass)-1]=0;cfg.adminPass[sizeof(cfg.adminPass)-1]=0;cfg.hostName[sizeof(cfg.hostName)-1]=0;
  if(strlen(cfg.apPass)<8){strlcpy(cfg.apPass,"Wortuhr31",sizeof(cfg.apPass));upgraded=true;}if(strlen(cfg.adminPass)<8){strlcpy(cfg.adminPass,"Wortuhr-Admin31",sizeof(cfg.adminPass));upgraded=true;}
  if(!cfg.hostName[0]){strlcpy(cfg.hostName,"wortuhr",sizeof(cfg.hostName));upgraded=true;}if(cfg.powerLimitMa<200||cfg.powerLimitMa>5000){cfg.powerLimitMa=1000;upgraded=true;}if(cfg.gameTimeoutSec<30||cfg.gameTimeoutSec>1800){cfg.gameTimeoutSec=150;upgraded=true;}
  if(cfg.adminUserSettingsVersion!=ADMIN_USER_SETTINGS_VERSION){cfg.adminUserSettingsVersion=ADMIN_USER_SETTINGS_VERSION;strlcpy(cfg.adminUser,"wortuhr",sizeof(cfg.adminUser));upgraded=true;}cfg.adminUser[sizeof(cfg.adminUser)-1]=0;if(!cfg.adminUser[0]){strlcpy(cfg.adminUser,"wortuhr",sizeof(cfg.adminUser));upgraded=true;}
  if(cfg.ldrSettingsVersion!=LDR_SETTINGS_VERSION){cfg.ldrSettingsVersion=LDR_SETTINGS_VERSION;cfg.ldrDarkRaw=0;cfg.ldrBrightRaw=1023;cfg.ldrSmoothing=4;upgraded=true;}if(cfg.ldrDarkRaw>1023||cfg.ldrBrightRaw>1023||abs((int)cfg.ldrBrightRaw-(int)cfg.ldrDarkRaw)<16){cfg.ldrDarkRaw=0;cfg.ldrBrightRaw=1023;upgraded=true;}if(cfg.ldrSmoothing<1||cfg.ldrSmoothing>16){cfg.ldrSmoothing=4;upgraded=true;}

  if(cfg.v32SettingsVersion!=V32_SETTINGS_VERSION){initV32Defaults();upgraded=true;}
  if(cfg.fxParamSettingsVersion!=FX_PARAM_SETTINGS_VERSION){initFxParamDefaults();upgraded=true;}
  if(cfg.v4SettingsVersion!=V4_SETTINGS_VERSION){initV4Defaults();upgraded=true;}
  if(cfg.foregroundPalette>11||cfg.backgroundPalette>11||cfg.minutePalette>11){cfg.foregroundPalette=cfg.backgroundPalette=cfg.minutePalette=0;upgraded=true;}
  if(cfg.rotationNoRepeat>6){cfg.rotationNoRepeat=4;upgraded=true;}
  if(cfg.ambientWordLevel>160){cfg.ambientWordLevel=42;upgraded=true;}
  if(!cfg.calibrationR||!cfg.calibrationG||!cfg.calibrationB){cfg.calibrationR=cfg.calibrationG=cfg.calibrationB=255;upgraded=true;}
  if(cfg.quietStartHour>23||cfg.quietEndHour>23){cfg.quietStartHour=22;cfg.quietEndHour=7;upgraded=true;}
  if(cfg.transitionType<1||cfg.transitionType>4){cfg.transitionType=4;upgraded=true;}
  if(cfg.rotationMinutes<1||cfg.rotationMinutes>240){cfg.rotationMinutes=15;upgraded=true;}
  if(cfg.regionalStyle>2){cfg.regionalStyle=0;upgraded=true;}
  if(upgraded){EEPROM.put(0,cfg);EEPROM.commit();}
  weatherRuntimeLocation=cfg.weatherLocation;
}
void saveSettings() { cfg.magic=SETTINGS_MAGIC;cfg.version=SETTINGS_VERSION;EEPROM.put(0,cfg);EEPROM.commit(); }
void scheduleSettingsSave(unsigned long delayMs=2500UL){settingsSavePending=true;settingsSaveAtMs=millis()+delayMs;}
void updateBrightnessFromLdr(){
  int lo=cfg.brightnessMin,hi=cfg.brightnessMax;
  int raw=ldrAverage8<0?currentLdrRaw:(ldrAverage8+4)/8;
  int32_t span=(int32_t)cfg.ldrBrightRaw-(int32_t)cfg.ldrDarkRaw;
  int normalized=span?constrain((int32_t)(raw-(int32_t)cfg.ldrDarkRaw)*1023L/span,0L,1023L):0;
  uint8_t target=constrain(map(normalized,0,1023,lo,hi),lo,hi);
  if(currentBrightness==0||abs((int)target-(int)currentBrightness)>=2)currentBrightness=target;
}

const char* paletteName(uint8_t p){static const char* n[]={"Eigene Farbe","Rainbow","Party","Ocean","Forest","Lava","Sunset","Ice","Heat","Retro","Aurora","Pastell"};return p<12?n[p]:n[0];}
CRGB paletteColor(uint8_t palette,CRGB base,uint8_t index){
  if(!palette)return base;
  switch(palette){
    case 1:return CHSV(index,255,255);
    case 2:return CHSV(index+sin8(index*3),220,255);
    case 3:return blend(CRGB(0,25,140),CRGB(0,220,255),sin8(index));
    case 4:return blend(CRGB(0,45,8),CRGB(80,255,80),sin8(index));
    case 5:return blend(CRGB(255,0,0),CRGB(255,150,0),sin8(index));
    case 6:return blend(CRGB(255,35,0),CRGB(255,0,150),sin8(index));
    case 7:return blend(CRGB(190,235,255),CRGB(35,90,255),sin8(index));
    case 8:return HeatColor(index);
    case 9:return blend(CRGB(255,120,45),CRGB(70,190,150),sin8(index));
    case 10:return blend(CRGB(25,255,120),CRGB(105,40,255),sin8(index));
    case 11:return CHSV(index,105,255);
  }return base;
}
bool effectFavorite(uint8_t e){return e<FX_COUNT&&(cfg.favoriteFx[e>>3]&(1<<(e&7)));}
void setEffectFavorite(uint8_t e,bool on){if(e>=FX_COUNT)return;uint8_t&m=cfg.favoriteFx[e>>3];uint8_t bit=1<<(e&7);if(on)m|=bit;else m&=~bit;scheduleSettingsSave();}
const char* effectCategory(uint8_t e){
  switch(e){case FX_SOLID:case FX_BREATHE:case FX_CANDLE:case FX_SUNRISE:return "Ruhig";case FX_FIRE:case FX_FIRE_2012:case FX_FIRE_NOISE:case FX_LAVA:return "Feuer";case FX_RAIN:case FX_DRIP:case FX_OCEAN:case FX_PACIFICA:case FX_LAKE:return "Wasser";case FX_RAINBOW:case FX_GRADIENT:case FX_GRADIENT_DYNAMIC:case FX_PRIDE:case FX_COLORWAVES:return "Farbe";case FX_MATRIX:case FX_NOISE:case FX_JULIA:case FX_GAME_OF_LIFE:return "Matrix";default:return "Animation";}
}
const char* effectName(uint8_t effect){static const char* names[]={"Statisch","Atmen","Regenbogen","Wellen","Funkeln","Matrix","Plasma","Feuer","Scanner","Aurora","Ozean","Lava","Konfetti","Komet","Pulswelle","Farbverlauf","Kerzenlicht","Sternenhimmel","Farbwechsel","Herzschlag","Noise","Black Hole","Blobs","BPM","Colored Bursts","ColorTwinkles","Crazy Bees","DNA","DNA Spiral","Drift","Drift Rose","Fire 2012","FireNoise","Funky Plank","Lissajous","Metaballs","Plasma Balls","Pulser","SinDots","Pride 2015","Colorwaves","Sinelon","Juggle","Pacifica","Polar Lights","Lake","Flow","Swirl","Squared Swirl","Hiphotic","Distortion Waves","Octopus","Soap","Julia","Meteor","Sunrise","Fireworks","Rain","Popcorn","Drip","Bouncing Balls","Halloween Eyes","Game of Life","Farbverlauf dynamisch","Chase","Theater Chase","Glitter","Lightning","Running Lights","Fairy"};return effect<FX_COUNT?names[effect]:names[0];}
int effectByName(const String& value){for(uint8_t i=0;i<FX_COUNT;i++)if(value.equalsIgnoreCase(effectName(i)))return i;return -1;}
const char* gameName(){switch(gameMode){case GAME_PONG:return "Pong";case GAME_PONG2:return "Pong 2P";case GAME_TETRIS:return "Tetris";case GAME_SNAKE:return "Snake";case GAME_FLAPPY:return "Flappy Bird";case GAME_BREAKOUT:return "Breakout";case GAME_DINO:return "Dino Run";case GAME_INVADERS:return "Space Invaders";case GAME_CONNECT4:return connect2P?"Vier gewinnt 2P":"Vier gewinnt";case GAME_LIFE:return "Game of Life";case GAME_SIMON:return "Simon Says";case GAME_FROGGER:return "Frogger";case GAME_ASTEROIDS:return "Asteroids";case GAME_MAZE:return "Labyrinth";case GAME_2048:return "2048";default:return "Uhr";}}
const char* presetName(uint8_t p){static const char* names[]={"Normal","Matrix","Ruhe","Party","Weihnachten","Wetter"};return p<6?names[p]:names[0];}
void applyPreset(uint8_t p){
  if(!cfg.presetsEnabled)return;cfg.activePreset=p%6;cfg.legacyEffectSpeed=45;cfg.foregroundSpeed=45;cfg.backgroundSpeed=45;cfg.minuteSpeed=45;
  switch(cfg.activePreset){
    case 0:cfg.foregroundEffect=FX_SOLID;cfg.backgroundEffect=FX_SOLID;cfg.fgR=0;cfg.fgG=110;cfg.fgB=255;cfg.backgroundLevel=0;break;
    case 1:cfg.foregroundEffect=FX_BREATHE;cfg.backgroundEffect=FX_MATRIX;cfg.fgR=210;cfg.fgG=255;cfg.fgB=220;cfg.bgR=0;cfg.bgG=255;cfg.bgB=70;cfg.backgroundLevel=55;cfg.foregroundSpeed=35;cfg.backgroundSpeed=42;break;
    case 2:cfg.foregroundEffect=FX_WAVES;cfg.backgroundEffect=FX_PLASMA;cfg.fgR=80;cfg.fgG=170;cfg.fgB=255;cfg.bgR=30;cfg.bgG=0;cfg.bgB=90;cfg.backgroundLevel=22;cfg.foregroundSpeed=20;cfg.backgroundSpeed=14;break;
    case 3:cfg.foregroundEffect=FX_BPM;cfg.backgroundEffect=FX_COLORED_BURSTS;cfg.backgroundLevel=50;cfg.foregroundSpeed=72;cfg.backgroundSpeed=60;cfg.minuteSpeed=72;break;
    case 4:cfg.foregroundEffect=FX_COLOR_TWINKLES;cfg.backgroundEffect=FX_COLOR_TWINKLES;cfg.fgR=255;cfg.fgG=25;cfg.fgB=25;cfg.bgR=0;cfg.bgG=180;cfg.bgB=35;cfg.backgroundLevel=48;cfg.foregroundSpeed=35;cfg.backgroundSpeed=28;break;
    case 5:cfg.foregroundEffect=FX_BREATHE;cfg.backgroundEffect=FX_WAVES;cfg.fgR=255;cfg.fgG=190;cfg.fgB=35;cfg.bgR=0;cfg.bgG=100;cfg.bgB=255;cfg.backgroundLevel=35;cfg.weatherAnimationsEnabled=1;cfg.foregroundSpeed=26;cfg.backgroundSpeed=18;break;
  }cfg.legacyEffectSpeed=cfg.foregroundSpeed;cfg.minuteEffect=cfg.foregroundEffect;cfg.minuteR=cfg.fgR;cfg.minuteG=cfg.fgG;cfg.minuteB=cfg.fgB;cfg.minuteLevel=255;scheduleSettingsSave();mqttDirtyMask|=MD_FG|MD_BG|MD_MIN_FX|MD_MIN_LEVEL|MD_PRESET|MD_F_WEATHER_ANIM|MD_LIGHT;
}

void loadExtendedSettings(){
  preferences.begin("wortuhr",true);size_t n=preferences.getBytesLength("automation");if(n==sizeof(automationCfg))preferences.getBytes("automation",&automationCfg,sizeof(automationCfg));else memset(&automationCfg,0,sizeof(automationCfg));
  n=preferences.getBytesLength("presets");if(n==sizeof(userPresets))preferences.getBytes("presets",userPresets,sizeof(userPresets));else memset(userPresets,0,sizeof(userPresets));preferences.end();
  if(automationCfg.version!=AUTOMATION_VERSION){memset(&automationCfg,0,sizeof(automationCfg));automationCfg.version=AUTOMATION_VERSION;automationCfg.alarmHour=7;automationCfg.alarmMinute=0;automationCfg.alarmWeekdays=0x1F;automationCfg.alarmLeadMinutes=20;}
}
void saveAutomation(){preferences.begin("wortuhr",false);preferences.putBytes("automation",&automationCfg,sizeof(automationCfg));preferences.end();}
void saveUserPresets(){preferences.begin("wortuhr",false);preferences.putBytes("presets",userPresets,sizeof(userPresets));preferences.end();}
void captureUserPreset(uint8_t slot,const String& requestedName){if(slot>=8)return;UserPreset& p=userPresets[slot];memset(&p,0,sizeof(p));p.valid=1;String name=requestedName;name.trim();if(!name.length())name="Preset "+String(slot+1);strlcpy(p.name,name.c_str(),sizeof(p.name));p.foregroundEffect=cfg.foregroundEffect;p.backgroundEffect=cfg.backgroundEffect;p.minuteEffect=cfg.minuteEffect;p.foregroundSpeed=cfg.foregroundSpeed;p.backgroundSpeed=cfg.backgroundSpeed;p.minuteSpeed=cfg.minuteSpeed;p.fgR=cfg.fgR;p.fgG=cfg.fgG;p.fgB=cfg.fgB;p.bgR=cfg.bgR;p.bgG=cfg.bgG;p.bgB=cfg.bgB;p.minuteR=cfg.minuteR;p.minuteG=cfg.minuteG;p.minuteB=cfg.minuteB;p.backgroundLevel=cfg.backgroundLevel;p.minuteLevel=cfg.minuteLevel;p.brightnessMin=cfg.brightnessMin;p.brightnessMax=cfg.brightnessMax;saveUserPresets();}
void applyUserPreset(uint8_t slot){if(slot>=8||!userPresets[slot].valid)return;UserPreset& p=userPresets[slot];cfg.foregroundEffect=p.foregroundEffect;cfg.backgroundEffect=p.backgroundEffect;cfg.minuteEffect=p.minuteEffect;cfg.foregroundSpeed=p.foregroundSpeed;cfg.backgroundSpeed=p.backgroundSpeed;cfg.minuteSpeed=p.minuteSpeed;cfg.fgR=p.fgR;cfg.fgG=p.fgG;cfg.fgB=p.fgB;cfg.bgR=p.bgR;cfg.bgG=p.bgG;cfg.bgB=p.bgB;cfg.minuteR=p.minuteR;cfg.minuteG=p.minuteG;cfg.minuteB=p.minuteB;cfg.backgroundLevel=p.backgroundLevel;cfg.minuteLevel=p.minuteLevel;cfg.brightnessMin=p.brightnessMin;cfg.brightnessMax=p.brightnessMax;scheduleSettingsSave();mqttDirtyMask|=MD_FG|MD_BG|MD_MIN_FX|MD_MIN_LEVEL|MD_FG_SPEED|MD_BG_SPEED|MD_MIN_SPEED|MD_BG_LEVEL|MD_LIGHT;}

int easterDayOfYear(int year){int a=year%19,b=year/100,c=year%100,d=b/4,e=b%4,f=(b+8)/25,g=(b-f+1)/3,h=(19*a+b-d-g+15)%30,i=c/4,k=c%4,l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451,month=(h+l-7*m+114)/31,day=(h+l-7*m+114)%31+1;struct tm t={};t.tm_year=year-1900;t.tm_mon=month-1;t.tm_mday=day;t.tm_hour=12;mktime(&t);return t.tm_yday;}
String holidayForDate(const tm& t){int m=t.tm_mon+1,d=t.tm_mday;if(m==1&&d==1)return "FROHES NEUES JAHR";if(m==5&&d==1)return "TAG DER ARBEIT";if(m==10&&d==3)return "TAG DER DEUTSCHEN EINHEIT";if(m==11&&d==1)return "ALLERHEILIGEN";if(m==12&&d==25)return "FROHE WEIHNACHTEN";if(m==12&&d==26)return "FROHE WEIHNACHTEN";int rel=t.tm_yday-easterDayOfYear(t.tm_year+1900);if(rel==-2)return "KARFREITAG";if(rel==0)return "FROHE OSTERN";if(rel==1)return "OSTERMONTAG";if(rel==39)return "CHRISTI HIMMELFAHRT";if(rel==49)return "PFINGSTEN";if(rel==50)return "PFINGSTMONTAG";if(rel==60)return "FRONLEICHNAM";return "";}
String personalEventForDate(const tm& t){if(!automationCfg.personalDatesEnabled)return "";String all=automationCfg.personalDates,needle=(t.tm_mon+1<10?"0":"")+String(t.tm_mon+1)+"-"+(t.tm_mday<10?"0":"")+String(t.tm_mday);int pos=0;while(pos<(int)all.length()){int end=all.indexOf('\n',pos);if(end<0)end=all.length();String line=all.substring(pos,end);line.trim();if(line.startsWith(needle)){line.remove(0,5);line.trim();return line.length()?line:"ALLES GUTE";}pos=end+1;}return "";}
void startOverlay(OverlayMode mode,unsigned long durationMs);

void checkAutomations(){if(!nowTmValid)return;const struct tm& t=nowTm;int minuteKey=(t.tm_year+1900)*600000+(t.tm_yday*1440)+t.tm_hour*60+t.tm_min;if(automationCfg.alarmEnabled&&(automationCfg.alarmWeekdays&(1<<((t.tm_wday+6)%7)))){int target=automationCfg.alarmHour*60+automationCfg.alarmMinute,nowMinute=t.tm_hour*60+t.tm_min,start=(target+1440-automationCfg.alarmLeadMinutes)%1440;if(nowMinute==start&&minuteKey!=lastAlarmMinuteKey){lastAlarmMinuteKey=minuteKey;startOverlay(OVERLAY_ALARM,max(1,(int)automationCfg.alarmLeadMinutes)*60000UL);}if(nowMinute==target&&minuteKey!=lastAlarmMinuteKey){lastAlarmMinuteKey=minuteKey;enqueueMessage("GUTEN MORGEN",3);}}

  int dayKey=(t.tm_year+1900)*400+t.tm_yday;if(dayKey!=lastHolidayDayKey&&t.tm_hour>=8){lastHolidayDayKey=dayKey;String event=automationCfg.holidaysEnabled?holidayForDate(t):String();String personal=personalEventForDate(t);if(personal.length())event=personal;if(event.length()){holidayText=event;startOverlay(OVERLAY_HOLIDAY,12000);enqueueMessage(event,2);}}
}

void maskPixel(uint8_t row,uint8_t col) {
  if(row<MATRIX_H && col<MATRIX_W) activeMask[matrixMap[row][col]]=true;
}
void maskRange(uint8_t row,uint8_t start,uint8_t count) {
  for(uint8_t i=0;i<count;i++) maskPixel(row,start+i);
}

void wordESIST(){maskRange(0,0,2);maskRange(0,3,3);} void wordFUENF(){maskRange(0,7,4);}
void wordZEHN(){maskRange(1,0,4);} void wordZWANZIG(){maskRange(1,4,7);} void wordVIERTEL(){maskRange(2,4,7);}
void wordDREIVIERTEL(){maskRange(2,0,11);}
void wordVOR(){maskRange(3,0,3);} void wordNACH(){maskRange(3,7,4);} void wordHALB(){maskRange(4,0,4);}
void hourEIN(){maskRange(5,0,3);} void hourEINS(){maskRange(5,0,4);} void hourZWEI(){maskRange(5,7,4);}
void hourDREI(){maskRange(6,0,4);} void hourVIER(){maskRange(6,7,4);} void hourFUENF(){maskRange(4,7,4);}
void hourSECHS(){maskRange(7,0,5);} void hourSIEBEN(){maskRange(8,0,6);} void hourACHT(){maskRange(7,7,4);}
void hourNEUN(){maskRange(9,3,4);} void hourZEHN(){maskRange(9,0,4);} void hourELF(){maskRange(4,5,3);}
void hourZWOELF(){maskRange(8,6,5);} void wordUHR(){maskRange(9,8,3);}

void buildClockMask(uint8_t h,uint8_t m) {
  memset(activeMask,0,sizeof(activeMask)); wordESIST();

  uint8_t style=cfg.regionalEnabled?cfg.regionalStyle:0;
  bool nextHour=m>=25;
  if(m>=5&&m<=9){wordFUENF();wordNACH();} if(m>=10&&m<=14){wordZEHN();wordNACH();}
  if(m>=15&&m<=19){if(style==1){wordVIERTEL();nextHour=true;}else{wordVIERTEL();wordNACH();}}
  if(m>=20&&m<=24){if(style==2){wordZEHN();wordVOR();wordHALB();nextHour=true;}else{wordZWANZIG();wordNACH();}}
  if(m>=25&&m<=29){wordFUENF();wordVOR();wordHALB();} if(m>=30&&m<=34)wordHALB();
  if(m>=35&&m<=39){wordFUENF();wordNACH();wordHALB();}
  if(m>=40&&m<=44){if(style==2){wordZEHN();wordNACH();wordHALB();}else{wordZWANZIG();wordVOR();}}
  if(m>=45&&m<=49){if(style==1)wordDREIVIERTEL();else{wordVIERTEL();wordVOR();}}
  if(m>=50&&m<=54){wordZEHN();wordVOR();}
  if(m>=55){wordFUENF();wordVOR();}
  uint8_t shownHour=h%12; if(nextHour)shownHour=(shownHour+1)%12;
  bool exact=m<5;
  switch(shownHour){case 0:hourZWOELF();break;case 1:exact?hourEIN():hourEINS();break;case 2:hourZWEI();break;case 3:hourDREI();break;case 4:hourVIER();break;case 5:hourFUENF();break;case 6:hourSECHS();break;case 7:hourSIEBEN();break;case 8:hourACHT();break;case 9:hourNEUN();break;case 10:hourZEHN();break;case 11:hourELF();break;}
  if(exact)wordUHR(); for(uint8_t i=0;i<m%5;i++)activeMask[110+i]=true;
}

uint8_t glyphColumn(char c,uint8_t col) {
  if(c>='a'&&c<='z')c-=32; if(c<32||c>90||col>4)return 0;
  return pgm_read_byte(&font5x7[c-32][col]);
}
void drawGlyphMask(char c,int x,int y) {
  for(uint8_t gx=0;gx<5;gx++){uint8_t bits=glyphColumn(c,gx);for(uint8_t gy=0;gy<7;gy++)if(bits&(1<<gy)){int px=x+gx,py=y+gy;if(px>=0&&px<MATRIX_W&&py>=0&&py<MATRIX_H)maskPixel(py,px);}}
}
void buildScrollMask() {
  memset(activeMask,0,sizeof(activeMask));
  int x=scrollX; for(uint16_t i=0;i<scrollText.length();i++,x+=6)if(x>-5&&x<MATRIX_W)drawGlyphMask(scrollText[i],x,1);
}

void setWifiUiText(String text) {
  text.replace("_"," ");
  wifiUiText=displayText(text)+"   ";
  wifiUiX=MATRIX_W;
  lastWifiUiStepMs=millis();
}

bool wifiAccessPointRunning(){
  wifi_mode_t mode=WiFi.getMode();
  return (mode==WIFI_AP||mode==WIFI_AP_STA) && WiFi.softAPIP() != IPAddress(0,0,0,0);
}
void nextWifiPortalPage() {
  wifiPortalPage=(wifiPortalPage+1)%5;
  if(wifiPortalPage==0)setWifiUiText("CONFIG WIFI");
  else if(wifiPortalPage==1)setWifiUiText(wifiApName);
  else if(wifiPortalPage==2)setWifiUiText(String("PASS ")+cfg.apPass);
  else if(wifiPortalPage==3)setWifiUiText("MIT HANDY VERBINDEN");
  else setWifiUiText("192.168.4.1");
}

void updateWifiUi(unsigned long now) {
  if(wifiUiMode==WIFI_UI_PORTAL&&!wifiAccessPointRunning()){
    wifiPortalActive=false;
    wifiUiMode=WIFI_UI_CONNECTING;
    setWifiUiText("WIFI AP NICHT AKTIV");
    static unsigned long lastWarningMs=0;
    if(now-lastWarningMs>=10000UL){lastWarningMs=now;Serial.println("[WiFi] Portal-Anzeige unterdrueckt: SoftAP nicht aktiv");}
  }
  if(wifiUiMode==WIFI_UI_NONE)return;

  if(wifiUiMode==WIFI_UI_SUCCESS&&wifiSuccessUntilMs&&(long)(now-wifiSuccessUntilMs)>=0){wifiUiMode=WIFI_UI_NONE;wifiUiText="";return;}
  if(wifiUiText.length()==0){
    if(wifiUiMode==WIFI_UI_CONNECTING)setWifiUiText("WIFI VERBINDEN");
    else if(wifiUiMode==WIFI_UI_PORTAL)setWifiUiText("CONFIG WIFI");
    else setWifiUiText("WIFI VERBUNDEN");
  }
  const uint16_t stepMs=wifiUiMode==WIFI_UI_PORTAL?75:65;
  if(now-lastWifiUiStepMs<stepMs)return;
  lastWifiUiStepMs=now;wifiUiX--;
  if(wifiUiX<-(int)wifiUiText.length()*6){
    if(wifiUiMode==WIFI_UI_PORTAL)nextWifiPortalPage();
    else if(wifiUiMode==WIFI_UI_SUCCESS){wifiUiMode=WIFI_UI_NONE;wifiUiText="";}
    else setWifiUiText("WIFI VERBINDEN");
  }
}

void renderWifiUi(unsigned long now) {
  memset(activeMask,0,sizeof(activeMask));
  int x=wifiUiX;
  for(uint16_t i=0;i<wifiUiText.length();i++,x+=6)if(x>-5&&x<MATRIX_W)drawGlyphMask(wifiUiText[i],x,1);
  FastLED.setBrightness(255);fill_solid(leds,NUM_LEDS,CRGB::Black);
  uint8_t breathe=beatsin8(10,10,34);
  CRGB accent=wifiUiMode==WIFI_UI_SUCCESS?CRGB(70,255,125):CRGB(80,215,255);
  for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t px=0;px<MATRIX_W;px++){
    uint8_t index=matrixMap[y][px];
    if(activeMask[index])leds[index]=scaleColor(accent,max((uint8_t)96,currentBrightness));
    else if(wifiUiMode==WIFI_UI_PORTAL&&((px+y+(now/350))%5==0))leds[index]=CRGB(0,0,breathe);
  }

  uint8_t activeDot=(now/300)%4;
  for(uint8_t i=0;i<4;i++)leds[110+i]=scaleColor(accent,i==activeDot?max((uint8_t)96,currentBrightness):22);
  yield();FastLED.show();
}
void drawCompactDigit(int d,int x,int y) {
  static const uint8_t digits[10][3] PROGMEM={{31,17,31},{0,31,0},{29,21,23},{21,21,31},{7,4,31},{23,21,29},{31,21,29},{1,1,31},{31,21,31},{23,21,31}};
  for(uint8_t gx=0;gx<3;gx++){uint8_t bits=pgm_read_byte(&digits[d][gx]);for(uint8_t gy=0;gy<5;gy++)if(bits&(1<<gy))maskPixel(y+gy,x+gx);}
}
void buildTemperatureMask() {
  memset(activeMask,0,sizeof(activeMask)); int t=(int)round(currentTemperature); t=constrain(t,-9,99);
  if(t<0){maskPixel(5,0);maskPixel(5,1);drawCompactDigit(-t,3,3);} else if(t<10){drawCompactDigit(t,3,3);} else {drawCompactDigit(t/10,0,3);drawCompactDigit(t%10,4,3);}

  maskPixel(2,8);maskPixel(2,9);maskPixel(3,8);maskPixel(3,9);maskPixel(5,9);maskPixel(5,10);maskPixel(6,9);maskPixel(7,9);maskPixel(7,10);
}

CRGB scaleColor(CRGB c,uint8_t amount){c.nscale8_video(amount);return c;}
CRGB readableWordColor(CRGB effect,CRGB fallback){

  uint8_t peak=max(effect.r,max(effect.g,effect.b));
  if(peak>=96)return effect;
  uint8_t fallbackPeak=max(fallback.r,max(fallback.g,fallback.b));
  if(fallbackPeak<32)fallback=CRGB::White;
  fallbackPeak=max(fallback.r,max(fallback.g,fallback.b));
  uint16_t factor=(uint16_t)96*255/fallbackPeak;
  fallback.r=min(255,(int)(((uint16_t)fallback.r*factor)/255));
  fallback.g=min(255,(int)(((uint16_t)fallback.g*factor)/255));
  fallback.b=min(255,(int)(((uint16_t)fallback.b*factor)/255));
  return fallback;
}
uint8_t pointGlow(int x,int y,int px,int py,uint8_t falloff){int dx=x-px,dy=y-py;int v=255-(dx*dx+dy*dy)*falloff;return v<=0?0:(v>=255?255:v);}
void updateEffectPhase(unsigned long now){
  if(!lastEffectPhaseMs){lastEffectPhaseMs=now;return;}unsigned long dt=now-lastEffectPhaseMs;lastEffectPhaseMs=now;if(dt>250)dt=250;
  auto advance=[dt](uint8_t speed,uint32_t& phase,uint16_t& remainder){

    uint32_t value=(uint32_t)dt*(speed*8UL+12UL)+remainder;phase+=value/1000UL;remainder=value%1000UL;
  };
  auto smartSpeed=[](uint8_t v)->uint8_t{
    if(cfg.ldrAdaptiveEffects&&cfg.brightnessMax>cfg.brightnessMin){uint8_t span=cfg.brightnessMax-cfg.brightnessMin;uint8_t rel=constrain((int)(currentBrightness-cfg.brightnessMin)*100/max((int)span,1),0,100);v=constrain((int)v*(60+rel)/160,1,100);}
    if(cfg.dayProfileEnabled&&nowTmValid){uint8_t h=nowTm.tm_hour;bool quiet=cfg.quietStartHour>cfg.quietEndHour?(h>=cfg.quietStartHour||h<cfg.quietEndHour):(h>=cfg.quietStartHour&&h<cfg.quietEndHour);if(quiet)v=max((uint8_t)1,(uint8_t)(v/2));}
    return v;
  };
  advance(smartSpeed(cfg.foregroundSpeed),foregroundPhase,foregroundPhaseRemainder);
  advance(smartSpeed(cfg.backgroundSpeed),backgroundPhase,backgroundPhaseRemainder);
  advance(smartSpeed(cfg.minuteSpeed),minutePhase,minutePhaseRemainder);
}

uint8_t curFgEffect(){return(cfg.rotationEnabled&&rotFg<FX_COUNT)?rotFg:cfg.foregroundEffect;}
uint8_t curBgEffect(){return(cfg.rotationEnabled&&rotBg<FX_COUNT)?rotBg:cfg.backgroundEffect;}
bool wasRecentlyRotated(uint8_t e){uint8_t n=min((uint8_t)6,cfg.rotationNoRepeat);for(uint8_t i=0;i<n;i++)if(rotationHistory[i]==e)return true;return false;}
void rememberRotation(uint8_t e){rotationHistory[rotationHistoryPos++%6]=e;}
uint8_t chooseRotationEffect(bool foreground){
  static const uint8_t readable[]={FX_SOLID,FX_BREATHE,FX_RAINBOW,FX_GRADIENT,FX_GRADIENT_DYNAMIC,FX_COLORSHIFT,FX_CANDLE,FX_OCEAN,FX_AURORA,FX_LAVA,FX_PLASMA,FX_NOISE,FX_BPM,FX_PRIDE,FX_COLORWAVES,FX_PACIFICA,FX_LAKE,FX_FLOW};
  uint8_t candidates[FX_COUNT],n=0;
  if(foreground){for(uint8_t e:readable)if((!cfg.rotationFavoritesOnly||effectFavorite(e))&&!wasRecentlyRotated(e))candidates[n++]=e;}
  else for(uint8_t e=1;e<FX_COUNT;e++)if((!cfg.rotationFavoritesOnly||effectFavorite(e))&&!wasRecentlyRotated(e))candidates[n++]=e;
  if(!n){if(foreground){for(uint8_t e:readable)if(!wasRecentlyRotated(e))candidates[n++]=e;}else for(uint8_t e=1;e<FX_COUNT;e++)if(!wasRecentlyRotated(e))candidates[n++]=e;}
  if(!n)return foreground?FX_SOLID:FX_MATRIX;
  uint8_t e=candidates[random8(n)];rememberRotation(e);return e;
}
void updateRotation(unsigned long now){
  if(!cfg.rotationEnabled){rotFg=rotBg=255;lastRotationMs=now;return;}
  if(rotFg>=FX_COUNT||(now-lastRotationMs)>=(unsigned long)cfg.rotationMinutes*60000UL){lastRotationMs=now;rotFg=chooseRotationEffect(true);rotBg=chooseRotationEffect(false);}
}
void nextEffectByButton(){
  if(cfg.rotationEnabled&&rotFg<FX_COUNT){rotFg=(rotFg+1)%FX_COUNT;lastRotationMs=millis();return;}
  cfg.foregroundEffect=(cfg.foregroundEffect+1)%FX_COUNT;cfg.minuteEffect=cfg.foregroundEffect;scheduleSettingsSave();mqttDirtyMask|=MD_FG|MD_MIN_FX|MD_LIGHT;
}

CRGB effectColor(Effect fx,uint8_t x,uint8_t y,CRGB base,uint8_t baseHue,uint8_t speed,uint32_t phase,uint8_t intensity,uint8_t trail,uint8_t size) {
  switch(fx){
    case FX_SOLID:return base;
    case FX_BREATHE:return scaleColor(base,beatsin8(7+speed/10,45,255));
    case FX_RAINBOW:return CHSV((x*19+y*9+phase/3)&255,235,255);
    case FX_WAVES:return CHSV(baseHue,220,sin8(x*28+y*13+phase));
    case FX_TWINKLE:{uint8_t n=inoise8(x*53,y*71,phase*8);return scaleColor(base,n>205?255:35);}
    case FX_MATRIX:{
      const int cycle=MATRIX_H+10;
      int head=((phase/13+x*7+(x*x)%5)%cycle)-5;
      int length=4+(x*3)%5,d=head-y;
      if(d==0)return CHSV(baseHue,70,255);
      if(d>0&&d<length)return CHSV(baseHue,255,255-(d*32));
      return CRGB::Black;
    }
    case FX_PLASMA:{uint8_t v=sin8(x*31+phase)+cos8(y*37-phase/2);return CHSV(v+phase/4,235,255);}
    case FX_FIRE:{uint8_t vertical=75+y*18;uint8_t heat=qadd8(scale8(y==MATRIX_H-1?random8(130,255):sin8(phase+x*27-y*20),vertical),55);return HeatColor(heat);}
    case FX_SCANNER:{int p=(phase/18)%(MATRIX_W*2-2);if(p>=MATRIX_W)p=MATRIX_W*2-2-p;int d=abs((int)x-p);if(d==0)return CRGB::White;return scaleColor(base,d==1?150:25);}
    case FX_AURORA:{uint8_t n=inoise8(x*36,y*54,phase*3);return CHSV(82+(n>>3),190,80+(n>>1));}
    case FX_OCEAN:{uint8_t v=100+(sin8(x*22+y*11+phase/2)>>1);return CHSV(135+(sin8(y*30-phase/3)>>4),220,v);}
    case FX_LAVA:{uint8_t h=inoise8(x*45,y*52,phase*5);return HeatColor(qadd8(h,45));}
    case FX_CONFETTI:{uint8_t n=inoise8(x*83,y*97,phase*12);if(n>225)return CHSV(n+phase,220,255);return scaleColor(base,18);}
    case FX_COMET:{int p=(phase/14)%(MATRIX_W+MATRIX_H+8);int d=abs((int)x+(int)y-p+4);if(d==0)return CRGB::White;return scaleColor(base,d<5?220-d*42:12);}
    case FX_RIPPLE:{int d=abs((int)x-5)*22+abs((int)y-4)*18;uint8_t v=sin8(d-phase);return scaleColor(base,v>180?v:20);}
    case FX_GRADIENT:return CHSV(baseHue+x*7+y*4,210,255);
    case FX_GRADIENT_DYNAMIC:{

      const uint8_t spread = 7 + (uint16_t)size * 10 / 100;
      const uint8_t diagonal = (uint8_t)((uint16_t)(x + y) * spread);
      const uint8_t shift = (uint8_t)(phase / 64UL);
      return CHSV((uint8_t)(baseHue + diagonal + shift),
                  (uint8_t)(120 + (uint16_t)intensity * 135 / 255),255);
    }
    case FX_CANDLE:{uint8_t flicker=180+random8(76);return CRGB(flicker,scale8(flicker,145),scale8(flicker,28));}
    case FX_STARS:{uint8_t n=inoise8(x*91,y*67,phase*10);if(n>238)return CRGB::White;return scaleColor(base,n>220?110:8);}
    case FX_COLORSHIFT:return CHSV(baseHue+phase/4,230,255);
    case FX_HEARTBEAT:{uint8_t p=phase&255;uint8_t beat=(p<45?sin8(p*3):(p>70&&p<108?sin8((p-70)*4):25));return scaleColor(base,beat);}
    case FX_NOISE:{uint8_t n=inoise8(x*49,y*61,phase*6);return CHSV(baseHue+(n>>4),180,n);}

    case FX_BLACK_HOLE:{
      int dx=(int)x-5,dy=(int)y-4,d2=dx*dx+dy*dy;if(d2<=1)return CRGB::White;
      uint8_t ring=sin8(d2*18-phase/2);return CHSV(baseHue+phase/7+d2*4,240,ring>178?ring:scale8(ring,28));
    }
    case FX_BLOBS:{
      int x1=(sin8(phase/3)*10)/255,y1=(cos8(phase/4)*9)/255;
      int x2=(sin8(phase/5+85)*10)/255,y2=(sin8(phase/3+170)*9)/255;
      int x3=(cos8(phase/6+40)*10)/255,y3=(sin8(phase/5+60)*9)/255;
      uint8_t v=max(pointGlow(x,y,x1,y1,16),max(pointGlow(x,y,x2,y2,18),pointGlow(x,y,x3,y3,20)));
      return CHSV(baseHue+phase/9+x*5,210,v);
    }
    case FX_BPM:{
      uint8_t beat=beatsin8(18+speed/3,70,255),stripe=sin8((x+y)*27+phase/2);
      return CHSV(baseHue+stripe/3+phase/8,230,scale8(stripe,beat));
    }
    case FX_COLORED_BURSTS:{
      int ya=(sin8(phase/2+x*23)*9)/255,yb=(cos8(phase/3+x*31+96)*9)/255;
      int d=min(abs((int)y-ya),abs((int)y-yb));uint8_t v=d==0?255:(d==1?125:12);
      return CHSV(phase/5+x*18+y*7,235,v);
    }
    case FX_COLOR_TWINKLES:{
      uint8_t n=inoise8(x*87,y*103,phase*11);if(n>218)return CHSV(n+phase/3+x*17,210,n);
      return scaleColor(base,n>195?55:6);
    }
    case FX_CRAZY_BEES:{
      uint8_t best=0,bee=0;for(uint8_t i=0;i<5;i++){int px=(sin8(phase/(3+i%2)+i*51)*10)/255;int py=(cos8(phase/(4+i%3)+i*73)*9)/255;uint8_t g=pointGlow(x,y,px,py,55);if(g>best){best=g;bee=i;}}
      return CHSV(28+bee*7+phase/20,245,best);
    }
    case FX_DNA:{
      int y1=(sin8(phase/2+x*23)*9)/255,y2=(sin8(phase/2+x*23+128)*9)/255;
      int d1=abs((int)y-y1),d2=abs((int)y-y2);uint8_t v=min(d1,d2)==0?255:(min(d1,d2)==1?75:8);
      return CHSV(baseHue+x*13+phase/7+(d2<d1?128:0),230,v);
    }
    case FX_DNA_SPIRAL:{
      int x1=(sin8(phase/2+y*26)*10)/255,x2=(sin8(phase/2+y*26+128)*10)/255;
      if(x==x1)return CRGB::White;if(x==x2)return CHSV(phase/6+y*19+128,235,255);
      bool bridge=x>min(x1,x2)&&x<max(x1,x2)&&(((y+(phase>>5))&3)!=0);
      if(bridge)return CHSV(phase/6+y*19,220,72);
      return CRGB::Black;
    }
    case FX_DRIFT:{
      uint8_t best=0,h=0;for(uint8_t i=0;i<4;i++){int px=5+(((int)sin8(phase/(4+i)+i*64)-128)*(2+i))/128;int py=4+(((int)cos8(phase/(5+i)+i*64)-128)*(2+i/2))/128;uint8_t g=pointGlow(x,y,px,py,42);if(g>best){best=g;h=i*58;}}
      return CHSV(baseHue+h+phase/10,225,best);
    }
    case FX_DRIFT_ROSE:{
      uint8_t best=0,h=0;for(uint8_t i=0;i<8;i++){uint8_t a=phase/5+i*32;int r=2+(sin8(a*3)>>6);int px=5+(((int)cos8(a)-128)*r)/128;int py=4+(((int)sin8(a)-128)*r)/128;uint8_t g=pointGlow(x,y,px,py,48);if(g>best){best=g;h=i*29;}}
      return CHSV(baseHue+h+phase/12,235,best);
    }
    case FX_FIRE_2012:{
      uint8_t n=inoise8(x*41,phase*7+x*13),cool=(MATRIX_H-1-y)*22;uint8_t heat=qsub8(qadd8(n,sin8(phase+x*29-y*15)>>2),cool);
      return HeatColor(heat);
    }
    case FX_FIRE_NOISE:{
      uint8_t n=inoise8(x*55,y*39-phase/4,phase*5),heat=qsub8(n,(MATRIX_H-1-y)*17);
      return CHSV(3+(heat>>4),255,heat);
    }
    case FX_FUNKY_PLANK:{
      uint8_t band=((x+(phase>>5))/2+(y/3))&7;uint8_t v=120+(sin8(y*34+phase+band*23)>>1);
      return CHSV(band*32+phase/5,235,v);
    }
    case FX_LISSAJOUS:{
      uint8_t best=0,h=0;for(uint8_t i=0;i<16;i++){uint8_t a=i*16;int px=(sin8(a*3+phase/3)*10)/255;int py=(sin8(a*4+phase/4+64)*9)/255;uint8_t g=pointGlow(x,y,px,py,64);if(g>best){best=g;h=a;}}
      return CHSV(baseHue+h+phase/10,235,best);
    }
    case FX_METABALLS:{
      int x1=(sin8(phase/3)*10)/255,y1=(cos8(phase/5)*9)/255,x2=(cos8(phase/4+80)*10)/255,y2=(sin8(phase/3+120)*9)/255,x3=(sin8(phase/6+170)*10)/255,y3=(cos8(phase/4+30)*9)/255;
      uint8_t v=qadd8(pointGlow(x,y,x1,y1,13),qadd8(pointGlow(x,y,x2,y2,15),pointGlow(x,y,x3,y3,16)));
      return CHSV(baseHue+phase/8+v/5,220,v);
    }
    case FX_PLASMA_BALLS:{
      uint8_t a=sin8(x*27+phase/3),b=sin8(y*31-phase/4),c=sin8((x+y)*19+phase/5);int delta=abs((int)a-(int)b);uint8_t v=qadd8(qsub8(255,min(255,delta*3)),scale8(c,80));
      return CHSV(baseHue+phase/7+c/4,235,v);
    }
    case FX_PULSER:{
      int dx=abs((int)x-5),dy=abs((int)y-4),dist=max(dx,dy);int radius=(phase/22)%9,d=abs(dist-radius);uint8_t v=d==0?255:(d==1?90:8);
      return CHSV(baseHue+radius*18+phase/12,225,v);
    }
    case FX_SINDOTS:{
      uint8_t best=0,h=0;for(uint8_t i=0;i<9;i++){int px=(sin8(phase/3+i*29)*10)/255;int py=(sin8(phase/4+i*47+64)*9)/255;uint8_t g=pointGlow(x,y,px,py,62);if(g>best){best=g;h=i*28;}}
      return CHSV(baseHue+h+phase/11,235,best);
    }
    case FX_PRIDE:{uint8_t v=sin8(x*23+y*17+phase/2);return CHSV(baseHue+phase/5+x*11-y*7,210,120+(v>>1));}
    case FX_COLORWAVES:{uint8_t a=sin8(x*31+phase/2),b=sin8(y*37-phase/3);return CHSV(baseHue+phase/6+a/3+b/4,230,96+((a+b)>>2));}
    case FX_SINELON:{int p=(sin8(phase/2)*((MATRIX_W*MATRIX_H)-1))/255,here=y*MATRIX_W+x,d=abs(here-p);return d<7?CHSV(baseHue+phase/5,235,255-d*34):scaleColor(base,5);}
    case FX_JUGGLE:{uint8_t best=0,h=0;for(uint8_t i=0;i<6;i++){int px=(sin8(phase/(2+i%3)+i*43)*10)/255,py=(cos8(phase/(3+i%2)+i*57)*9)/255;uint8_t g=pointGlow(x,y,px,py,68);if(g>best){best=g;h=i*42;}}return CHSV(baseHue+h,235,best);}
    case FX_PACIFICA:{uint8_t a=sin8(x*19+y*7+phase/4),b=sin8(x*11-y*23-phase/6);return CHSV(132+(a>>4),210,55+(a>>2)+(b>>2));}
    case FX_POLAR_LIGHTS:{uint8_t n=inoise8(x*28+phase*2,y*46,phase);uint8_t curtain=sin8(y*24+n);return CHSV(90+(n>>3),190,scale8(curtain,180));}
    case FX_LAKE:{uint8_t ripple=sin8(x*25+y*15+phase/3),n=inoise8(x*35,y*42,phase*2);return CHSV(125+(n>>4),180,65+(ripple>>1));}
    case FX_FLOW:return CHSV(baseHue+phase/6+inoise8(x*45+phase,y*39-phase)/3,220,190+((sin8(x*18+y*24+phase/2))>>2));
    case FX_SWIRL:{int dx=(int)x-5,dy=(int)y-4;uint8_t a=(uint8_t)(x*23-y*31),r=(uint8_t)sqrt16(dx*dx+dy*dy)*35;return CHSV(baseHue+a+phase/4,230,sin8(r-phase));}
    case FX_SQUARED_SWIRL:{int d=max(abs((int)x-5),abs((int)y-4));uint8_t edge=(x+y+d*3)*19;return CHSV(baseHue+edge+phase/4,230,sin8(d*45+edge-phase));}
    case FX_HIPHOTIC:{uint8_t v=sin8((x*17)^(y*29)^((phase/3)&255));return CHSV(baseHue+v+phase/7,240,v);}
    case FX_DISTORTION_WAVES:{uint8_t n=inoise8(x*52+phase*2,y*47-phase,phase*3);return CHSV(baseHue+n/2+phase/8,235,sin8(n+x*17-y*13));}
    case FX_OCTOPUS:{int dx=x-5,dy=y-4;uint8_t a=(uint8_t)(x*29-y*37),r=(uint8_t)sqrt16(dx*dx+dy*dy)*42;uint8_t arms=sin8(a*4+r-phase);return CHSV(baseHue+a/2+phase/9,235,arms>125?arms:12);}
    case FX_SOAP:{uint8_t n=inoise8(x*71,y*63,phase*3);uint8_t edge=abs((int)n-128);return CHSV(baseHue+n+phase/11,135,edge>92?255:35);}
    case FX_JULIA:{int16_t zx=(x-5)*45,zy=(y-4)*48;uint8_t i=0;for(;i<9&&((int32_t)zx*zx+(int32_t)zy*zy)<30000;i++){int16_t nx=((int32_t)zx*zx-(int32_t)zy*zy)/128-70+sin8(phase/8)/3;zy=((int32_t)zx*zy)/64+28;zx=nx;}return CHSV(baseHue+i*25+phase/10,230,i==9?20:255-i*18);}
    case FX_METEOR:{int p=(phase/8)%(MATRIX_W+MATRIX_H+12),d=abs((int)x+y-p+5);if(d==0)return CRGB::White;if(d<8)return CRGB(CHSV(baseHue+phase/8,220,255-d*31));return CRGB::Black;}
    case FX_SUNRISE:{uint8_t horizon=constrain((int)(phase/20)%24-5,0,18),v=qsub8(255,abs((int)y-(MATRIX_H-1-horizon/2))*31);return CHSV(4+horizon*2+x,220-horizon*5,v);}
    case FX_FIREWORKS:{uint8_t seed=(phase>>6)+x*17+y*31;int cx=2+(seed%7),cy=2+((seed>>3)%5),r=(phase>>3)%9,d=abs(abs((int)x-cx)+abs((int)y-cy)-r);if(d<2)return CRGB(CHSV(seed*29+phase/5,220,d?120:255));return CRGB::Black;}
    case FX_RAIN:{int head=((phase/9)+x*5+(x*x))%(MATRIX_H+7),d=head-y;if(d==0)return CRGB(CHSV(baseHue,80,255));if(d>0&&d<5)return CRGB(CHSV(baseHue,220,220-d*42));return CRGB::Black;}
    case FX_POPCORN:{uint8_t seed=x*37+phase/16;int peak=seed%MATRIX_H,age=(phase/5+x*23)%28,py=MATRIX_H-1-max(0,peak-abs((int)age-peak));if(y==py)return CRGB(CHSV(baseHue+x*21,170,255));return CRGB::Black;}
    case FX_DRIP:{int drop=((phase/8)+x*11)%(MATRIX_H+8),d=drop-y;return d==0?CHSV(baseHue+x*5,150,255):(d>0&&d<3?scaleColor(base,130-d*35):CRGB::Black);}
    case FX_BOUNCING_BALLS:{uint8_t best=0;for(uint8_t i=0;i<5;i++){int px=(i*23+3)%MATRIX_W;uint8_t t=(phase/(3+i))%18;int py=t<9?8-t:t-9;best=max(best,pointGlow(x,y,px,py,75));}return CHSV(baseHue+x*13+phase/10,220,best);}
    case FX_HALLOWEEN_EYES:{int shift=(phase/80)%7;bool eye=(y==3+shift%3)&&((x==2||x==3)||(x==7||x==8));return eye?CRGB(255,18,0):CRGB::Black;}
    case FX_GAME_OF_LIFE:if(fxLife[fxLifePage][y][x])return CRGB(CHSV(baseHue+x*9+y*13+phase/12,190,255));return CRGB::Black;
    case FX_CHASE:{
      int len=MATRIX_W*MATRIX_H,here=y*MATRIX_W+x,p=(phase/10)%len,d=(p-here+len)%len;
      uint8_t tailLen=1+(trail*20UL/255UL);if(d==0)return CRGB::White;if(d<=tailLen)return CHSV(baseHue+phase/10,230,255-(d*220/tailLen));return scaleColor(base,8);
    }
    case FX_THEATER_CHASE:{
      uint8_t group=1+(size*4UL/100UL),gap=group+2+(trail*5UL/255UL),pos=(x+y*MATRIX_W+(phase/18))%gap;
      return pos<group?CHSV(baseHue+phase/12+x*8,210,80+(intensity*175UL/255UL)):scaleColor(base,6);
    }
    case FX_GLITTER:{
      uint8_t n=inoise8(x*101,y*83,phase*17);uint8_t threshold=250-(intensity*45UL/255UL);
      if(n>threshold)return n>251?CRGB(CRGB::White):CRGB(CHSV(baseHue+n+phase/9,150,255));return scaleColor(base,18);
    }
    case FX_LIGHTNING:{
      uint8_t t=(phase/9)&255,n=inoise8((phase>>3)+x*33,y*57);bool flash=(t<6+(intensity>>5))&&(n>120);
      if(flash){int center=(inoise8(phase>>4)*MATRIX_W)>>8;int d=abs((int)x-center);return d<=max(1,(int)size/28)?CRGB::White:scaleColor(base,trail>>3);}
      return scaleColor(base,3+(trail>>5));
    }
    case FX_RUNNING_LIGHTS:{
      uint8_t spacing=10+(100-size);uint8_t wave=sin8((x*MATRIX_H+y)*spacing/5+phase/2);uint8_t floor=8+(trail>>3);
      return CHSV(baseHue+phase/14,200,max(floor,(int)scale8(wave,intensity)));
    }
    case FX_FAIRY:{
      uint8_t n=inoise8(x*67,y*89,phase*7);uint8_t threshold=235-(intensity*35UL/255UL);
      if(n>threshold)return CHSV(baseHue+20+(n>>3)+phase/18,90,180+(n>>2));return scaleColor(base,10+(trail>>4));
    }
    default:return base;
  }
}

void updateEffectState(unsigned long now){
  if(now-lastFxStateMs<300)return;lastFxStateMs=now;
  bool wanted=curFgEffect()==FX_GAME_OF_LIFE||curBgEffect()==FX_GAME_OF_LIFE||cfg.minuteEffect==FX_GAME_OF_LIFE;
  if(!wanted)return;uint8_t next=fxLifePage^1;bool any=false;
  for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++){uint8_t n=0;for(int8_t dy=-1;dy<=1;dy++)for(int8_t dx=-1;dx<=1;dx++)if(dx||dy)n+=fxLife[fxLifePage][(y+dy+MATRIX_H)%MATRIX_H][(x+dx+MATRIX_W)%MATRIX_W];uint8_t alive=fxLife[fxLifePage][y][x];fxLife[next][y][x]=(n==3||(alive&&n==2));any|=fxLife[next][y][x];}
  fxLifePage=next;if(!any)for(uint8_t i=0;i<28;i++)fxLife[fxLifePage][random8(MATRIX_H)][random8(MATRIX_W)]=1;
}

const uint16_t tetromino[7][4] PROGMEM={
  {0x00F0,0x2222,0x00F0,0x2222},
  {0x0066,0x0066,0x0066,0x0066},
  {0x0072,0x0262,0x0270,0x0232},
  {0x0036,0x0462,0x0036,0x0462},
  {0x0063,0x0264,0x0063,0x0264},
  {0x0071,0x0226,0x0470,0x0322},
  {0x0074,0x0622,0x0170,0x0223}
};
const CRGB tetColors[7]={CRGB::Cyan,CRGB::Yellow,CRGB(180,0,255),CRGB::Green,CRGB::Red,CRGB::Blue,CRGB(255,90,0)};
bool tetBit(uint8_t type,uint8_t rotation,uint8_t x,uint8_t y){return pgm_read_word(&tetromino[type][rotation&3])&(1U<<(y*4+x));}
bool tetCollision(int8_t nx,int8_t ny,uint8_t nr){
  for(uint8_t y=0;y<4;y++)for(uint8_t x=0;x<4;x++)if(tetBit(tetType,nr,x,y)){
    int px=nx+x,py=ny+y;if(px<0||px>=MATRIX_W||py>=MATRIX_H)return true;if(py>=0&&tetrisBoard[py][px])return true;
  }return false;
}
void storeHighscore(uint16_t& slot,uint16_t score,uint32_t dirtyBit){if(cfg.highScoresEnabled&&score>slot){slot=score;scheduleSettingsSave(10000UL);mqttDirtyMask|=dirtyBit;}}
void newTetromino(){tetType=random8(7);tetRotation=0;tetX=3;tetY=-1;if(tetCollision(tetX,tetY,tetRotation)){storeHighscore(cfg.highTetris,gameScore,MD_HIGH_TETRIS);memset(tetrisBoard,0,sizeof(tetrisBoard));memset(tetrisColor,0,sizeof(tetrisColor));gameScore=0;gameStatus="Tetris – Neustart";}}
void lockTetromino(){
  for(uint8_t y=0;y<4;y++)for(uint8_t x=0;x<4;x++)if(tetBit(tetType,tetRotation,x,y)){int py=tetY+y,px=tetX+x;if(py>=0&&py<MATRIX_H){tetrisBoard[py][px]=true;tetrisColor[py][px]=tetType;}}
  for(int y=MATRIX_H-1;y>=0;y--){bool full=true;for(uint8_t x=0;x<MATRIX_W;x++)if(!tetrisBoard[y][x])full=false;if(full){for(int yy=y;yy>0;yy--){memcpy(tetrisBoard[yy],tetrisBoard[yy-1],sizeof(tetrisBoard[0]));memcpy(tetrisColor[yy],tetrisColor[yy-1],sizeof(tetrisColor[0]));}memset(tetrisBoard[0],0,sizeof(tetrisBoard[0]));gameScore+=100;y++;}}
  newTetromino();
}
bool snakeOccupies(int x,int y){for(uint8_t i=0;i<snakeLength;i++)if(snakeX[i]==x&&snakeY[i]==y)return true;return false;}
void placeSnakeFood(){for(uint8_t tries=0;tries<100;tries++){snakeFoodX=random8(MATRIX_W);snakeFoodY=random8(MATRIX_H);if(!snakeOccupies(snakeFoodX,snakeFoodY))return;}}
void resetFlappy(){flappyY4=16;flappyVelocity4=0;flappyPipeX=10;flappyGapTop=1+random8(5);flappyPipeScored=false;gameScore=0;}
void clearMiniGame(){memset(miniGrid,0,sizeof(miniGrid));playerX=5;playerY=8;objectX=5;objectY=4;velocityX=1;velocityY=-1;gameScore=0;gameLevel=1;gameLives=3;gameTurn=1;gameRunning=true;}
void add2048Tile(){for(uint8_t tries=0;tries<40;tries++){uint8_t x=random8(5),y=random8(5);if(!miniGrid[y][x]){miniGrid[y][x]=random8(10)?1:2;return;}}}

bool connectWon(uint8_t who){for(uint8_t y=0;y<6;y++)for(uint8_t x=0;x<7;x++)if(miniGrid[y][x]==who)for(uint8_t d=0;d<4;d++){int8_t dx[4]={1,0,1,1},dy[4]={0,1,1,-1};uint8_t n=1;for(uint8_t i=1;i<4;i++){int xx=x+dx[d]*i,yy=y+dy[d]*i;if(xx>=0&&xx<7&&yy>=0&&yy<6&&miniGrid[yy][xx]==who)n++;}if(n==4)return true;}return false;}
int8_t connectPlace(uint8_t col,uint8_t who){for(int y=5;y>=0;y--)if(!miniGrid[y][col]){miniGrid[y][col]=who;return y;}return -1;}
void connectUnplace(uint8_t col){for(uint8_t y=0;y<6;y++)if(miniGrid[y][col]){miniGrid[y][col]=0;return;}}
bool connectFull(){for(uint8_t x=0;x<7;x++)if(!miniGrid[0][x])return false;return true;}
bool connectMove(uint8_t col,uint8_t who){
  if(connectPlace(col,who)<0)return false;
  if(connectWon(who)){gameRunning=false;connectEnd=20;if(who==1)gameScore++;gameStatus=connect2P?(who==1?"Spieler 1 gewinnt":"Spieler 2 gewinnt"):(who==1?"Vier gewinnt!":"Computer gewinnt");}
  else if(connectFull()){gameRunning=false;connectEnd=20;gameStatus="Unentschieden";}
  else gameTurn=(who==1)?2:1;
  return true;
}
void connectAiMove(){
  int8_t choice=-1;
  for(uint8_t c=0;c<7;c++){if(connectPlace(c,2)<0)continue;bool w=connectWon(2);connectUnplace(c);if(w){choice=c;break;}}
  if(choice<0)for(uint8_t c=0;c<7;c++){if(connectPlace(c,1)<0)continue;bool w=connectWon(1);connectUnplace(c);if(w){choice=c;break;}}
  if(choice<0){
    int best=-100;
    for(uint8_t c=0;c<7;c++){
      int8_t r=connectPlace(c,2);if(r<0)continue;
      int score=10-abs((int)c-3)*2+random8(4);

      if(r>0&&connectPlace(c,1)>=0){if(connectWon(1))score-=30;connectUnplace(c);}
      connectUnplace(c);
      if(score>best){best=score;choice=c;}
    }
  }
  if(choice>=0)connectMove(choice,2);
}

void mazeGenerate(){
  for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++)miniGrid[y][x]=1;
  bool vis[4][5]={};int8_t sx[20],sy[20];uint8_t sp=1;sx[0]=0;sy[0]=0;vis[0][0]=true;miniGrid[1][1]=0;
  static const int8_t dx4[4]={1,-1,0,0},dy4[4]={0,0,1,-1};
  while(sp){
    int cx=sx[sp-1],cy=sy[sp-1];uint8_t opts[4],n=0;
    for(uint8_t d=0;d<4;d++){int nx=cx+dx4[d],ny=cy+dy4[d];if(nx>=0&&nx<5&&ny>=0&&ny<4&&!vis[ny][nx])opts[n++]=d;}
    if(!n){sp--;continue;}
    uint8_t d=opts[random8(n)];int nx=cx+dx4[d],ny=cy+dy4[d];
    miniGrid[2*cy+1+dy4[d]][2*cx+1+dx4[d]]=0;miniGrid[2*ny+1][2*nx+1]=0;
    vis[ny][nx]=true;sx[sp]=nx;sy[sp]=ny;sp++;
  }
}
void move2048(int8_t dx,int8_t dy){for(uint8_t pass=0;pass<5;pass++)for(uint8_t yy=0;yy<5;yy++)for(uint8_t xx=0;xx<5;xx++){int x=dx>0?4-xx:xx,y=dy>0?4-yy:yy;if(!miniGrid[y][x])continue;int nx=x+dx,ny=y+dy;if(nx<0||nx>=5||ny<0||ny>=5)continue;if(!miniGrid[ny][nx]){miniGrid[ny][nx]=miniGrid[y][x];miniGrid[y][x]=0;}else if(miniGrid[ny][nx]==miniGrid[y][x]){miniGrid[ny][nx]++;miniGrid[y][x]=0;gameScore+=(1U<<miniGrid[ny][nx]);}}add2048Tile();}
void startGame(const String& name){
  if(name!="none"&&!cfg.gamesEnabled)return;
  if(name=="pong"){gameMode=GAME_PONG;pongPaddle=pongAi=3;pongBallX=5;pongBallY=5;pongDX=random8(2)?1:-1;pongDY=random8(2)?1:-1;pongPlayerScore=pongAiScore=0;gameScore=0;gameStatus="Pong";}
  else if(name=="pong2"){gameMode=GAME_PONG2;pongPaddle=pongAi=3;pongBallX=5;pongBallY=5;pongDX=random8(2)?1:-1;pongDY=random8(2)?1:-1;pongPlayerScore=pongAiScore=0;gameScore=0;gameStatus="Pong 2P";}
  else if(name=="tetris"){gameMode=GAME_TETRIS;memset(tetrisBoard,0,sizeof(tetrisBoard));memset(tetrisColor,0,sizeof(tetrisColor));gameScore=0;gameStatus="Tetris";newTetromino();}
  else if(name=="snake"){gameMode=GAME_SNAKE;snakeLength=3;snakeX[0]=5;snakeY[0]=5;snakeX[1]=4;snakeY[1]=5;snakeX[2]=3;snakeY[2]=5;snakeDirection=snakeNextDirection=1;gameScore=0;gameStatus="Snake";placeSnakeFood();}
  else if(name=="flappy"||name=="flappy bird"){gameMode=GAME_FLAPPY;resetFlappy();gameStatus="Flappy Bird";}
  else if(name=="breakout"){gameMode=GAME_BREAKOUT;clearMiniGame();playerX=4;playerY=9;objectX=5;objectY=7;velocityX=velocityY=-1;for(uint8_t y=0;y<3;y++)for(uint8_t x=0;x<MATRIX_W;x++)miniGrid[y][x]=1+(y%3);gameStatus="Breakout";}
  else if(name=="dino"||name=="dino run"){gameMode=GAME_DINO;clearMiniGame();playerX=2;playerY=8;objectX=10;objectY=8;velocityY=0;gameStatus="Dino Run";}
  else if(name=="invaders"||name=="space invaders"){gameMode=GAME_INVADERS;clearMiniGame();playerY=9;for(uint8_t y=0;y<3;y++)for(uint8_t x=1;x<10;x+=2)miniGrid[y][x]=1;objectY=-1;gameStatus="Space Invaders";}
  else if(name=="connect4"||name=="vier gewinnt"||name=="connect4-2p"||name=="vier gewinnt 2p"){gameMode=GAME_CONNECT4;clearMiniGame();connect2P=(name=="connect4-2p"||name=="vier gewinnt 2p");connectColumn=3;connectEnd=0;gameStatus=connect2P?"Vier gewinnt 2P":"Vier gewinnt";}
  else if(name=="life"||name=="game of life"){gameMode=GAME_LIFE;clearMiniGame();for(uint8_t i=0;i<32;i++)miniGrid[random8(MATRIX_H)][random8(MATRIX_W)]=1;playerX=5;playerY=5;gameStatus="Game of Life";}
  else if(name=="simon"){gameMode=GAME_SIMON;clearMiniGame();simonLength=1;simonInput=0;for(uint8_t i=0;i<24;i++)simonSequence[i]=random8(4);gameStatus="Simon Says";}
  else if(name=="frogger"){gameMode=GAME_FROGGER;clearMiniGame();playerX=5;playerY=9;gameStatus="Frogger";}
  else if(name=="asteroids"){gameMode=GAME_ASTEROIDS;clearMiniGame();playerX=5;playerY=8;for(uint8_t i=0;i<12;i++)miniGrid[random8(7)][random8(MATRIX_W)]=1;objectY=-1;gameStatus="Asteroids";}
  else if(name=="maze"||name=="labyrinth"){gameMode=GAME_MAZE;clearMiniGame();mazeGenerate();playerX=1;playerY=1;objectX=9;objectY=7;gameStatus="Labyrinth";}
  else if(name=="2048"){gameMode=GAME_2048;clearMiniGame();add2048Tile();add2048Tile();gameStatus="2048";}
  else{gameMode=GAME_NONE;gameStatus="Uhr";displayMode=MODE_CLOCK;}
  lastGameStepMs=lastGameInputMs=millis();mqttDirtyMask|=MD_GAME;
}
void gameControl(const String& cmd,uint8_t player){
  if(gameMode!=GAME_NONE)lastGameInputMs=millis();
  if(gameMode==GAME_SNAKE){uint8_t d=snakeNextDirection;if(cmd=="up")d=0;else if(cmd=="right")d=1;else if(cmd=="down")d=2;else if(cmd=="left")d=3;if((d+2)%4!=snakeDirection)snakeNextDirection=d;}
  else if(gameMode==GAME_PONG||gameMode==GAME_PONG2){int8_t& paddle=(player==2&&gameMode==GAME_PONG2)?pongAi:pongPaddle;if(cmd=="up")paddle=max(0,(int)paddle-1);if(cmd=="down")paddle=min(MATRIX_H-3,(int)paddle+1);}
  else if(gameMode==GAME_FLAPPY){if(cmd=="flap"||cmd=="up"||cmd=="drop")flappyVelocity4=-5;}
  else if(gameMode==GAME_BREAKOUT){if(cmd=="left")playerX=max(0,(int)playerX-1);if(cmd=="right")playerX=min(MATRIX_W-3,(int)playerX+1);}
  else if(gameMode==GAME_DINO){if((cmd=="up"||cmd=="drop"||cmd=="flap")&&playerY==8)velocityY=-3;}
  else if(gameMode==GAME_INVADERS||gameMode==GAME_ASTEROIDS){if(cmd=="left")playerX=max(0,(int)playerX-1);if(cmd=="right")playerX=min(MATRIX_W-1,(int)playerX+1);if((cmd=="up"||cmd=="drop"||cmd=="rotate"||cmd=="flap")&&objectY<0){objectX=playerX;objectY=playerY-1;}}
  else if(gameMode==GAME_CONNECT4){
    if(cmd=="left")connectColumn=(connectColumn+6)%7;
    if(cmd=="right")connectColumn=(connectColumn+1)%7;
    if((cmd=="down"||cmd=="drop"||cmd=="flap")&&gameRunning){
      uint8_t who=connect2P?gameTurn:1;
      if(connectMove(connectColumn,who)&&!connect2P&&gameRunning)connectAiMove();
    }
  }
  else if(gameMode==GAME_LIFE){if(cmd=="left")playerX=(playerX+MATRIX_W-1)%MATRIX_W;if(cmd=="right")playerX=(playerX+1)%MATRIX_W;if(cmd=="up")playerY=(playerY+MATRIX_H-1)%MATRIX_H;if(cmd=="down")playerY=(playerY+1)%MATRIX_H;if(cmd=="drop"||cmd=="rotate"||cmd=="flap")miniGrid[playerY][playerX]^=1;}
  else if(gameMode==GAME_SIMON){uint8_t key=255;if(cmd=="up")key=0;else if(cmd=="right")key=1;else if(cmd=="down")key=2;else if(cmd=="left")key=3;if(key<4){if(key==simonSequence[simonInput]){simonInput++;if(simonInput>=simonLength){simonInput=0;simonLength=min(24,(int)simonLength+1);gameScore=simonLength-1;gameStatus="Richtig";}}else{simonLength=1;simonInput=0;gameScore=0;gameStatus="Nochmal";}}}
  else if(gameMode==GAME_FROGGER||gameMode==GAME_MAZE){
    int nx=playerX+(cmd=="right")-(cmd=="left"),ny=playerY+(cmd=="down")-(cmd=="up");
    if(nx>=0&&nx<MATRIX_W&&ny>=0&&ny<MATRIX_H&&(gameMode!=GAME_MAZE||miniGrid[ny][nx]!=1)){playerX=nx;playerY=ny;}
    if(gameMode==GAME_MAZE&&playerX==objectX&&playerY==objectY){uint16_t s=gameScore+1;startGame("maze");gameScore=s;gameStatus="Ziel erreicht";}
  }
  else if(gameMode==GAME_2048){if(cmd=="left")move2048(-1,0);if(cmd=="right")move2048(1,0);if(cmd=="up")move2048(0,-1);if(cmd=="down")move2048(0,1);}
  else if(gameMode==GAME_TETRIS){
    if(cmd=="left"&&!tetCollision(tetX-1,tetY,tetRotation))tetX--;if(cmd=="right"&&!tetCollision(tetX+1,tetY,tetRotation))tetX++;
    if(cmd=="down"){if(!tetCollision(tetX,tetY+1,tetRotation))tetY++;else lockTetromino();}
    if(cmd=="rotate"){uint8_t nr=(tetRotation+1)&3;const int8_t kickX[6]={0,-1,1,-2,2,0},kickY[6]={0,0,0,0,0,-1};for(uint8_t i=0;i<6;i++)if(!tetCollision(tetX+kickX[i],tetY+kickY[i],nr)){tetX+=kickX[i];tetY+=kickY[i];tetRotation=nr;break;}}
    if(cmd=="drop"){while(!tetCollision(tetX,tetY+1,tetRotation))tetY++;lockTetromino();}
  }
}

void wsEvent(uint8_t num,WStype_t type,uint8_t* payload,size_t length){
  if(type!=WStype_TEXT||length<5||length>24)return;
  String m;m.reserve(length+1);for(size_t i=0;i<length;i++)m+=(char)payload[i];
  int a=m.indexOf('|'),b=m.indexOf('|',a+1);
  if(a!=1||m[0]!='c'||b<0)return;
  gameControl(m.substring(a+1,b),constrain(m.substring(b+1).toInt(),1,2));
}

void stepSnake(){
  snakeDirection=snakeNextDirection;int nx=snakeX[0]+(snakeDirection==1)-(snakeDirection==3),ny=snakeY[0]+(snakeDirection==2)-(snakeDirection==0);
  bool ate=nx==snakeFoodX&&ny==snakeFoodY,grows=ate&&snakeLength<110;bool hitSelf=false;uint8_t occupiedLength=snakeLength-(grows?0:1);for(uint8_t i=0;i<occupiedLength;i++)if(snakeX[i]==nx&&snakeY[i]==ny){hitSelf=true;break;}
  if(nx<0||nx>=MATRIX_W||ny<0||ny>=MATRIX_H||hitSelf){storeHighscore(cfg.highSnake,gameScore,MD_HIGH_SNAKE);startGame("snake");gameStatus="Snake – Neustart";return;}
  if(grows){snakeLength++;gameScore+=10;}
  for(int i=snakeLength-1;i>0;i--){snakeX[i]=snakeX[i-1];snakeY[i]=snakeY[i-1];}snakeX[0]=nx;snakeY[0]=ny;if(ate)placeSnakeFood();
}
void stepPong(){
  if(gameMode==GAME_PONG&&random8(100)>=30){if(pongBallY<pongAi+1)pongAi=max(0,(int)pongAi-1);else if(pongBallY>pongAi+1)pongAi=min(MATRIX_H-3,(int)pongAi+1);}
  int nx=pongBallX+pongDX,ny=pongBallY+pongDY;if(ny<0||ny>=MATRIX_H){pongDY=-pongDY;ny=pongBallY+pongDY;}
  if(nx==0&&ny>=pongPaddle&&ny<pongPaddle+3){pongDX=1;nx=1;}if(nx==MATRIX_W-1&&ny>=pongAi&&ny<pongAi+3){pongDX=-1;nx=MATRIX_W-2;}
  if(nx<0){pongAiScore++;pongBallX=5;pongBallY=5;pongDX=1;return;}if(nx>=MATRIX_W){pongPlayerScore++;gameScore=pongPlayerScore;storeHighscore(cfg.highPong,gameScore,MD_HIGH_PONG);pongBallX=5;pongBallY=5;pongDX=-1;return;}pongBallX=nx;pongBallY=ny;
}
void stepFlappy(){
  flappyVelocity4=min(4,(int)flappyVelocity4+1);flappyY4+=flappyVelocity4;flappyPipeX--;
  int birdY=flappyY4<0?-1:(flappyY4+2)/4;bool pipeAtBird=flappyPipeX<=3&&flappyPipeX+1>=1;bool outsideGap=birdY<flappyGapTop||birdY+1>=flappyGapTop+4;
  if(!flappyPipeScored&&flappyPipeX+1<2){flappyPipeScored=true;gameScore++;}
  if(birdY<0||birdY+1>=MATRIX_H||(pipeAtBird&&outsideGap)){
    storeHighscore(cfg.highFlappy,gameScore,MD_HIGH_FLAPPY);resetFlappy();gameStatus="Flappy – Neustart";return;
  }
  if(flappyPipeX<-1){flappyPipeX=10;flappyGapTop=1+random8(5);flappyPipeScored=false;}
}
void stepMiniGame(){
  if(gameMode==GAME_BREAKOUT){int nx=objectX+velocityX,ny=objectY+velocityY;if(nx<0||nx>=MATRIX_W){velocityX=-velocityX;nx=objectX+velocityX;}if(ny<0){velocityY=1;ny=1;}if(ny>=9&&objectX>=playerX-1&&objectX<=playerX+3){velocityY=-1;ny=8;}if(ny>=MATRIX_H){gameLives--;objectX=5;objectY=7;velocityY=-1;if(!gameLives){gameLives=3;gameScore=0;for(uint8_t y=0;y<3;y++)for(uint8_t x=0;x<MATRIX_W;x++)miniGrid[y][x]=1+(y%3);}}else if(miniGrid[ny][nx]){miniGrid[ny][nx]=0;velocityY=-velocityY;gameScore+=10;}else{objectX=nx;objectY=ny;}}
  else if(gameMode==GAME_DINO){if(playerY<8||velocityY){playerY+=velocityY;velocityY++;if(playerY>=8){playerY=8;velocityY=0;}}objectX--;if(objectX<0){objectX=10;gameScore++;}if(objectX>=1&&objectX<=3&&playerY>=7){gameScore=0;objectX=10;gameStatus="Dino – Neustart";}}
  else if(gameMode==GAME_INVADERS){if(objectY>=0){objectY--;if(objectY>=0&&miniGrid[objectY][objectX]){miniGrid[objectY][objectX]=0;objectY=-1;gameScore+=10;}}if(random8(12)==0){bool any=false;for(uint8_t y=0;y<7;y++)for(uint8_t x=0;x<MATRIX_W;x++)any|=miniGrid[y][x];if(!any){gameLevel++;for(uint8_t y=0;y<min(4,(int)gameLevel+1);y++)for(uint8_t x=1;x<10;x+=2)miniGrid[y][x]=1;}}}
  else if(gameMode==GAME_LIFE){uint8_t next[MATRIX_H][MATRIX_W];for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++){uint8_t n=0;for(int8_t dy=-1;dy<=1;dy++)for(int8_t dx=-1;dx<=1;dx++)if(dx||dy)n+=miniGrid[(y+dy+MATRIX_H)%MATRIX_H][(x+dx+MATRIX_W)%MATRIX_W];next[y][x]=(n==3||(miniGrid[y][x]&&n==2));}memcpy(miniGrid,next,sizeof(miniGrid));}
  else if(gameMode==GAME_FROGGER){if(playerY==0){gameScore++;playerX=5;playerY=9;}else if(playerY>1&&playerY<9){uint8_t gap=(lastGameStepMs/260+playerY*3)%MATRIX_W;if(abs((int)playerX-gap)>1){playerX=5;playerY=9;gameStatus="Frogger – Neustart";}}}
  else if(gameMode==GAME_ASTEROIDS){if(objectY>=0){objectY--;if(objectY>=0&&miniGrid[objectY][objectX]){miniGrid[objectY][objectX]=0;objectY=-1;gameScore+=5;}}for(int y=8;y>0;y--)if(random8(5)==0)memcpy(miniGrid[y],miniGrid[y-1],MATRIX_W);if(random8(3)==0)miniGrid[0][random8(MATRIX_W)]=1;if(miniGrid[playerY][playerX]){miniGrid[playerY][playerX]=0;gameLives--;if(!gameLives){gameLives=3;gameScore=0;}}}
}
void stepConnect4(){
  if(gameRunning||!connectEnd)return;
  connectEnd--;
  if(!connectEnd){memset(miniGrid,0,sizeof(miniGrid));gameRunning=true;gameTurn=1;connectColumn=3;gameStatus=connect2P?"Vier gewinnt 2P":"Vier gewinnt";}
}
void updateGame(unsigned long now){
  if(gameMode==GAME_NONE)return;uint16_t waitMs=150;
  if(gameMode==GAME_SNAKE)waitMs=max(80,230-(int)gameScore*2);if(gameMode==GAME_TETRIS)waitMs=max(130,650-(int)gameScore/2);if(gameMode==GAME_FLAPPY)waitMs=max(75,130-(int)gameScore*3);
  if(gameMode==GAME_LIFE)waitMs=280;if(gameMode==GAME_DINO||gameMode==GAME_BREAKOUT)waitMs=110;
  if(now-lastGameStepMs<waitMs)return;lastGameStepMs=now;if(gameMode==GAME_SNAKE)stepSnake();else if(gameMode==GAME_PONG||gameMode==GAME_PONG2)stepPong();else if(gameMode==GAME_FLAPPY)stepFlappy();else if(gameMode==GAME_TETRIS){if(!tetCollision(tetX,tetY+1,tetRotation))tetY++;else lockTetromino();}else if(gameMode==GAME_CONNECT4)stepConnect4();else if(gameMode!=GAME_SIMON&&gameMode!=GAME_MAZE&&gameMode!=GAME_2048)stepMiniGame();
}
void renderGame(){
  FastLED.setBrightness(currentBrightness);
  fill_solid(&gamePixels[0][0], MATRIX_H * MATRIX_W, CRGB::Black);
  uint8_t dots=0;CRGB dotsColor=CRGB(255,60,60);
  if(gameMode==GAME_SNAKE){for(uint8_t i=0;i<snakeLength;i++)gamePixels[snakeY[i]][snakeX[i]]=i?CRGB(0,150,40):CRGB(120,255,160);gamePixels[snakeFoodY][snakeFoodX]=CRGB::Red;}
  else if(gameMode==GAME_PONG||gameMode==GAME_PONG2){for(uint8_t i=0;i<3;i++){gamePixels[pongPaddle+i][0]=CRGB(0,150,255);gamePixels[pongAi+i][MATRIX_W-1]=CRGB(255,70,30);}gamePixels[pongBallY][pongBallX]=CRGB::White;}
  else if(gameMode==GAME_FLAPPY){for(int8_t px=flappyPipeX;px<=flappyPipeX+1;px++)if(px>=0&&px<MATRIX_W)for(uint8_t y=0;y<MATRIX_H;y++)if(y<flappyGapTop||y>=flappyGapTop+4)gamePixels[y][px]=px==flappyPipeX?CRGB(20,210,65):CRGB(10,125,40);int birdY=constrain((flappyY4+2)/4,0,MATRIX_H-1);gamePixels[birdY][2]=CRGB(255,220,25);if(birdY+1<MATRIX_H)gamePixels[birdY+1][1]=CRGB(255,130,15);if(3<MATRIX_W)gamePixels[birdY][3]=CRGB(255,75,10);}
  else if(gameMode==GAME_BREAKOUT){for(uint8_t y=0;y<3;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(miniGrid[y][x])gamePixels[y][x]=CHSV(miniGrid[y][x]*55,230,255);for(uint8_t x=0;x<3;x++)gamePixels[9][playerX+x]=CRGB(30,190,255);if(objectX>=0&&objectX<MATRIX_W&&objectY>=0&&objectY<MATRIX_H)gamePixels[objectY][objectX]=CRGB::White;dots=gameLives;}
  else if(gameMode==GAME_DINO){for(uint8_t x=0;x<MATRIX_W;x++)gamePixels[9][x]=CRGB(18,55,35);gamePixels[playerY][2]=CRGB(60,235,100);if(playerY>0)gamePixels[playerY-1][2]=CRGB(115,255,130);gamePixels[8][objectX]=CRGB(255,90,35);}
  else if(gameMode==GAME_INVADERS){for(uint8_t y=0;y<7;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(miniGrid[y][x])gamePixels[y][x]=CRGB(100,255,70);gamePixels[9][playerX]=CRGB(60,170,255);if(objectY>=0)gamePixels[objectY][objectX]=CRGB::White;}
  else if(gameMode==GAME_CONNECT4){
    for(uint8_t y=0;y<6;y++)for(uint8_t x=0;x<7;x++)if(miniGrid[y][x])gamePixels[y+3][x+2]=miniGrid[y][x]==1?CRGB(255,45,35):CRGB(255,210,20);
    if(gameRunning)gamePixels[1][connectColumn+2]=(connect2P&&gameTurn==2)?CRGB(255,210,20):CRGB(255,45,35);
    else if((millis()/250)&1)for(uint8_t x=2;x<9;x++)gamePixels[1][x]=CRGB(60,255,90);
  }
  else if(gameMode==GAME_LIFE){for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(miniGrid[y][x])gamePixels[y][x]=CHSV(x*17+y*13+millis()/20,190,255);gamePixels[playerY][playerX]+=CRGB(70,70,70);}
  else if(gameMode==GAME_SIMON){uint8_t shown=simonSequence[(millis()/550)%simonLength],on=(millis()/550)&1;CRGB colors[4]={CRGB(30,110,255),CRGB(255,55,45),CRGB(255,200,25),CRGB(40,220,80)};for(uint8_t q=0;q<4;q++)for(uint8_t y=(q>1?5:0);y<(q>1?10:5);y++)for(uint8_t x=(q&1?6:0);x<(q&1?11:5);x++)gamePixels[y][x]=scaleColor(colors[q],on&&q==shown?255:45);}
  else if(gameMode==GAME_FROGGER){for(uint8_t y=2;y<9;y++){uint8_t gap=(millis()/260+y*3)%MATRIX_W;for(uint8_t x=0;x<MATRIX_W;x++)if(abs((int)x-gap)>1)gamePixels[y][x]=CHSV(135+y*4,180,80);}gamePixels[playerY][playerX]=CRGB(80,255,80);for(uint8_t x=0;x<MATRIX_W;x++)gamePixels[0][x]=CRGB(30,120,40);}
  else if(gameMode==GAME_ASTEROIDS){for(uint8_t y=0;y<9;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(miniGrid[y][x])gamePixels[y][x]=CRGB(160,120,80);gamePixels[playerY][playerX]=CRGB(50,180,255);if(objectY>=0)gamePixels[objectY][objectX]=CRGB::White;dots=gameLives;}
  else if(gameMode==GAME_MAZE){for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(miniGrid[y][x]==1)gamePixels[y][x]=CRGB(35,55,90);gamePixels[objectY][objectX]=((millis()/300)&1)?CRGB(60,255,90):CRGB(20,110,40);gamePixels[playerY][playerX]=CRGB(255,185,30);}
  else if(gameMode==GAME_2048){for(uint8_t y=0;y<5;y++)for(uint8_t x=0;x<5;x++)if(miniGrid[y][x]){CRGB c=CHSV(25+miniGrid[y][x]*24,210,255);gamePixels[y*2][x*2]=c;gamePixels[y*2][x*2+1]=c;gamePixels[y*2+1][x*2]=c;gamePixels[y*2+1][x*2+1]=c;}}
  else if(gameMode==GAME_TETRIS){
    for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(tetrisBoard[y][x])gamePixels[y][x]=tetColors[tetrisColor[y][x]];
    for(uint8_t y=0;y<4;y++)for(uint8_t x=0;x<4;x++)if(tetBit(tetType,tetRotation,x,y)){int px=tetX+x,py=tetY+y;if(px>=0&&px<MATRIX_W&&py>=0&&py<MATRIX_H)gamePixels[py][px]=tetColors[tetType];}
    dots=min(4,1+(int)gameScore/500);dotsColor=CRGB(60,140,255);
  }
  fill_solid(leds,NUM_LEDS,CRGB::Black);for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++)leds[matrixMap[y][x]]=gamePixels[y][x];
  for(uint8_t i=0;i<4;i++)leds[110+i]=i<dots?dotsColor:CRGB::Black;
  yield();FastLED.show();
}

void overlayPixel(int x,int y,CRGB color){if(x>=0&&x<MATRIX_W&&y>=0&&y<MATRIX_H)leds[matrixMap[y][x]]=color;}

const char* const WASTE_NAMES[4]={"blau","gelb","gruen","grau"};
const char* const WASTE_WORDS[4]={"BLAUE","GELBE","GRUENE","GRAUE"};
const CRGB WASTE_COLORS[4]={CRGB(35,105,255),CRGB(255,205,20),CRGB(35,190,75),CRGB(150,160,175)};
uint8_t wasteColorCount(){return (uint8_t)__builtin_popcount(wasteMask&0x0F);}
String wasteLabel(){
  uint8_t total=wasteColorCount();String out;out.reserve(48);uint8_t count=0;
  for(uint8_t i=0;i<4;i++)if(wasteMask&(1<<i)){if(count)out+=count+1==total?" UND ":", ";out+=WASTE_WORDS[i];count++;}
  out+=count>1?" TONNEN":" TONNE";return out;
}
CRGB wasteColor(uint8_t number=0){
  uint8_t found=0;
  for(uint8_t i=0;i<4;i++)if(wasteMask&(1<<i)){if(found++==number)return WASTE_COLORS[i];}
  return WASTE_COLORS[0];
}
void setWaste(String value){
  value.toLowerCase();value.replace("ü","ue");value.replace("ö","oe");value.replace("ä","ae");value.replace(" ","");
  if(value==""||value=="off"||value=="none"||value=="clear"||value=="0"){wastePending=false;wasteTypes[0]=0;wasteMask=0;wasteScrollPending=false;return;}
  uint8_t mask=0;for(uint8_t i=0;i<4;i++)if(value.indexOf(WASTE_NAMES[i])>=0)mask|=1<<i;
  if(!mask)return;
  wasteTypes[0]=0;for(uint8_t i=0;i<4;i++)if(mask&(1<<i)){if(wasteTypes[0])strlcat(wasteTypes,",",sizeof(wasteTypes));strlcat(wasteTypes,WASTE_NAMES[i],sizeof(wasteTypes));}
  wasteMask=mask;wastePending=true;lastWasteReminderMs=0;
}
void acknowledgeWaste(){wastePending=false;wasteTypes[0]=0;wasteMask=0;wasteScrollPending=false;if(overlayMode==OVERLAY_WASTE)overlayMode=OVERLAY_NONE;}

const char* wasteDayWord(){if(!nowTmValid)return "MORGEN";return nowTm.tm_hour<12?"HEUTE":"MORGEN";}
void startWasteOverlay(){if(!wastePending)return;wasteScrollPending=true;startOverlay(OVERLAY_WASTE,8000);}
void startOverlay(OverlayMode mode,unsigned long durationMs){if(mode!=OVERLAY_WEATHER)weatherScrollPending=false;if(mode!=OVERLAY_WASTE)wasteScrollPending=false;gameMode=GAME_NONE;overlayMode=mode;overlayStartedMs=millis();overlayUntilMs=overlayStartedMs+durationMs;}
void buildClimateMask(bool humidity){
  memset(activeMask,0,sizeof(activeMask));int v=(int)round(humidity?roomHumidity:roomTemperature);v=constrain(v,-9,99);
  if(v<0){maskPixel(5,0);maskPixel(5,1);drawCompactDigit(-v,3,3);}else if(v<10)drawCompactDigit(v,3,3);else{drawCompactDigit(v/10,0,3);drawCompactDigit(v%10,4,3);}
  if(humidity){maskPixel(2,9);maskPixel(3,8);maskPixel(4,9);maskPixel(6,9);maskPixel(7,8);maskPixel(8,9);}else{maskPixel(2,8);maskPixel(2,9);maskPixel(3,8);maskPixel(3,9);maskPixel(5,9);maskPixel(5,10);maskPixel(6,9);maskPixel(7,9);maskPixel(7,10);}
}
void renderOverlay(unsigned long now){
  FastLED.setBrightness(currentBrightness);
  fill_solid(leds,NUM_LEDS,CRGB::Black);uint8_t pulse=beatsin8(18,100,255);uint8_t step=((now-overlayStartedMs)/110)%12;
  if(overlayMode==OVERLAY_WASHER||overlayMode==OVERLAY_DRYER){
    CRGB base=overlayMode==OVERLAY_WASHER?CRGB(0,145,255):CRGB(255,105,25);CRGB dim=scaleColor(base,55);

    for(int x=1;x<=9;x++){overlayPixel(x,0,dim);overlayPixel(x,9,dim);}
    for(int y=1;y<9;y++){overlayPixel(1,y,dim);overlayPixel(9,y,dim);}
    for(int x=2;x<=8;x++)overlayPixel(x,2,scaleColor(base,38));
    overlayPixel(3,1,scaleColor(base,150));overlayPixel(7,1,scaleColor(base,90));overlayPixel(8,1,scaleColor(base,pulse));
    const int8_t ring[16][2]={{5,3},{6,3},{7,4},{8,4},{8,5},{8,6},{7,7},{6,8},{5,8},{4,8},{3,7},{2,6},{2,5},{2,4},{3,4},{4,3}};
    for(uint8_t i=0;i<16;i++){uint8_t pos=overlayMode==OVERLAY_WASHER?i:15-i;uint8_t d=(pos+16-(step%16))%16;overlayPixel(ring[i][0],ring[i][1],scaleColor(base,d==0?255:(d<5?190-d*28:42)));}

    int swirlX=5+(((int)sin8((now-overlayStartedMs)/5)-128)*2)/128;
    int swirlY=6+(((int)cos8((now-overlayStartedMs)/6)-128)*1)/128;
    overlayPixel(swirlX,swirlY,scaleColor(base,210));overlayPixel(10-swirlX,11-swirlY,scaleColor(base,105));
  }else if(overlayMode==OVERLAY_DISHWASHER){

    CRGB aqua=CRGB(0,190,235),dim=scaleColor(aqua,55);
    for(int x=1;x<=9;x++){overlayPixel(x,1,dim);overlayPixel(x,9,dim);}for(int y=2;y<9;y++){overlayPixel(1,y,dim);overlayPixel(9,y,dim);}
    for(int x=2;x<=8;x++)overlayPixel(x,3,scaleColor(aqua,45));overlayPixel(3,2,scaleColor(aqua,120));overlayPixel(7,2,scaleColor(aqua,pulse));
    uint8_t arm=(now-overlayStartedMs)/115;int ax=5+((arm&1)?2:-2),ay=6+((arm&2)?1:-1);overlayPixel(5,6,CRGB::White);overlayPixel(ax,ay,scaleColor(aqua,235));overlayPixel(10-ax,12-ay,scaleColor(aqua,145));
    for(uint8_t i=0;i<4;i++){int x=2+((i*2+arm)%7),y=4+((i+arm/2)%4);overlayPixel(x,y,scaleColor(CRGB::White,110+i*28));}
  }else if(overlayMode==OVERLAY_WASTE){

    uint8_t count=wasteColorCount();if(!count)count=1;uint8_t width=count==1?5:(count==2?4:2);uint8_t gap=count==1?0:(count==2?1:1);uint8_t total=count*width+(count-1)*gap;int start=(MATRIX_W-total)/2;
    for(uint8_t n=0;n<count;n++){int left=start+n*(width+gap),right=left+width-1;CRGB c=wasteColor(n),dim=scaleColor(c,58);for(int x=left;x<=right;x++){overlayPixel(x,2,dim);overlayPixel(x,8,dim);overlayPixel(x,1,scaleColor(c,160));}for(int y=3;y<8;y++){overlayPixel(left,y,dim);overlayPixel(right,y,dim);}if(width>=4){overlayPixel(left+1,5,scaleColor(c,pulse));overlayPixel(right-1,6,scaleColor(c,170));}overlayPixel(left,9,scaleColor(c,115));overlayPixel(right,9,scaleColor(c,115));int shine=left+((now-overlayStartedMs)/150+n)%width;overlayPixel(shine,1,CRGB::White);}
  }else if(overlayMode==OVERLAY_DOORBELL){
    CRGB gold=CRGB(255,185,35);uint8_t phase=((now-overlayStartedMs)/90)%10;
    for(int y=2;y<=6;y++){int half=1+(y-2)/2;for(int x=5-half;x<=5+half;x++)overlayPixel(x,y,scaleColor(gold,pulse));}for(int x=1;x<=9;x++)overlayPixel(x,7,gold);

    int clapperX=5+(((int)sin8((now-overlayStartedMs)/3)-128)*3)/128;
    overlayPixel((5+clapperX)/2,8,scaleColor(gold,190));overlayPixel(clapperX,9,CRGB::White);
    if(phase<5){overlayPixel(1,3+phase/2,scaleColor(gold,170));overlayPixel(9,3+phase/2,scaleColor(gold,170));if(phase>2){overlayPixel(0,2,scaleColor(gold,80));overlayPixel(10,2,scaleColor(gold,80));}}
  }else if(overlayMode==OVERLAY_WEATHER){
    if(currentWeatherCode<=1){CRGB sun=CRGB(255,190,25);for(int y=3;y<=6;y++)for(int x=3;x<=7;x++)if((x-5)*(x-5)+(y-4)*(y-4)<=5)overlayPixel(x,y,scaleColor(sun,pulse));int ray=(now/180)%4;overlayPixel(5,ray?1:0,sun);overlayPixel(5,8+ray%2,sun);overlayPixel(1,4,sun);overlayPixel(9,4,sun);}
    else if(currentWeatherCode==45||currentWeatherCode==48){CRGB fog=CRGB(145,175,190);int drift=(now/280)%3;for(int x=1;x<=9;x++)if((x+drift)%3)overlayPixel(x,3,fog);for(int x=0;x<=8;x++)if((x+drift+1)%3)overlayPixel(x,5,scaleColor(fog,190));for(int x=2;x<=10;x++)if((x+drift+2)%3)overlayPixel(x,7,scaleColor(fog,135));}
    else{CRGB cloud=CRGB(130,175,220);for(int x=2;x<=8;x++)overlayPixel(x,4,cloud);for(int x=3;x<=7;x++)overlayPixel(x,3,cloud);overlayPixel(5,2,cloud);
      if(currentWeatherCode>=71&&currentWeatherCode<=86){for(int x=2;x<=8;x+=2)overlayPixel(x,6+((step+x)%3),CRGB(210,240,255));}
      else if(currentWeatherCode>=95){overlayPixel(6,5,CRGB(255,220,40));overlayPixel(5,6,CRGB(255,220,40));overlayPixel(6,6,CRGB(255,220,40));overlayPixel(5,7,CRGB::White);}
      else if((currentWeatherCode>=51&&currentWeatherCode<=67)||(currentWeatherCode>=80&&currentWeatherCode<=82))for(int x=2;x<=8;x+=2)overlayPixel(x,6+((step+x)%3),CRGB(0,120,255));}
  }else if(overlayMode==OVERLAY_ALARM){
    uint32_t duration=max(1UL,overlayUntilMs-overlayStartedMs),elapsed=now-overlayStartedMs;uint8_t progress=constrain((uint32_t)elapsed*255UL/duration,0UL,255UL);int horizon=9-(progress*10/256);
    for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++){int above=horizon-y;uint8_t v=above>=0?constrain(35+progress-above*19,5,255):constrain(15+progress/2,4,180);overlayPixel(x,y,CHSV(5+progress/7+y*2,220-progress/3,v));}
    int sunY=constrain(9-progress*8/255,1,9);for(int8_t dy=-1;dy<=1;dy++)for(int8_t dx=-1;dx<=1;dx++)if(dx*dx+dy*dy<=2)overlayPixel(5+dx,sunY+dy,CRGB(255,220,110));
  }else if(overlayMode==OVERLAY_HOLIDAY){
    uint8_t burst=(now-overlayStartedMs)/90;for(uint8_t i=0;i<7;i++){uint8_t seed=i*47+(burst/10)*31;int cx=1+seed%9,cy=1+(seed/9)%6,r=(burst+i*3)%8;for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++){int d=abs((int)x-cx)+abs((int)y-cy);if(abs(d-r)<=0)overlayPixel(x,y,CHSV(seed+i*29,220,255));}}
  }else if(overlayMode==OVERLAY_ROOM_CLIMATE){
    bool humidity=(now-overlayStartedMs)>5000&&!isnan(roomHumidity);buildClimateMask(humidity);CRGB c=humidity?CRGB(0,180,255):CRGB(255,115,35);for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++)if(activeMask[matrixMap[y][x]])overlayPixel(x,y,scaleColor(c,pulse));
  }
  yield();FastLED.show();
}
void triggerEvent(const String& payload){
  if(!cfg.notificationsEnabled)return;
  String type=payload;
  unsigned long durationMs=10000;
  int separator=type.indexOf(':');
  if(separator>=0){
    String seconds=type.substring(separator+1);
    type.remove(separator);
    if(seconds.isEmpty())return;
    for(size_t i=0;i<seconds.length();i++)if(seconds[i]<'0'||seconds[i]>'9')return;
    unsigned long value=seconds.toInt();
    if(value<1||value>600)return;
    durationMs=value*1000UL;
  }
  type.toLowerCase();
  if(type=="washer")startOverlay(OVERLAY_WASHER,durationMs);
  else if(type=="dryer")startOverlay(OVERLAY_DRYER,durationMs);
  else if(type=="dishwasher")startOverlay(OVERLAY_DISHWASHER,durationMs);
  else if(type=="doorbell")startOverlay(OVERLAY_DOORBELL,durationMs);
}
void showRoomClimate(){if(cfg.roomClimateEnabled&&(!isnan(roomTemperature)||!isnan(roomHumidity)))startOverlay(OVERLAY_ROOM_CLIMATE,10000);}
void buildTimeLoadingMask(unsigned long now){
  static const uint8_t ring[12][2]={{5,1},{7,1},{9,3},{9,5},{8,7},{6,8},{4,8},{2,7},{1,5},{1,3},{3,1},{4,1}};
  memset(activeMask,0,sizeof(activeMask));uint8_t head=(now/140)%12;for(uint8_t i=0;i<3;i++){uint8_t p=(head+12-i)%12;maskPixel(ring[p][1],ring[p][0]);}
}

void handleTransition(unsigned long now,bool clockShown){
  if(!clockShown){transitionRunning=false;return;}
  if(!cfg.transitionEnabled){transitionRunning=false;memcpy(lastClockMask,activeMask,sizeof(activeMask));lastClockValid=true;return;}
  if(lastClockValid&&memcmp(lastClockMask,activeMask,sizeof(activeMask))!=0){
    memcpy(transFrom,lastClockMask,sizeof(transFrom));memcpy(transTo,activeMask,sizeof(transTo));
    transitionRunning=true;transStartMs=now;transTypeNow=cfg.transitionType==4?1+random8(3):cfg.transitionType;
  }
  memcpy(lastClockMask,activeMask,sizeof(activeMask));lastClockValid=true;
  if(transitionRunning){
    if(now-transStartMs>=TRANSITION_MS)transitionRunning=false;
    else for(uint8_t i=0;i<NUM_LEDS;i++)activeMask[i]=transFrom[i]||transTo[i];
  }
}

uint8_t transitionAlpha(uint8_t i,uint8_t x,uint8_t y,uint8_t prog){
  bool inFrom=transFrom[i],inTo=transTo[i];
  if(inFrom==inTo)return inTo?255:0;
  uint8_t a;
  if(transTypeNow==2){int edge=((int)prog*(MATRIX_W+2))/255-1;a=(int)x<edge?255:0;}
  else if(transTypeNow==3){int front=((int)prog*(MATRIX_H+6))/255-((x*7)%5);a=(int)y<front?255:0;}
  else a=prog;
  return inTo?a:255-a;
}
uint8_t transitionFadeAlpha(uint8_t i,uint8_t prog){
  bool inFrom=transFrom[i],inTo=transTo[i];
  if(inFrom==inTo)return inTo?255:0;
  return inTo?prog:255-prog;
}

void updateCircadian(unsigned long now){
  if(now-lastCircadianMs<20000UL)return;lastCircadianMs=now;
  CRGB target(255,255,255);
  if(cfg.circadianEnabled&&nowTmValid){
    int nowMin=nowTm.tm_hour*60+nowTm.tm_min;
    int sr=sunriseMinutes>=0?sunriseMinutes:420,ss=sunsetMinutes>=0?sunsetMinutes:1140;
    const CRGB night(255,150,70),morning(225,238,255),neutral(255,255,255),evening(255,185,110);
    int pt[6]={sr-30,sr+90,(sr+ss)/2,ss-120,ss+30,ss+120};
    const CRGB* pc[6]={&night,&morning,&neutral,&neutral,&evening,&night};
    if(nowMin<pt[0]||nowMin>=pt[5])target=night;
    else for(uint8_t i=0;i<5;i++)if(nowMin<pt[i+1]){int span=pt[i+1]-pt[i];uint8_t amt=span>0?(uint8_t)(((long)(nowMin-pt[i])*255L)/span):255;target=blend(*pc[i],*pc[i+1],amt);break;}
  }
  tintNow=target;FastLED.setTemperature(tintNow);
}

void renderFrame() {
  unsigned long now=millis();updateEffectPhase(now);updateEffectState(now);
  if(overlayMode!=OVERLAY_NONE){renderOverlay(now);return;}
  if(gameMode!=GAME_NONE){renderGame();return;}
  bool clockShown=false;
  if(displayMode==MODE_SCROLL)buildScrollMask(); else if(displayMode==MODE_TEMPERATURE&&weatherValid)buildTemperatureMask(); else {
    if(!nowTmValid)buildTimeLoadingMask(now);else{buildClockMask(nowTm.tm_hour,nowTm.tm_min);clockShown=true;}
  }
  handleTransition(now,clockShown);
  fill_solid(leds,NUM_LEDS,CRGB::Black);

  FastLED.setBrightness(currentBrightness);

  CRGB fg=fgBase(),bg=bgBase(),minute=minuteBase();uint8_t fgHue=rgb2hsv_approximate(fg).hue,bgHue=rgb2hsv_approximate(bg).hue,minuteHue=rgb2hsv_approximate(minute).hue;
  uint8_t fgFx=curFgEffect(),bgFx=curBgEffect();
  uint8_t transProg=0;if(transitionRunning){unsigned long el=now-transStartMs;transProg=el>=TRANSITION_MS?255:(uint8_t)(el*255UL/TRANSITION_MS);}
  auto bgAt=[&](uint8_t x,uint8_t y)->CRGB{CRGB pb=paletteColor(cfg.backgroundPalette,bg,(uint8_t)(backgroundPhase+x*14+y*9));return scaleColor(effectColor((Effect)bgFx,x,y,pb,rgb2hsv_approximate(pb).hue,cfg.backgroundSpeed,backgroundPhase,cfg.backgroundIntensity,cfg.backgroundTrail,cfg.backgroundSize),cfg.backgroundLevel);};
  for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++){
    uint8_t i=matrixMap[y][x];
    if(activeMask[i]){
      CRGB pfg=paletteColor(cfg.foregroundPalette,fg,(uint8_t)(foregroundPhase+x*17+y*11));
      CRGB wordColor=displayMode==MODE_SCROLL?pfg:effectColor((Effect)fgFx,x,y,pfg,rgb2hsv_approximate(pfg).hue,cfg.foregroundSpeed,foregroundPhase,cfg.foregroundIntensity,cfg.foregroundTrail,cfg.foregroundSize);
      if(cfg.ambientMode&&clockShown)wordColor=scaleColor(wordColor,cfg.ambientWordLevel);

      if(displayMode==MODE_SCROLL||fgFx==FX_SOLID)wordColor=readableWordColor(wordColor,fg);
      if(transitionRunning){
        uint8_t a=transitionAlpha(i,x,y,transProg);
        wordColor=scaleColor(wordColor,a);
        if(a<255&&cfg.backgroundLevel)wordColor+=scaleColor(bgAt(x,y),255-a);
      }
      leds[i]=wordColor;
    }else if(displayMode!=MODE_SCROLL&&cfg.backgroundLevel)leds[i]=bgAt(x,y);
  }

  if(transitionRunning&&transTypeNow==3){for(uint8_t x=0;x<MATRIX_W;x++){int front=((int)transProg*(MATRIX_H+6))/255-((x*7)%5);if(front>=0&&front<MATRIX_H)leds[matrixMap[front][x]]=scaleColor(CRGB(170,255,200),200);}}
  if(transitionRunning&&transTypeNow==2){int edge=((int)transProg*(MATRIX_W+2))/255-1;if(edge>=0&&edge<MATRIX_W)for(uint8_t y=0;y<MATRIX_H;y++)leds[matrixMap[y][edge]]=scaleColor(blend(fg,CRGB::White,160),200);}

  CRGB pmin=paletteColor(cfg.minutePalette,minute,(uint8_t)minutePhase);
  CRGB minuteFrame=scaleColor(effectColor((Effect)cfg.minuteEffect,5,5,pmin,rgb2hsv_approximate(pmin).hue,cfg.minuteSpeed,minutePhase,cfg.minuteIntensity,cfg.minuteTrail,cfg.minuteSize),cfg.minuteLevel);
  for(uint8_t i=110;i<114;i++)if(activeMask[i])leds[i]=transitionRunning?scaleColor(minuteFrame,transitionFadeAlpha(i,transProg)):minuteFrame;

  if(wastePending&&overlayMode==OVERLAY_NONE){uint8_t glow=beatsin8(12,40,210);leds[matrixMap[0][10]]=scaleColor(wasteColor((now/1800UL)%max((uint8_t)1,wasteColorCount())),glow);}
  if(countdownActive&&countdownDurationMs){uint32_t remaining=(long)(countdownEndMs-now)>0?countdownEndMs-now:0;uint8_t dots=min((uint32_t)4,(uint32_t)((remaining*4UL+countdownDurationMs-1)/countdownDurationMs));for(uint8_t i=0;i<4;i++)leds[110+i]=i<dots?minuteFrame:CRGB::Black;}

  for(uint8_t i=0;i<NUM_LEDS;i++){
    leds[i].r=scale8_video(leds[i].r,cfg.calibrationR);leds[i].g=scale8_video(leds[i].g,cfg.calibrationG);leds[i].b=scale8_video(leds[i].b,cfg.calibrationB);
    if(cfg.gammaEnabled){leds[i].r=scale8_video(leds[i].r,leds[i].r);leds[i].g=scale8_video(leds[i].g,leds[i].g);leds[i].b=scale8_video(leds[i].b,leds[i].b);}
  }
  yield();FastLED.show();renderedLedFrames++;
}

float jsonNumber(const String& json,const String& key,int occurrence=0) {
  int pos=0;for(int i=0;i<=occurrence;i++){pos=json.indexOf("\""+key+"\"",pos);if(pos<0)return NAN;pos+=key.length()+2;}
  pos=json.indexOf(':',pos)+1;return json.substring(pos).toFloat();
}
float jsonArrayNumber(const String& json,const String& key,uint8_t index) {
  int p=json.indexOf("\""+key+"\"");if(p<0)return NAN;p=json.indexOf('[',p)+1;
  for(uint8_t i=0;i<index;i++){p=json.indexOf(',',p)+1;if(p<=0)return NAN;}return json.substring(p).toFloat();
}

int jsonArrayTimeMinutes(const String& json,const String& key){
  int p=json.indexOf("\""+key+"\"");if(p<0)return -1;p=json.indexOf('[',p);if(p<0)return -1;p=json.indexOf('T',p);if(p<0||p+6>(int)json.length())return -1;
  return json.substring(p+1,p+3).toInt()*60+json.substring(p+4,p+6).toInt();
}
const char* weatherName(int code){
  if(code==0)return "KLAR";if(code==1)return "LEICHT BEWOELKT";if(code==2)return "BEWOELKT";if(code==3)return "BEDECKT";if(code==45||code==48)return "NEBEL";
  if(code>=51&&code<=57)return "NIESELREGEN";if(code>=61&&code<=67)return "REGEN";if(code>=71&&code<=77)return "SCHNEE";
  if(code>=80&&code<=82)return "SCHAUER";if(code>=85&&code<=86)return "SCHNEESCHAUER";if(code>=95)return "GEWITTER";return "WECHSELHAFT";
}
void rebuildWeatherText(){
  String place=weatherRuntimeLocation.length()?weatherRuntimeLocation:String("STANDORT");place.toUpperCase();
  weatherText=place+"  JETZT "+String((int)round(currentTemperature))+" GRAD  "+weatherName(currentWeatherCode)+"  HEUTE "+String((int)round(todayMin))+" BIS "+String((int)round(todayMax))+" GRAD  "+weatherName(todayWeatherCode)+"  MORGEN "+String((int)round(tomorrowMin))+" BIS "+String((int)round(tomorrowMax))+" GRAD  "+weatherName(tomorrowWeatherCode)+"   ";
}
bool fetchWeatherBlocking(const WeatherTaskData& request,WeatherTaskData& result) {
  if(WiFi.status()!=WL_CONNECTED)return false;
  WiFiClientSecure client;client.setInsecure();HTTPClient http;
  float lat=request.lat,lon=request.lon;String location=request.location;
  String url="https://api.open-meteo.com/v1/forecast?latitude="+String(lat,5)+"&longitude="+String(lon,5)+"&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto&forecast_days=2";
  if(!http.begin(client,url))return false;http.setTimeout(7000);int code=http.GET();if(code!=HTTP_CODE_OK){http.end();return false;}
  String json=http.getString();http.end();
  int currentPos=json.indexOf("\"current\"");int dailyUnitsPos=json.indexOf("\"daily_units\"",currentPos);int dailyPos=json.indexOf("\"daily\":");
  if(currentPos<0||dailyUnitsPos<0||dailyPos<0)return false;String current=json.substring(currentPos,dailyUnitsPos);String daily=json.substring(dailyPos);
  float curTemp=jsonNumber(current,"temperature_2m"),tMax=jsonArrayNumber(daily,"temperature_2m_max",0),nMax=jsonArrayNumber(daily,"temperature_2m_max",1),tMin=jsonArrayNumber(daily,"temperature_2m_min",0),nMin=jsonArrayNumber(daily,"temperature_2m_min",1);
  int curCode=(int)jsonNumber(current,"weather_code"),tCode=(int)jsonArrayNumber(daily,"weather_code",0),nCode=(int)jsonArrayNumber(daily,"weather_code",1);
  if(isnan(curTemp)||isnan(tMax)||isnan(tMin)||isnan(nMax)||isnan(nMin))return false;
  result=request;result.currentTemp=curTemp;result.todayMax=tMax;result.tomorrowMax=nMax;result.todayMin=tMin;result.tomorrowMin=nMin;result.currentCode=curCode;result.todayCode=tCode;result.tomorrowCode=nCode;
  result.sunriseMin=jsonArrayTimeMinutes(daily,"sunrise");result.sunsetMin=jsonArrayTimeMinutes(daily,"sunset");return true;
}
void weatherTask(void*){
  WeatherTaskData request,result;portENTER_CRITICAL(&weatherTaskMux);request=weatherTaskRequest;portEXIT_CRITICAL(&weatherTaskMux);
  bool ok=fetchWeatherBlocking(request,result);
  portENTER_CRITICAL(&weatherTaskMux);if(ok)weatherTaskResult=result;weatherTaskResultOk=ok;weatherTaskResultReady=true;weatherTaskRunning=false;portEXIT_CRITICAL(&weatherTaskMux);
  vTaskDelete(nullptr);
}
void requestWeather(bool showAfter=false){
  if(showAfter)weatherShowAfterFetch=true;if(weatherTaskRunning||cfg.weatherSource!=0||WiFi.status()!=WL_CONNECTED)return;
  portENTER_CRITICAL(&weatherTaskMux);weatherTaskRequest.lat=cfg.weatherLat;weatherTaskRequest.lon=cfg.weatherLon;strlcpy(weatherTaskRequest.location,cfg.weatherLocation,sizeof(weatherTaskRequest.location));weatherTaskRunning=true;portEXIT_CRITICAL(&weatherTaskMux);
  lastWeatherMs=millis();nextWeatherAttemptMs=lastWeatherMs+300000UL;
  if(xTaskCreatePinnedToCore(weatherTask,"weather",12288,nullptr,1,nullptr,0)!=pdPASS){portENTER_CRITICAL(&weatherTaskMux);weatherTaskRunning=false;weatherTaskResultOk=false;weatherTaskResultReady=true;portEXIT_CRITICAL(&weatherTaskMux);}
}
bool applyMqttWeather(const String& json){
  float t=jsonNumber(json,"temperature");if(isnan(t))t=jsonNumber(json,"temp");if(isnan(t))return false;
  float wc=jsonNumber(json,"weather_code");if(isnan(wc))wc=jsonNumber(json,"code");
  float tMin=jsonNumber(json,"today_min"),tMax=jsonNumber(json,"today_max"),tCode=jsonNumber(json,"today_code");
  float nMin=jsonNumber(json,"tomorrow_min"),nMax=jsonNumber(json,"tomorrow_max"),nCode=jsonNumber(json,"tomorrow_code");
  currentTemperature=t;currentWeatherCode=isnan(wc)?0:(int)wc;
  todayMin=isnan(tMin)?t:tMin;todayMax=isnan(tMax)?t:tMax;todayWeatherCode=isnan(tCode)?currentWeatherCode:(int)tCode;
  tomorrowMin=isnan(nMin)?todayMin:nMin;tomorrowMax=isnan(nMax)?todayMax:nMax;tomorrowWeatherCode=isnan(nCode)?todayWeatherCode:(int)nCode;
  String location=jsonString(json,"location","");weatherRuntimeLocation=location.length()?location:String(cfg.weatherLocation);
  weatherValid=true;lastWeatherMs=millis();rebuildWeatherText();mqttDirtyMask|=MD_WEATHER;return true;
}
String displayText(String text){text.replace("ä","AE");text.replace("ö","OE");text.replace("ü","UE");text.replace("Ä","AE");text.replace("Ö","OE");text.replace("Ü","UE");text.replace("ß","SS");text.toUpperCase();return text.substring(0,300);}
void startScroll(const String& text){gameMode=GAME_NONE;scrollText=displayText(text)+"   ";scrollX=MATRIX_W;displayMode=MODE_SCROLL;lastScrollStepMs=millis();}
void enqueueMessage(const String& text,uint8_t priority=1){
  String clean=displayText(text);if(!clean.length())return;uint8_t pos=messageQueueCount;
  for(uint8_t i=0;i<messageQueueCount;i++)if(priority>messageQueue[i].priority){pos=i;break;}
  if(messageQueueCount<6)messageQueueCount++;else if(pos>=6)return;
  for(int i=messageQueueCount-1;i>(int)pos;i--)messageQueue[i]=messageQueue[i-1];strlcpy(messageQueue[pos].text,clean.c_str(),sizeof(messageQueue[pos].text));messageQueue[pos].priority=priority;
}
void processMessageQueue(){if(!messageQueueCount||gameMode!=GAME_NONE||overlayMode!=OVERLAY_NONE||displayMode==MODE_SCROLL)return;String text=messageQueue[0].text;for(uint8_t i=1;i<messageQueueCount;i++)messageQueue[i-1]=messageQueue[i];messageQueueCount--;startScroll(text);}
void startWeatherScroll(){if(!weatherValid){requestWeather(true);enqueueMessage("WETTER WIRD GELADEN",2);return;}if(cfg.weatherAnimationsEnabled){weatherScrollPending=true;startOverlay(OVERLAY_WEATHER,5000);}else enqueueMessage(weatherText,2);}

String mqttTopic(const char* suffix){return String(cfg.mqttBase)+"/"+suffix;}
void mqttPublish(const String& topic,const String& payload,bool retained=true){if(mqttClient.connected())mqttClient.publish(topic,payload,retained,0);}
String haDevice(){return "\"device\":{\"identifiers\":[\"wortuhr_stefan\"],\"name\":\"Wortuhr von ihmSelbst\",\"manufacturer\":\"ihmSelbst\",\"model\":\"Wortuhr ESP32 3.2\"}";}
void publishHaConfig(const char* component,const char* objectId,const String& payload){mqttPublish(String("homeassistant/")+component+"/wortuhr/"+objectId+"/config",payload,true);}
void publishFeatureSwitch(const String& b,const String& d,const char* key,const char* name){publishHaConfig("switch",key,"{\"name\":\""+String(name)+"\",\"unique_id\":\"wortuhr_"+key+"\",\"command_topic\":\""+b+"/feature/"+key+"/set\",\"state_topic\":\""+b+"/feature/"+key+"/state\",\"availability_topic\":\""+b+"/status\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","+d+"}");}
String effectOptionsJson(){uint8_t order[FX_COUNT];for(uint8_t i=0;i<FX_COUNT;i++)order[i]=i;for(uint8_t i=0;i<FX_COUNT-1;i++)for(uint8_t j=i+1;j<FX_COUNT;j++)if(strcmp(effectName(order[j]),effectName(order[i]))<0){uint8_t t=order[i];order[i]=order[j];order[j]=t;}String out;out.reserve(900);out="[";for(uint8_t i=0;i<FX_COUNT;i++){if(i)out+=",";out+="\"";out+=effectName(order[i]);out+="\"";}return out+"]";}
void mqttPublishDiscovery(){
  if(!cfg.mqttDiscovery||!mqttClient.connected())return;String b=cfg.mqttBase,d=haDevice();
  publishHaConfig("switch","power","{\"name\":\"Ein/Aus\",\"unique_id\":\"wortuhr_power\",\"command_topic\":\""+b+"/power/set\",\"state_topic\":\""+b+"/power/state\",\"availability_topic\":\""+b+"/status\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","+d+"}");
  publishHaConfig("switch","temperature_mode","{\"name\":\"Temperatur zyklisch anzeigen\",\"unique_id\":\"wortuhr_temperature_mode\",\"command_topic\":\""+b+"/temperature_mode/set\",\"state_topic\":\""+b+"/temperature_mode/state\",\"availability_topic\":\""+b+"/status\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","+d+"}");
  publishHaConfig("button","weather","{\"name\":\"Wettervorhersage\",\"unique_id\":\"wortuhr_weather\",\"command_topic\":\""+b+"/weather/set\",\"payload_press\":\"PRESS\",\"availability_topic\":\""+b+"/status\","+d+"}");
  String options=effectOptionsJson();
  publishHaConfig("select","foreground_effect","{\"name\":\"Wort-Effekt\",\"unique_id\":\"wortuhr_fg_effect\",\"command_topic\":\""+b+"/foreground_effect/set\",\"state_topic\":\""+b+"/foreground_effect/state\",\"options\":"+options+",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("select","background_effect","{\"name\":\"Hintergrund-Effekt\",\"unique_id\":\"wortuhr_bg_effect\",\"command_topic\":\""+b+"/background_effect/set\",\"state_topic\":\""+b+"/background_effect/state\",\"options\":"+options+",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("select","minute_effect","{\"name\":\"Minuten-Effekt\",\"unique_id\":\"wortuhr_minute_effect\",\"command_topic\":\""+b+"/minute_effect/set\",\"state_topic\":\""+b+"/minute_effect/state\",\"options\":"+options+",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","minute_level","{\"name\":\"Minutenstärke\",\"unique_id\":\"wortuhr_minute_level\",\"command_topic\":\""+b+"/minute_level/set\",\"state_topic\":\""+b+"/minute_level/state\",\"min\":0,\"max\":255,\"step\":1,\"mode\":\"slider\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","foreground_speed","{\"name\":\"Wort-Geschwindigkeit\",\"unique_id\":\"wortuhr_fg_speed\",\"command_topic\":\""+b+"/foreground_speed/set\",\"state_topic\":\""+b+"/foreground_speed/state\",\"min\":1,\"max\":100,\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","background_speed","{\"name\":\"Hintergrund-Geschwindigkeit\",\"unique_id\":\"wortuhr_bg_speed\",\"command_topic\":\""+b+"/background_speed/set\",\"state_topic\":\""+b+"/background_speed/state\",\"min\":1,\"max\":100,\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","minute_speed","{\"name\":\"Minuten-Geschwindigkeit\",\"unique_id\":\"wortuhr_min_speed\",\"command_topic\":\""+b+"/minute_speed/set\",\"state_topic\":\""+b+"/minute_speed/state\",\"min\":1,\"max\":100,\"availability_topic\":\""+b+"/status\","+d+"}");
  String palettes="[\"Eigene Farbe\",\"Rainbow\",\"Party\",\"Ocean\",\"Forest\",\"Lava\",\"Sunset\",\"Ice\",\"Heat\",\"Retro\",\"Aurora\",\"Pastell\"]";
  publishHaConfig("select","foreground_palette","{\"name\":\"Wort-Palette\",\"unique_id\":\"wortuhr_fg_palette\",\"command_topic\":\""+b+"/foreground_palette/set\",\"state_topic\":\""+b+"/foreground_palette/state\",\"options\":"+palettes+",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("select","background_palette","{\"name\":\"Hintergrund-Palette\",\"unique_id\":\"wortuhr_bg_palette\",\"command_topic\":\""+b+"/background_palette/set\",\"state_topic\":\""+b+"/background_palette/state\",\"options\":"+palettes+",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","background_level","{\"name\":\"Hintergrundstärke\",\"unique_id\":\"wortuhr_bg_level\",\"command_topic\":\""+b+"/background_level/set\",\"state_topic\":\""+b+"/background_level/state\",\"min\":0,\"max\":160,\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","power_limit","{\"name\":\"LED-Stromlimit\",\"unique_id\":\"wortuhr_power_limit\",\"command_topic\":\""+b+"/power_limit/set\",\"state_topic\":\""+b+"/power_limit/state\",\"min\":500,\"max\":5000,\"step\":100,\"unit_of_measurement\":\"mA\",\"entity_category\":\"config\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("light","light","{\"name\":\"Wortuhr Licht\",\"unique_id\":\"wortuhr_light\",\"schema\":\"json\",\"command_topic\":\""+b+"/light/set\",\"state_topic\":\""+b+"/light/state\",\"brightness\":true,\"supported_color_modes\":[\"rgb\"],\"effect\":true,\"effect_list\":"+options+",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("select","weather_source","{\"name\":\"Wetterquelle\",\"unique_id\":\"wortuhr_weather_source\",\"command_topic\":\""+b+"/weather/source/set\",\"state_topic\":\""+b+"/weather/source/state\",\"options\":[\"Open-Meteo\",\"Home Assistant MQTT\"],\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("select","game","{\"name\":\"Spiel\",\"unique_id\":\"wortuhr_game\",\"command_topic\":\""+b+"/game/set\",\"state_topic\":\""+b+"/game/state\",\"options\":[\"Uhr\",\"Pong\",\"Pong 2P\",\"Tetris\",\"Snake\",\"Flappy Bird\",\"Breakout\",\"Dino Run\",\"Space Invaders\",\"Vier gewinnt\",\"Vier gewinnt 2P\",\"Game of Life\",\"Simon Says\",\"Frogger\",\"Asteroids\",\"Labyrinth\",\"2048\"],\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("select","preset","{\"name\":\"Szene\",\"unique_id\":\"wortuhr_preset\",\"command_topic\":\""+b+"/preset/set\",\"state_topic\":\""+b+"/preset/state\",\"options\":[\"Normal\",\"Matrix\",\"Ruhe\",\"Party\",\"Weihnachten\",\"Wetter\"],\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("text","message","{\"name\":\"Laufschrift\",\"unique_id\":\"wortuhr_message\",\"command_topic\":\""+b+"/message/set\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","temperature","{\"name\":\"Außentemperatur\",\"unique_id\":\"wortuhr_temperature\",\"state_topic\":\""+b+"/temperature/state\",\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"state_class\":\"measurement\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","ip","{\"name\":\"IP-Adresse\",\"unique_id\":\"wortuhr_ip\",\"state_topic\":\""+b+"/ip/state\",\"entity_category\":\"diagnostic\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","heap","{\"name\":\"Freier Heap\",\"unique_id\":\"wortuhr_heap\",\"state_topic\":\""+b+"/diagnostic/heap\",\"unit_of_measurement\":\"B\",\"entity_category\":\"diagnostic\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","uptime","{\"name\":\"Uptime\",\"unique_id\":\"wortuhr_uptime\",\"state_topic\":\""+b+"/diagnostic/uptime\",\"unit_of_measurement\":\"s\",\"device_class\":\"duration\",\"entity_category\":\"diagnostic\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","rssi","{\"name\":\"WLAN RSSI\",\"unique_id\":\"wortuhr_rssi\",\"state_topic\":\""+b+"/diagnostic/rssi\",\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\",\"entity_category\":\"diagnostic\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","reset_reason","{\"name\":\"Reset-Grund\",\"unique_id\":\"wortuhr_reset_reason\",\"state_topic\":\""+b+"/diagnostic/reset_reason\",\"entity_category\":\"diagnostic\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("number","timer","{\"name\":\"Countdown\",\"unique_id\":\"wortuhr_timer\",\"command_topic\":\""+b+"/timer/set\",\"state_topic\":\""+b+"/timer/state\",\"min\":0,\"max\":86400,\"unit_of_measurement\":\"s\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","room_temperature","{\"name\":\"Raumtemperatur-Quelle\",\"unique_id\":\"wortuhr_room_temperature\",\"state_topic\":\""+b+"/room_temperature/state\",\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","room_humidity","{\"name\":\"Raumfeuchte-Quelle\",\"unique_id\":\"wortuhr_room_humidity\",\"state_topic\":\""+b+"/room_humidity/state\",\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","highscore_snake","{\"name\":\"Highscore Snake\",\"unique_id\":\"wortuhr_highscore_snake\",\"state_topic\":\""+b+"/highscore/snake\",\"icon\":\"mdi:snake\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","highscore_tetris","{\"name\":\"Highscore Tetris\",\"unique_id\":\"wortuhr_highscore_tetris\",\"state_topic\":\""+b+"/highscore/tetris\",\"icon\":\"mdi:view-grid\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","highscore_pong","{\"name\":\"Highscore Pong\",\"unique_id\":\"wortuhr_highscore_pong\",\"state_topic\":\""+b+"/highscore/pong\",\"icon\":\"mdi:table-tennis\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("sensor","highscore_flappy","{\"name\":\"Highscore Flappy Bird\",\"unique_id\":\"wortuhr_highscore_flappy\",\"state_topic\":\""+b+"/highscore/flappy\",\"icon\":\"mdi:bird\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("button","room_climate","{\"name\":\"Raumklima anzeigen\",\"unique_id\":\"wortuhr_room_climate_show\",\"command_topic\":\""+b+"/climate/show\",\"payload_press\":\"PRESS\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("button","dishwasher","{\"name\":\"Spülmaschine anzeigen\",\"unique_id\":\"wortuhr_dishwasher\",\"command_topic\":\""+b+"/event/set\",\"payload_press\":\"dishwasher\",\"icon\":\"mdi:dishwasher\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("text","waste","{\"name\":\"Mülltonnen-Erinnerung\",\"unique_id\":\"wortuhr_waste\",\"command_topic\":\""+b+"/waste/set\",\"state_topic\":\""+b+"/waste/state\",\"icon\":\"mdi:trash-can-outline\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("button","waste_show","{\"name\":\"Mülltonnen jetzt anzeigen\",\"unique_id\":\"wortuhr_waste_show\",\"command_topic\":\""+b+"/waste/show\",\"payload_press\":\"PRESS\",\"icon\":\"mdi:trash-can\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishHaConfig("button","waste_ack","{\"name\":\"Tonnen rausgestellt\",\"unique_id\":\"wortuhr_waste_ack\",\"command_topic\":\""+b+"/waste/ack/set\",\"payload_press\":\"ON\",\"icon\":\"mdi:check-circle\",\"availability_topic\":\""+b+"/status\","+d+"}");
  publishFeatureSwitch(b,d,"preview","Live-Vorschau");publishFeatureSwitch(b,d,"presets","Presets");publishFeatureSwitch(b,d,"notifications","Benachrichtigungen");publishFeatureSwitch(b,d,"calendar","Kalender");publishFeatureSwitch(b,d,"games","Spiele");publishFeatureSwitch(b,d,"highscores","Highscores");publishFeatureSwitch(b,d,"weather_animations","Wetteranimationen");publishFeatureSwitch(b,d,"room_climate","Raumklima");
  for(const FeatureDef& f:newFeatures)publishFeatureSwitch(b,d,f.key,f.name);
  mqttDiscoveryPublished=true;
}
String lightStateJson(){String j;j.reserve(150);j="{\"state\":\"";j+=cfg.power?"ON":"OFF";j+="\",\"brightness\":"+String(cfg.brightnessMax)+",\"color\":{\"r\":"+String(cfg.fgR)+",\"g\":"+String(cfg.fgG)+",\"b\":"+String(cfg.fgB)+"},\"effect\":\""+String(effectName(cfg.foregroundEffect))+"\"}";return j;}
void mqttPublishExtra(){
  if(!mqttClient.connected())return;
  for(const FeatureDef& f:newFeatures)mqttPublish(mqttTopic((String("feature/")+f.key+"/state").c_str()),*f.value?"ON":"OFF");
}
void mqttPublishState(){
  if(!mqttClient.connected())return;mqttPublish(mqttTopic("status"),"online");mqttPublish(mqttTopic("power/state"),cfg.power?"ON":"OFF");mqttPublish(mqttTopic("temperature_mode/state"),cfg.showTemperature?"ON":"OFF");
  mqttPublish(mqttTopic("foreground_effect/state"),effectName(cfg.foregroundEffect));mqttPublish(mqttTopic("background_effect/state"),effectName(cfg.backgroundEffect));mqttPublish(mqttTopic("minute_effect/state"),effectName(cfg.minuteEffect));mqttPublish(mqttTopic("foreground_palette/state"),paletteName(cfg.foregroundPalette));mqttPublish(mqttTopic("background_palette/state"),paletteName(cfg.backgroundPalette));mqttPublish(mqttTopic("minute_level/state"),String(cfg.minuteLevel));mqttPublish(mqttTopic("foreground_speed/state"),String(cfg.foregroundSpeed));mqttPublish(mqttTopic("background_speed/state"),String(cfg.backgroundSpeed));mqttPublish(mqttTopic("minute_speed/state"),String(cfg.minuteSpeed));mqttPublish(mqttTopic("background_level/state"),String(cfg.backgroundLevel));mqttPublish(mqttTopic("power_limit/state"),String(cfg.powerLimitMa));mqttPublish(mqttTopic("weather/source/state"),cfg.weatherSource?"Home Assistant MQTT":"Open-Meteo");mqttPublish(mqttTopic("game/state"),gameName());mqttPublish(mqttTopic("preset/state"),presetName(cfg.activePreset));mqttPublish(mqttTopic("ip/state"),WiFi.localIP().toString());if(weatherValid)mqttPublish(mqttTopic("temperature/state"),String(currentTemperature,1));
  mqttPublish(mqttTopic("light/state"),lightStateJson());
  mqttPublish(mqttTopic("diagnostic/heap"),String(ESP.getFreeHeap()));mqttPublish(mqttTopic("diagnostic/uptime"),String(millis()/1000UL));mqttPublish(mqttTopic("diagnostic/rssi"),String(WiFi.RSSI()));mqttPublish(mqttTopic("diagnostic/reset_reason"),resetReasonName());mqttPublish(mqttTopic("timer/state"),countdownActive?String(max(0L,(long)(countdownEndMs-millis())/1000L)):"0");
  mqttPublish(mqttTopic("feature/preview/state"),cfg.previewEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/presets/state"),cfg.presetsEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/notifications/state"),cfg.notificationsEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/calendar/state"),cfg.calendarEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/games/state"),cfg.gamesEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/highscores/state"),cfg.highScoresEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/weather_animations/state"),cfg.weatherAnimationsEnabled?"ON":"OFF");mqttPublish(mqttTopic("feature/room_climate/state"),cfg.roomClimateEnabled?"ON":"OFF");
  mqttPublishExtra();
  if(!isnan(roomTemperature))mqttPublish(mqttTopic("room_temperature/state"),String(roomTemperature,1));if(!isnan(roomHumidity))mqttPublish(mqttTopic("room_humidity/state"),String(roomHumidity,1));
  mqttPublish(mqttTopic("highscore/snake"),String(cfg.highSnake));mqttPublish(mqttTopic("highscore/tetris"),String(cfg.highTetris));mqttPublish(mqttTopic("highscore/pong"),String(cfg.highPong));mqttPublish(mqttTopic("highscore/flappy"),String(cfg.highFlappy));
  mqttPublish(mqttTopic("waste/state"),wastePending?String(wasteTypes):"none");
  mqttDirtyMask=0;
}
void mqttPublishDirtyState(uint32_t dirty){
  if(!mqttClient.connected())return;
  if(dirty&MD_POWER)mqttPublish(mqttTopic("power/state"),cfg.power?"ON":"OFF");if(dirty&MD_TEMP_MODE)mqttPublish(mqttTopic("temperature_mode/state"),cfg.showTemperature?"ON":"OFF");
  if(dirty&MD_FG)mqttPublish(mqttTopic("foreground_effect/state"),effectName(cfg.foregroundEffect));if(dirty&MD_BG)mqttPublish(mqttTopic("background_effect/state"),effectName(cfg.backgroundEffect));
  if(dirty&MD_MIN_FX)mqttPublish(mqttTopic("minute_effect/state"),effectName(cfg.minuteEffect));if(dirty&MD_MIN_LEVEL)mqttPublish(mqttTopic("minute_level/state"),String(cfg.minuteLevel));
  if(dirty&MD_WEATHER_SOURCE)mqttPublish(mqttTopic("weather/source/state"),cfg.weatherSource?"Home Assistant MQTT":"Open-Meteo");if((dirty&MD_WEATHER)&&weatherValid)mqttPublish(mqttTopic("temperature/state"),String(currentTemperature,1));
  if(dirty&MD_GAME)mqttPublish(mqttTopic("game/state"),gameName());if(dirty&MD_PRESET)mqttPublish(mqttTopic("preset/state"),presetName(cfg.activePreset));
  if((dirty&MD_ROOM_TEMP)&&!isnan(roomTemperature))mqttPublish(mqttTopic("room_temperature/state"),String(roomTemperature,1));if((dirty&MD_ROOM_HUM)&&!isnan(roomHumidity))mqttPublish(mqttTopic("room_humidity/state"),String(roomHumidity,1));
  if(dirty&MD_F_PREVIEW)mqttPublish(mqttTopic("feature/preview/state"),cfg.previewEnabled?"ON":"OFF");if(dirty&MD_F_PRESETS)mqttPublish(mqttTopic("feature/presets/state"),cfg.presetsEnabled?"ON":"OFF");
  if(dirty&MD_F_NOTIFICATIONS)mqttPublish(mqttTopic("feature/notifications/state"),cfg.notificationsEnabled?"ON":"OFF");if(dirty&MD_F_CALENDAR)mqttPublish(mqttTopic("feature/calendar/state"),cfg.calendarEnabled?"ON":"OFF");
  if(dirty&MD_F_GAMES)mqttPublish(mqttTopic("feature/games/state"),cfg.gamesEnabled?"ON":"OFF");if(dirty&MD_F_HIGHSCORES)mqttPublish(mqttTopic("feature/highscores/state"),cfg.highScoresEnabled?"ON":"OFF");
  if(dirty&MD_F_WEATHER_ANIM)mqttPublish(mqttTopic("feature/weather_animations/state"),cfg.weatherAnimationsEnabled?"ON":"OFF");if(dirty&MD_F_ROOM_CLIMATE)mqttPublish(mqttTopic("feature/room_climate/state"),cfg.roomClimateEnabled?"ON":"OFF");
  if(dirty&MD_EXTRA)mqttPublishExtra();
  if(dirty&MD_HIGH_SNAKE)mqttPublish(mqttTopic("highscore/snake"),String(cfg.highSnake));if(dirty&MD_HIGH_TETRIS)mqttPublish(mqttTopic("highscore/tetris"),String(cfg.highTetris));
  if(dirty&MD_HIGH_PONG)mqttPublish(mqttTopic("highscore/pong"),String(cfg.highPong));if(dirty&MD_HIGH_FLAPPY)mqttPublish(mqttTopic("highscore/flappy"),String(cfg.highFlappy));
  if(dirty&MD_FG_SPEED)mqttPublish(mqttTopic("foreground_speed/state"),String(cfg.foregroundSpeed));if(dirty&MD_BG_SPEED)mqttPublish(mqttTopic("background_speed/state"),String(cfg.backgroundSpeed));if(dirty&MD_MIN_SPEED)mqttPublish(mqttTopic("minute_speed/state"),String(cfg.minuteSpeed));if(dirty&MD_BG_LEVEL)mqttPublish(mqttTopic("background_level/state"),String(cfg.backgroundLevel));if(dirty&MD_POWER_LIMIT)mqttPublish(mqttTopic("power_limit/state"),String(cfg.powerLimitMa));if(dirty&MD_TIMER)mqttPublish(mqttTopic("timer/state"),countdownActive?String(max(0L,(long)(countdownEndMs-millis())/1000L)):"0");
  if(dirty&MD_LIGHT)mqttPublish(mqttTopic("light/state"),lightStateJson());
}

void mqttCallback(String& t,String& v){
  size_t baseLen=strlen(cfg.mqttBase);
  if(t.length()<=baseLen+1||!t.startsWith(cfg.mqttBase)||t[baseLen]!='/')return;
  const String s=t.substring(baseLen+1);
  v.trim();bool changed=false;uint32_t dirty=0;bool enabled=v.equalsIgnoreCase("ON")||v=="1"||v.equalsIgnoreCase("true");
  if(s=="power/set"){cfg.power=v.equalsIgnoreCase("ON")||v=="1";scheduleSettingsSave();dirty|=MD_POWER|MD_LIGHT;}
  else if(s=="temperature_mode/set"){cfg.showTemperature=v.equalsIgnoreCase("ON")||v=="1";displayMode=MODE_CLOCK;scheduleSettingsSave();dirty|=MD_TEMP_MODE;}
  else if(s=="weather/set")startWeatherScroll();
  else if(s=="weather/data/set"){if(cfg.weatherSource==1)applyMqttWeather(v);}
  else if(s=="weather/source/set"){cfg.weatherSource=v.equalsIgnoreCase("Home Assistant MQTT")||v.equalsIgnoreCase("MQTT")||v=="1";weatherValid=false;weatherRefreshPending=cfg.weatherSource==0;scheduleSettingsSave();dirty|=MD_WEATHER_SOURCE;}
  else if(s=="foreground_effect/set"){int e=effectByName(v);if(e>=0){cfg.foregroundEffect=e;scheduleSettingsSave();dirty|=MD_FG|MD_LIGHT;}}
  else if(s=="background_effect/set"){int e=effectByName(v);if(e>=0){cfg.backgroundEffect=e;scheduleSettingsSave();dirty|=MD_BG;}}
  else if(s=="minute_effect/set"){int e=effectByName(v);if(e>=0){cfg.minuteEffect=e;scheduleSettingsSave();dirty|=MD_MIN_FX;}}
  else if(s=="minute_level/set"){cfg.minuteLevel=constrain(v.toInt(),0,255);scheduleSettingsSave();dirty|=MD_MIN_LEVEL;}
  else if(s=="foreground_speed/set"){cfg.foregroundSpeed=constrain(v.toInt(),1,100);scheduleSettingsSave();dirty|=MD_FG_SPEED;}
  else if(s=="background_speed/set"){cfg.backgroundSpeed=constrain(v.toInt(),1,100);scheduleSettingsSave();dirty|=MD_BG_SPEED;}
  else if(s=="minute_speed/set"){cfg.minuteSpeed=constrain(v.toInt(),1,100);scheduleSettingsSave();dirty|=MD_MIN_SPEED;}
  else if(s=="foreground_palette/set"){for(uint8_t i=0;i<12;i++)if(v.equalsIgnoreCase(paletteName(i))){cfg.foregroundPalette=i;scheduleSettingsSave();break;}}
  else if(s=="background_palette/set"){for(uint8_t i=0;i<12;i++)if(v.equalsIgnoreCase(paletteName(i))){cfg.backgroundPalette=i;scheduleSettingsSave();break;}}
  else if(s=="background_level/set"){cfg.backgroundLevel=constrain(v.toInt(),0,160);scheduleSettingsSave();dirty|=MD_BG_LEVEL;}
  else if(s=="power_limit/set"){cfg.powerLimitMa=constrain(v.toInt(),500,5000);FastLED.setMaxPowerInVoltsAndMilliamps(5,cfg.powerLimitMa);scheduleSettingsSave();dirty|=MD_POWER_LIMIT;}
  else if(s=="timer/set"){uint32_t seconds=constrain(v.toInt(),0,86400);if(seconds){countdownDurationMs=seconds*1000UL;countdownEndMs=millis()+countdownDurationMs;countdownActive=true;}else countdownActive=false;dirty|=MD_TIMER;}
  else if(s=="light/set"){String state=jsonString(v,"state","");if(state.length())cfg.power=!state.equalsIgnoreCase("OFF");cfg.brightnessMax=constrain(jsonInt(v,"brightness",cfg.brightnessMax),1,255);jsonColor(v,"color",cfg.fgR,cfg.fgG,cfg.fgB);String effect=jsonString(v,"effect","");int e=effectByName(effect);if(e>=0)cfg.foregroundEffect=e;scheduleSettingsSave();updateBrightnessFromLdr();dirty|=MD_POWER|MD_FG|MD_LIGHT;}
  else if(s=="message/set"){if(cfg.notificationsEnabled)enqueueMessage(v,2);}
  else if(s=="calendar/set"){if(cfg.calendarEnabled)enqueueMessage("TERMIN  "+v,2);}
  else if(s=="event/set"){v.toLowerCase();triggerEvent(v);}
  else if(s=="waste/set"){setWaste(v);if(mqttClient.connected())mqttPublish(mqttTopic("waste/state"),wastePending?String(wasteTypes):"none");}
  else if(s=="waste/show"){if(wastePending)startWasteOverlay();}
  else if(s=="waste/ack/set"){acknowledgeWaste();if(mqttClient.connected())mqttPublish(mqttTopic("waste/state"),"none");}
  else if(s=="room_temperature/set"){roomTemperature=v.toFloat();dirty|=MD_ROOM_TEMP;}
  else if(s=="room_humidity/set"){roomHumidity=v.toFloat();dirty|=MD_ROOM_HUM;}
  else if(s=="climate/show")showRoomClimate();
  else if(s=="preset/set"){for(uint8_t i=0;i<6;i++)if(v.equalsIgnoreCase(presetName(i)))applyPreset(i);}
  else if(s=="feature/preview/set"){cfg.previewEnabled=enabled;changed=true;dirty|=MD_F_PREVIEW;}
  else if(s=="feature/presets/set"){cfg.presetsEnabled=enabled;changed=true;dirty|=MD_F_PRESETS;}
  else if(s=="feature/notifications/set"){cfg.notificationsEnabled=enabled;changed=true;dirty|=MD_F_NOTIFICATIONS;}
  else if(s=="feature/calendar/set"){cfg.calendarEnabled=enabled;changed=true;dirty|=MD_F_CALENDAR;}
  else if(s=="feature/games/set"){cfg.gamesEnabled=enabled;if(!enabled)startGame("none");changed=true;dirty|=MD_F_GAMES;}
  else if(s=="feature/highscores/set"){cfg.highScoresEnabled=enabled;changed=true;dirty|=MD_F_HIGHSCORES;}
  else if(s=="feature/weather_animations/set"){cfg.weatherAnimationsEnabled=enabled;changed=true;dirty|=MD_F_WEATHER_ANIM;}
  else if(s=="feature/room_climate/set"){cfg.roomClimateEnabled=enabled;changed=true;dirty|=MD_F_ROOM_CLIMATE;}
  else if(s.startsWith("feature/")&&s.endsWith("/set")){
    const String key=s.substring(8,s.length()-4);
    for(const FeatureDef& f:newFeatures)if(key==f.key){*f.value=enabled?1:0;if(f.value==&cfg.circadianEnabled)lastCircadianMs=0;changed=true;dirty|=MD_EXTRA;break;}
  }
  else if(s=="game/set"){v.toLowerCase();if(v=="uhr")startGame("none");else if(v=="pong 2p")startGame("pong2");else if(v=="flappy bird")startGame("flappy");else startGame(v);}
  if(changed)scheduleSettingsSave();
  mqttDirtyMask|=dirty;
}
void mqttConnect(){
  if(!cfg.mqttEnabled||!cfg.mqttHost[0]||WiFi.status()!=WL_CONNECTED||mqttClient.connected())return;unsigned long now=millis();if(now-lastMqttAttemptMs<mqttRetryDelayMs)return;lastMqttAttemptMs=now;
  mqttClient.begin(cfg.mqttHost,cfg.mqttPort,mqttNet);String id="wortuhr-"+chipSerial(),will=mqttTopic("status");mqttClient.setWill(will.c_str(),"offline",true,0);
  bool ok=cfg.mqttUser[0]?mqttClient.connect(id.c_str(),cfg.mqttUser,cfg.mqttPass):mqttClient.connect(id.c_str());
  if(ok){
    mqttRetryDelayMs=5000;
    static const char* const subs[]={"power/set","temperature_mode/set","weather/set","weather/data/set","weather/source/set","foreground_effect/set","background_effect/set","minute_effect/set","minute_level/set","foreground_speed/set","background_speed/set","minute_speed/set","foreground_palette/set","background_palette/set","background_level/set","power_limit/set","light/set","timer/set","message/set","calendar/set","event/set","waste/set","waste/show","waste/ack/set","room_temperature/set","room_humidity/set","climate/show","preset/set","feature/+/set","game/set"};
    for(const char* s:subs)mqttClient.subscribe(mqttTopic(s));
    mqttPublishDiscovery();mqttPublishState();
  }
  else mqttRetryDelayMs=mqttRetryDelayMs<30000?30000:60000;
}

String colorJson(uint8_t r,uint8_t g,uint8_t b){return String("{\"r\":")+r+",\"g\":"+g+",\"b\":"+b+"}";}
String jsonEscape(String value){value.replace("\\","\\\\");value.replace("\"","\\\"");value.replace("\n","\\n");value.replace("\r","\\r");return value;}
const char* resetReasonName(){switch(esp_reset_reason()){case ESP_RST_POWERON:return "Power-on";case ESP_RST_SW:return "Software";case ESP_RST_PANIC:return "Panic";case ESP_RST_INT_WDT:return "Interrupt-WDT";case ESP_RST_TASK_WDT:return "Task-WDT";case ESP_RST_BROWNOUT:return "Brownout";default:return "Sonstiger";}}
void sendFrame(){
  static uint8_t frame[MATRIX_W*MATRIX_H*3];uint16_t p=0;uint32_t estimatedMa=0;
  for(uint8_t y=0;y<MATRIX_H;y++)for(uint8_t x=0;x<MATRIX_W;x++){CRGB c=leds[matrixMap[y][x]];c.nscale8_video(FastLED.getBrightness());c.r=scale8(c.r,tintNow.r);c.g=scale8(c.g,tintNow.g);c.b=scale8(c.b,tintNow.b);frame[p++]=c.r;frame[p++]=c.g;frame[p++]=c.b;estimatedMa+=((uint16_t)c.r+c.g+c.b)*20UL/255UL;}

  if(estimatedMa>cfg.powerLimitMa)for(uint16_t i=0;i<sizeof(frame);i++)frame[i]=(uint32_t)frame[i]*cfg.powerLimitMa/estimatedMa;

  for(uint16_t i=0;i<sizeof(frame);i+=3){uint8_t peak=max(frame[i],max(frame[i+1],frame[i+2]));if(peak&&peak<30){frame[i]=min(255,(int)frame[i]*30/peak);frame[i+1]=min(255,(int)frame[i+1]*30/peak);frame[i+2]=min(255,(int)frame[i+2]*30/peak);}}
  server.sendHeader("Cache-Control","no-store");server.setContentLength(sizeof(frame));server.send(200,"application/octet-stream","");server.client().write(frame,sizeof(frame));
}
void sendState(){
  String j;j.reserve(2600);j="{";
  j+="\"power\":"+String(cfg.power)+",\"showTemperature\":"+String(cfg.showTemperature);
  j+=",\"brightnessMin\":"+String(cfg.brightnessMin)+",\"brightnessMax\":"+String(cfg.brightnessMax);
  j+=",\"ldrDarkRaw\":"+String(cfg.ldrDarkRaw)+",\"ldrBrightRaw\":"+String(cfg.ldrBrightRaw)+",\"ldrSmoothing\":"+String(cfg.ldrSmoothing);
  j+=",\"foregroundEffect\":"+String(cfg.foregroundEffect)+",\"backgroundEffect\":"+String(cfg.backgroundEffect)+",\"minuteEffect\":"+String(cfg.minuteEffect);
  j+=",\"foregroundSpeed\":"+String(cfg.foregroundSpeed)+",\"backgroundSpeed\":"+String(cfg.backgroundSpeed)+",\"minuteSpeed\":"+String(cfg.minuteSpeed);
  j+=",\"foregroundIntensity\":"+String(cfg.foregroundIntensity)+",\"foregroundTrail\":"+String(cfg.foregroundTrail)+",\"foregroundSize\":"+String(cfg.foregroundSize);
  j+=",\"backgroundIntensity\":"+String(cfg.backgroundIntensity)+",\"backgroundTrail\":"+String(cfg.backgroundTrail)+",\"backgroundSize\":"+String(cfg.backgroundSize);
  j+=",\"minuteIntensity\":"+String(cfg.minuteIntensity)+",\"minuteTrail\":"+String(cfg.minuteTrail)+",\"minuteSize\":"+String(cfg.minuteSize);
  j+=",\"foregroundPalette\":"+String(cfg.foregroundPalette)+",\"backgroundPalette\":"+String(cfg.backgroundPalette)+",\"minutePalette\":"+String(cfg.minutePalette);
  j+=",\"rotationFavoritesOnly\":"+String(cfg.rotationFavoritesOnly)+",\"rotationNoRepeat\":"+String(cfg.rotationNoRepeat)+",\"adaptiveFps\":"+String(cfg.adaptiveFps)+",\"gammaEnabled\":"+String(cfg.gammaEnabled)+",\"ldrAdaptiveEffects\":"+String(cfg.ldrAdaptiveEffects)+",\"ambientMode\":"+String(cfg.ambientMode)+",\"ambientWordLevel\":"+String(cfg.ambientWordLevel);
  j+=",\"calibrationR\":"+String(cfg.calibrationR)+",\"calibrationG\":"+String(cfg.calibrationG)+",\"calibrationB\":"+String(cfg.calibrationB)+",\"dayProfileEnabled\":"+String(cfg.dayProfileEnabled)+",\"quietStartHour\":"+String(cfg.quietStartHour)+",\"quietEndHour\":"+String(cfg.quietEndHour)+",\"soundReactiveEnabled\":"+String(cfg.soundReactiveEnabled);
  j+=",\"scrollSpeed\":"+String(cfg.scrollSpeed)+",\"backgroundLevel\":"+String(cfg.backgroundLevel)+",\"minuteLevel\":"+String(cfg.minuteLevel);
  j+=",\"foregroundColor\":"+colorJson(cfg.fgR,cfg.fgG,cfg.fgB)+",\"backgroundColor\":"+colorJson(cfg.bgR,cfg.bgG,cfg.bgB)+",\"minuteColor\":"+colorJson(cfg.minuteR,cfg.minuteG,cfg.minuteB);
  j+=",\"weatherSource\":"+String(cfg.weatherSource)+",\"weatherLocation\":\""+String(cfg.weatherLocation)+"\",\"weatherLat\":"+String(cfg.weatherLat,5)+",\"weatherLon\":"+String(cfg.weatherLon,5);
  j+=",\"mqttEnabled\":"+String(cfg.mqttEnabled)+",\"mqttDiscovery\":"+String(cfg.mqttDiscovery)+",\"mqttHost\":\""+jsonEscape(cfg.mqttHost)+"\",\"mqttPort\":"+String(cfg.mqttPort)+",\"mqttUser\":\""+jsonEscape(cfg.mqttUser)+"\",\"mqttBase\":\""+jsonEscape(cfg.mqttBase)+"\"";
  j+=",\"previewEnabled\":"+String(cfg.previewEnabled)+",\"presetsEnabled\":"+String(cfg.presetsEnabled)+",\"notificationsEnabled\":"+String(cfg.notificationsEnabled)+",\"calendarEnabled\":"+String(cfg.calendarEnabled)+",\"gamesEnabled\":"+String(cfg.gamesEnabled)+",\"highScoresEnabled\":"+String(cfg.highScoresEnabled)+",\"weatherAnimationsEnabled\":"+String(cfg.weatherAnimationsEnabled)+",\"roomClimateEnabled\":"+String(cfg.roomClimateEnabled)+",\"activePreset\":"+String(cfg.activePreset);
  j+=",\"transitionEnabled\":"+String(cfg.transitionEnabled)+",\"transitionType\":"+String(cfg.transitionType)+",\"circadianEnabled\":"+String(cfg.circadianEnabled)+",\"rotationEnabled\":"+String(cfg.rotationEnabled)+",\"rotationMinutes\":"+String(cfg.rotationMinutes)+",\"buttonMultiEnabled\":"+String(cfg.buttonMultiEnabled)+",\"regionalEnabled\":"+String(cfg.regionalEnabled)+",\"regionalStyle\":"+String(cfg.regionalStyle);
  uint32_t timerRemaining=countdownActive&&((long)(countdownEndMs-millis())>0)?(countdownEndMs-millis()+999)/1000:0;
  j+=",\"hostName\":\""+jsonEscape(cfg.hostName)+"\",\"adminUser\":\""+jsonEscape(cfg.adminUser)+"\",\"powerLimitMa\":"+String(cfg.powerLimitMa)+",\"gameTimeoutSec\":"+String(cfg.gameTimeoutSec);
  j+=",\"ip\":\""+WiFi.localIP().toString()+"\",\"mqttStatus\":\""+(cfg.mqttEnabled?(mqttClient.connected()?"verbunden":"getrennt"):"aus")+"\",\"weather\":\""+(weatherValid?String(currentTemperature,1)+" C":"--")+"\",\"ldr\":"+String(currentLdrRaw)+",\"actualBrightness\":"+String(currentBrightness)+",\"fps\":"+String(measuredFps)+",\"heap\":"+String(ESP.getFreeHeap())+",\"uptime\":"+String(millis()/1000UL)+",\"rssi\":"+String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0)+",\"resetReason\":\""+resetReasonName()+"\",\"timerRemaining\":"+String(timerRemaining)+",\"game\":\""+gameName()+"\",\"gameScore\":"+String(gameScore)+",\"highSnake\":"+String(cfg.highSnake)+",\"highTetris\":"+String(cfg.highTetris)+",\"highPong\":"+String(cfg.highPong)+",\"highFlappy\":"+String(cfg.highFlappy)+",\"wastePending\":"+String(wastePending?1:0)+",\"waste\":\""+jsonEscape(wastePending?String(wasteTypes):String("none"))+"\"}";
  server.send(200,"application/json",j);
}
int jsonInt(const String& body,const String& key,int fallback){int p=body.indexOf("\""+key+"\"");if(p<0)return fallback;p=body.indexOf(':',p)+1;return body.substring(p).toInt();}
float jsonFloat(const String& body,const String& key,float fallback){int p=body.indexOf("\""+key+"\"");if(p<0)return fallback;p=body.indexOf(':',p)+1;if(p<=0)return fallback;return body.substring(p).toFloat();}
String jsonString(const String& body,const String& key,const String& fallback){int p=body.indexOf("\""+key+"\"");if(p<0)return fallback;p=body.indexOf(':',p);p=body.indexOf('"',p)+1;if(p<=0)return fallback;int e=p;while((e=body.indexOf('"',e))>=0&&e>p&&body[e-1]=='\\')e++;if(e<0)return fallback;String out=body.substring(p,e);out.replace("\\n","\n");out.replace("\\r","");out.replace("\\\"","\"");out.replace("\\\\","\\");return out;}
void jsonColor(const String& body,const String& key,uint8_t& r,uint8_t& g,uint8_t& b){int p=body.indexOf("\""+key+"\"");if(p<0)return;int e=body.indexOf('}',p);String s=body.substring(p,e);r=constrain(jsonInt(s,"r",r),0,255);g=constrain(jsonInt(s,"g",g),0,255);b=constrain(jsonInt(s,"b",b),0,255);}
void sendExtendedState(){String j;j.reserve(900);j="{\"alarmEnabled\":"+String(automationCfg.alarmEnabled)+",\"alarmHour\":"+String(automationCfg.alarmHour)+",\"alarmMinute\":"+String(automationCfg.alarmMinute)+",\"alarmLead\":"+String(automationCfg.alarmLeadMinutes)+",\"alarmWeekdays\":"+String(automationCfg.alarmWeekdays)+",\"holidaysEnabled\":"+String(automationCfg.holidaysEnabled)+",\"personalDatesEnabled\":"+String(automationCfg.personalDatesEnabled)+",\"personalDates\":\""+jsonEscape(automationCfg.personalDates)+"\",\"presets\":[";for(uint8_t i=0;i<8;i++){if(i)j+=",";j+="{\"valid\":"+String(userPresets[i].valid?1:0)+",\"name\":\""+jsonEscape(userPresets[i].name)+"\"}";}j+="]}";server.send(200,"application/json",j);}
void receiveAutomation(){String b=server.arg("plain");automationCfg.version=AUTOMATION_VERSION;automationCfg.alarmEnabled=jsonInt(b,"alarmEnabled",automationCfg.alarmEnabled);automationCfg.alarmHour=constrain(jsonInt(b,"alarmHour",automationCfg.alarmHour),0,23);automationCfg.alarmMinute=constrain(jsonInt(b,"alarmMinute",automationCfg.alarmMinute),0,59);automationCfg.alarmLeadMinutes=constrain(jsonInt(b,"alarmLead",automationCfg.alarmLeadMinutes),1,60);automationCfg.alarmWeekdays=constrain(jsonInt(b,"alarmWeekdays",automationCfg.alarmWeekdays),0,127);automationCfg.holidaysEnabled=jsonInt(b,"holidaysEnabled",automationCfg.holidaysEnabled);automationCfg.personalDatesEnabled=jsonInt(b,"personalDatesEnabled",automationCfg.personalDatesEnabled);String dates=jsonString(b,"personalDates",automationCfg.personalDates);dates.replace("\r","");strlcpy(automationCfg.personalDates,dates.c_str(),sizeof(automationCfg.personalDates));saveAutomation();server.send(200,"application/json","{\"ok\":true}");}
void receiveConfig(){
  String requestBody=server.arg("plain"),requestedAdminUser=jsonString(requestBody,"adminUser",cfg.adminUser),requestedApPass=jsonString(requestBody,"apPass",""),requestedAdminPass=jsonString(requestBody,"adminPass","");requestedAdminUser.trim();bool validAdminUser=requestedAdminUser.length()>=1&&requestedAdminUser.length()<=16;for(uint16_t i=0;i<requestedAdminUser.length();i++){char c=requestedAdminUser[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))validAdminUser=false;}if(!validAdminUser||(requestedApPass.length()&&requestedApPass.length()<8)||(requestedAdminPass.length()&&requestedAdminPass.length()<8)){server.send(400,"application/json","{\"ok\":false,\"error\":\"Benutzername oder Passwort ungueltig\"}");return;}bool securityChange=requestedAdminUser!=String(cfg.adminUser)||requestedApPass.length()||requestedAdminPass.length();if(securityChange&&!server.authenticate(cfg.adminUser,cfg.adminPass)){server.requestAuthentication();return;}
  Settings oldCfg=cfg;
  uint8_t oldWeatherSource=cfg.weatherSource;float oldLat=cfg.weatherLat,oldLon=cfg.weatherLon;String oldLocation=cfg.weatherLocation;
  uint8_t oldMqttEnabled=cfg.mqttEnabled,oldMqttDiscovery=cfg.mqttDiscovery;uint16_t oldMqttPort=cfg.mqttPort;
  String oldMqttHost=cfg.mqttHost,oldMqttUser=cfg.mqttUser,oldMqttPass=cfg.mqttPass,oldMqttBase=cfg.mqttBase;
  String oldHostName=cfg.hostName,oldApPass=cfg.apPass,oldAdminPass=cfg.adminPass,oldAdminUser=cfg.adminUser;
  String b=requestBody;cfg.power=jsonInt(b,"power",cfg.power);cfg.showTemperature=jsonInt(b,"showTemperature",cfg.showTemperature);
  int requestedDark=constrain(jsonInt(b,"brightnessMin",cfg.brightnessMin),1,255),requestedBright=constrain(jsonInt(b,"brightnessMax",cfg.brightnessMax),1,255);
  cfg.brightnessMin=min(requestedDark,requestedBright);cfg.brightnessMax=max(requestedDark,requestedBright);
  int newLdrDark=constrain(jsonInt(b,"ldrDarkRaw",cfg.ldrDarkRaw),0,1023),newLdrBright=constrain(jsonInt(b,"ldrBrightRaw",cfg.ldrBrightRaw),0,1023);if(abs(newLdrBright-newLdrDark)>=16){cfg.ldrDarkRaw=newLdrDark;cfg.ldrBrightRaw=newLdrBright;}cfg.ldrSmoothing=constrain(jsonInt(b,"ldrSmoothing",cfg.ldrSmoothing),1,16);
  cfg.foregroundEffect=constrain(jsonInt(b,"foregroundEffect",cfg.foregroundEffect),0,FX_COUNT-1);cfg.backgroundEffect=constrain(jsonInt(b,"backgroundEffect",cfg.backgroundEffect),0,FX_COUNT-1);cfg.minuteEffect=constrain(jsonInt(b,"minuteEffect",cfg.minuteEffect),0,FX_COUNT-1);
  cfg.foregroundSpeed=constrain(jsonInt(b,"foregroundSpeed",cfg.foregroundSpeed),1,100);cfg.backgroundSpeed=constrain(jsonInt(b,"backgroundSpeed",cfg.backgroundSpeed),1,100);cfg.minuteSpeed=constrain(jsonInt(b,"minuteSpeed",cfg.minuteSpeed),1,100);
  cfg.foregroundIntensity=constrain(jsonInt(b,"foregroundIntensity",cfg.foregroundIntensity),0,255);cfg.foregroundTrail=constrain(jsonInt(b,"foregroundTrail",cfg.foregroundTrail),0,255);cfg.foregroundSize=constrain(jsonInt(b,"foregroundSize",cfg.foregroundSize),1,100);
  cfg.backgroundIntensity=constrain(jsonInt(b,"backgroundIntensity",cfg.backgroundIntensity),0,255);cfg.backgroundTrail=constrain(jsonInt(b,"backgroundTrail",cfg.backgroundTrail),0,255);cfg.backgroundSize=constrain(jsonInt(b,"backgroundSize",cfg.backgroundSize),1,100);
  cfg.minuteIntensity=constrain(jsonInt(b,"minuteIntensity",cfg.minuteIntensity),0,255);cfg.minuteTrail=constrain(jsonInt(b,"minuteTrail",cfg.minuteTrail),0,255);cfg.minuteSize=constrain(jsonInt(b,"minuteSize",cfg.minuteSize),1,100);
  cfg.foregroundPalette=constrain(jsonInt(b,"foregroundPalette",cfg.foregroundPalette),0,11);cfg.backgroundPalette=constrain(jsonInt(b,"backgroundPalette",cfg.backgroundPalette),0,11);cfg.minutePalette=constrain(jsonInt(b,"minutePalette",cfg.minutePalette),0,11);
  cfg.rotationFavoritesOnly=jsonInt(b,"rotationFavoritesOnly",cfg.rotationFavoritesOnly)?1:0;cfg.rotationNoRepeat=constrain(jsonInt(b,"rotationNoRepeat",cfg.rotationNoRepeat),0,6);
  cfg.adaptiveFps=jsonInt(b,"adaptiveFps",cfg.adaptiveFps)?1:0;cfg.gammaEnabled=jsonInt(b,"gammaEnabled",cfg.gammaEnabled)?1:0;cfg.ldrAdaptiveEffects=jsonInt(b,"ldrAdaptiveEffects",cfg.ldrAdaptiveEffects)?1:0;cfg.ambientMode=jsonInt(b,"ambientMode",cfg.ambientMode)?1:0;cfg.ambientWordLevel=constrain(jsonInt(b,"ambientWordLevel",cfg.ambientWordLevel),0,160);
  cfg.calibrationR=constrain(jsonInt(b,"calibrationR",cfg.calibrationR),1,255);cfg.calibrationG=constrain(jsonInt(b,"calibrationG",cfg.calibrationG),1,255);cfg.calibrationB=constrain(jsonInt(b,"calibrationB",cfg.calibrationB),1,255);
  cfg.dayProfileEnabled=jsonInt(b,"dayProfileEnabled",cfg.dayProfileEnabled)?1:0;cfg.quietStartHour=constrain(jsonInt(b,"quietStartHour",cfg.quietStartHour),0,23);cfg.quietEndHour=constrain(jsonInt(b,"quietEndHour",cfg.quietEndHour),0,23);
  cfg.soundReactiveEnabled=(WORTUHR_ENABLE_SOUND_REACTIVE&&jsonInt(b,"soundReactiveEnabled",cfg.soundReactiveEnabled))?1:0;
  cfg.legacyEffectSpeed=cfg.foregroundSpeed;cfg.scrollSpeed=constrain(jsonInt(b,"scrollSpeed",cfg.scrollSpeed),20,255);cfg.backgroundLevel=constrain(jsonInt(b,"backgroundLevel",cfg.backgroundLevel),0,160);cfg.minuteLevel=constrain(jsonInt(b,"minuteLevel",cfg.minuteLevel),0,255);
  jsonColor(b,"foregroundColor",cfg.fgR,cfg.fgG,cfg.fgB);jsonColor(b,"backgroundColor",cfg.bgR,cfg.bgG,cfg.bgB);jsonColor(b,"minuteColor",cfg.minuteR,cfg.minuteG,cfg.minuteB);
  cfg.weatherSource=constrain(jsonInt(b,"weatherSource",cfg.weatherSource),0,1);float newLat=jsonFloat(b,"weatherLat",cfg.weatherLat),newLon=jsonFloat(b,"weatherLon",cfg.weatherLon);if(newLat>=-90&&newLat<=90)cfg.weatherLat=newLat;if(newLon>=-180&&newLon<=180)cfg.weatherLon=newLon;String newLocation=jsonString(b,"weatherLocation",cfg.weatherLocation);newLocation.trim();newLocation.replace("\"","");newLocation.replace("\\","");if(newLocation.length())strlcpy(cfg.weatherLocation,newLocation.c_str(),sizeof(cfg.weatherLocation));
  cfg.mqttEnabled=jsonInt(b,"mqttEnabled",cfg.mqttEnabled);cfg.mqttDiscovery=jsonInt(b,"mqttDiscovery",cfg.mqttDiscovery);cfg.mqttPort=constrain(jsonInt(b,"mqttPort",cfg.mqttPort),1,65535);
  strlcpy(cfg.mqttHost,jsonString(b,"mqttHost",cfg.mqttHost).c_str(),sizeof(cfg.mqttHost));strlcpy(cfg.mqttUser,jsonString(b,"mqttUser",cfg.mqttUser).c_str(),sizeof(cfg.mqttUser));String newPass=jsonString(b,"mqttPass","");if(newPass.length())strlcpy(cfg.mqttPass,newPass.c_str(),sizeof(cfg.mqttPass));strlcpy(cfg.mqttBase,jsonString(b,"mqttBase",cfg.mqttBase).c_str(),sizeof(cfg.mqttBase));
  cfg.previewEnabled=jsonInt(b,"previewEnabled",cfg.previewEnabled);cfg.presetsEnabled=jsonInt(b,"presetsEnabled",cfg.presetsEnabled);cfg.notificationsEnabled=jsonInt(b,"notificationsEnabled",cfg.notificationsEnabled);cfg.calendarEnabled=jsonInt(b,"calendarEnabled",cfg.calendarEnabled);cfg.gamesEnabled=jsonInt(b,"gamesEnabled",cfg.gamesEnabled);cfg.highScoresEnabled=jsonInt(b,"highScoresEnabled",cfg.highScoresEnabled);cfg.weatherAnimationsEnabled=jsonInt(b,"weatherAnimationsEnabled",cfg.weatherAnimationsEnabled);cfg.roomClimateEnabled=jsonInt(b,"roomClimateEnabled",cfg.roomClimateEnabled);cfg.activePreset=constrain(jsonInt(b,"activePreset",cfg.activePreset),0,5);

  cfg.transitionEnabled=jsonInt(b,"transitionEnabled",cfg.transitionEnabled)?1:0;cfg.transitionType=constrain(jsonInt(b,"transitionType",cfg.transitionType),1,4);cfg.circadianEnabled=jsonInt(b,"circadianEnabled",cfg.circadianEnabled)?1:0;
  cfg.rotationEnabled=jsonInt(b,"rotationEnabled",cfg.rotationEnabled)?1:0;cfg.rotationMinutes=constrain(jsonInt(b,"rotationMinutes",cfg.rotationMinutes),1,240);cfg.buttonMultiEnabled=jsonInt(b,"buttonMultiEnabled",cfg.buttonMultiEnabled)?1:0;
  cfg.regionalEnabled=jsonInt(b,"regionalEnabled",cfg.regionalEnabled)?1:0;cfg.regionalStyle=constrain(jsonInt(b,"regionalStyle",cfg.regionalStyle),0,2);
  if(oldCfg.circadianEnabled!=cfg.circadianEnabled)lastCircadianMs=0;
  if(oldCfg.rotationEnabled!=cfg.rotationEnabled){rotFg=rotBg=255;lastRotationMs=millis();}
  if(oldCfg.transitionEnabled!=cfg.transitionEnabled||oldCfg.circadianEnabled!=cfg.circadianEnabled||oldCfg.rotationEnabled!=cfg.rotationEnabled||oldCfg.buttonMultiEnabled!=cfg.buttonMultiEnabled||oldCfg.regionalEnabled!=cfg.regionalEnabled)mqttDirtyMask|=MD_EXTRA;
  String newHost=jsonString(b,"hostName",cfg.hostName);newHost.trim();newHost.toLowerCase();newHost.replace(" ","-");String safeHost;for(uint16_t i=0;i<newHost.length();i++){char c=newHost[i];if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')safeHost+=c;}while(safeHost.startsWith("-"))safeHost.remove(0,1);while(safeHost.endsWith("-"))safeHost.remove(safeHost.length()-1);if(safeHost.length())strlcpy(cfg.hostName,safeHost.c_str(),sizeof(cfg.hostName));
  strlcpy(cfg.adminUser,requestedAdminUser.c_str(),sizeof(cfg.adminUser));if(requestedApPass.length())strlcpy(cfg.apPass,requestedApPass.c_str(),sizeof(cfg.apPass));if(requestedAdminPass.length())strlcpy(cfg.adminPass,requestedAdminPass.c_str(),sizeof(cfg.adminPass));
  cfg.powerLimitMa=constrain(jsonInt(b,"powerLimitMa",cfg.powerLimitMa),200,5000);cfg.gameTimeoutSec=constrain(jsonInt(b,"gameTimeoutSec",cfg.gameTimeoutSec),30,1800);FastLED.setMaxPowerInVoltsAndMilliamps(5,cfg.powerLimitMa);
  if(!cfg.mqttBase[0])strlcpy(cfg.mqttBase,"wortuhr/uhr",sizeof(cfg.mqttBase));
  bool weatherChanged=oldWeatherSource!=cfg.weatherSource||oldLat!=cfg.weatherLat||oldLon!=cfg.weatherLon||oldLocation!=String(cfg.weatherLocation);
  bool mqttChanged=oldMqttEnabled!=cfg.mqttEnabled||oldMqttDiscovery!=cfg.mqttDiscovery||oldMqttPort!=cfg.mqttPort||oldMqttHost!=String(cfg.mqttHost)||oldMqttUser!=String(cfg.mqttUser)||oldMqttPass!=String(cfg.mqttPass)||oldMqttBase!=String(cfg.mqttBase);
  bool apPasswordChanged=oldApPass!=String(cfg.apPass),adminPasswordChanged=oldAdminPass!=String(cfg.adminPass),adminUserChanged=oldAdminUser!=String(cfg.adminUser);bool restartChanged=oldHostName!=String(cfg.hostName)||apPasswordChanged||adminPasswordChanged||adminUserChanged;
  if(weatherChanged){weatherValid=false;weatherRuntimeLocation=cfg.weatherLocation;weatherRefreshPending=cfg.weatherSource==0;}
  if(oldCfg.ldrDarkRaw!=cfg.ldrDarkRaw||oldCfg.ldrBrightRaw!=cfg.ldrBrightRaw||oldCfg.ldrSmoothing!=cfg.ldrSmoothing)ldrAverage8=(int32_t)currentLdrRaw*8;
  updateBrightnessFromLdr();scheduleSettingsSave();
  if(restartChanged){restartPending=true;restartAtMs=millis()+3500UL;}
  if(mqttChanged){if(mqttClient.connected())mqttClient.disconnect();mqttDiscoveryPublished=false;lastMqttAttemptMs=0;mqttRetryDelayMs=5000;}
  if(oldCfg.power!=cfg.power)mqttDirtyMask|=MD_POWER;if(oldCfg.showTemperature!=cfg.showTemperature)mqttDirtyMask|=MD_TEMP_MODE;
  if(oldCfg.foregroundEffect!=cfg.foregroundEffect)mqttDirtyMask|=MD_FG;if(oldCfg.backgroundEffect!=cfg.backgroundEffect)mqttDirtyMask|=MD_BG;if(oldCfg.minuteEffect!=cfg.minuteEffect)mqttDirtyMask|=MD_MIN_FX;if(oldCfg.minuteLevel!=cfg.minuteLevel)mqttDirtyMask|=MD_MIN_LEVEL;
  if(oldCfg.foregroundSpeed!=cfg.foregroundSpeed)mqttDirtyMask|=MD_FG_SPEED;if(oldCfg.backgroundSpeed!=cfg.backgroundSpeed)mqttDirtyMask|=MD_BG_SPEED;if(oldCfg.minuteSpeed!=cfg.minuteSpeed)mqttDirtyMask|=MD_MIN_SPEED;if(oldCfg.backgroundLevel!=cfg.backgroundLevel)mqttDirtyMask|=MD_BG_LEVEL;if(oldCfg.powerLimitMa!=cfg.powerLimitMa)mqttDirtyMask|=MD_POWER_LIMIT;
  if(oldCfg.power!=cfg.power||oldCfg.brightnessMax!=cfg.brightnessMax||oldCfg.foregroundEffect!=cfg.foregroundEffect||oldCfg.fgR!=cfg.fgR||oldCfg.fgG!=cfg.fgG||oldCfg.fgB!=cfg.fgB)mqttDirtyMask|=MD_LIGHT;
  if(oldCfg.weatherSource!=cfg.weatherSource)mqttDirtyMask|=MD_WEATHER_SOURCE;if(oldCfg.activePreset!=cfg.activePreset)mqttDirtyMask|=MD_PRESET;
  if(oldCfg.previewEnabled!=cfg.previewEnabled)mqttDirtyMask|=MD_F_PREVIEW;if(oldCfg.presetsEnabled!=cfg.presetsEnabled)mqttDirtyMask|=MD_F_PRESETS;if(oldCfg.notificationsEnabled!=cfg.notificationsEnabled)mqttDirtyMask|=MD_F_NOTIFICATIONS;if(oldCfg.calendarEnabled!=cfg.calendarEnabled)mqttDirtyMask|=MD_F_CALENDAR;
  if(oldCfg.gamesEnabled!=cfg.gamesEnabled)mqttDirtyMask|=MD_F_GAMES;if(oldCfg.highScoresEnabled!=cfg.highScoresEnabled)mqttDirtyMask|=MD_F_HIGHSCORES;if(oldCfg.weatherAnimationsEnabled!=cfg.weatherAnimationsEnabled)mqttDirtyMask|=MD_F_WEATHER_ANIM;if(oldCfg.roomClimateEnabled!=cfg.roomClimateEnabled)mqttDirtyMask|=MD_F_ROOM_CLIMATE;
  if(!cfg.gamesEnabled&&gameMode!=GAME_NONE)startGame("none");
  if(displayMode!=MODE_SCROLL&&gameMode==GAME_NONE)displayMode=MODE_CLOCK;String response=String("{\"ok\":true,\"apPasswordChanged\":")+(apPasswordChanged?"true":"false")+",\"adminUserChanged\":"+(adminUserChanged?"true":"false")+",\"adminPasswordChanged\":"+(adminPasswordChanged?"true":"false")+",\"restartRequired\":"+(restartChanged?"true":"false")+"}";server.send(200,"application/json",response);
}

void renderWifiStartup(bool portal,unsigned long now){
  fill_solid(leds,NUM_LEDS,CRGB::Black);CRGB base=portal?CRGB(0,210,105):CRGB(25,115,255);uint8_t head=(now/110)%MATRIX_W;
  for(uint8_t y=2;y<8;y++)for(uint8_t x=0;x<MATRIX_W;x++){uint8_t distance=(x+MATRIX_W-head)%MATRIX_W;if(distance<4&&((x+y)&1)==0)leds[matrixMap[y][x]]=scaleColor(base,255-distance*55);}
  for(uint8_t i=0;i<4;i++)leds[110+i]=i==((now/250)%4)?CRGB::White:scaleColor(base,45);FastLED.show();
}
void startLedHardwareTest(){wordTestUntilMs=0;ledTestStartedMs=millis();ledTestUntilMs=ledTestStartedMs+10000UL;Serial.println("[LED-Test] Rot, Gruen, Blau, danach Zeilenscan");}
void renderLedHardwareTest(unsigned long now){
  unsigned long elapsed=now-ledTestStartedMs;FastLED.setBrightness(160);currentBrightness=160;fill_solid(leds,NUM_LEDS,CRGB::Black);
  if(elapsed<1800)fill_solid(leds,NUM_LEDS,CRGB::Red);else if(elapsed<3600)fill_solid(leds,NUM_LEDS,CRGB::Green);else if(elapsed<5400)fill_solid(leds,NUM_LEDS,CRGB::Blue);
  else{uint8_t row=((elapsed-5400)/400)%MATRIX_H;for(uint8_t x=0;x<MATRIX_W;x++)leds[matrixMap[row][x]]=CRGB::White;for(uint8_t i=110;i<114;i++)leds[i]=CRGB(80,80,80);}
  FastLED.show();
}
void renderWordHardwareTest(){

  FastLED.setBrightness(180);currentBrightness=180;
  fill_solid(leds,NUM_LEDS,CRGB::Black);
  const CRGB white=CRGB::White;
  for(uint8_t x=0;x<2;x++)leds[matrixMap[0][x]]=white;
  for(uint8_t x=3;x<6;x++)leds[matrixMap[0][x]]=white;
  for(uint8_t x=0;x<4;x++)leds[matrixMap[9][x]]=white;
  for(uint8_t x=8;x<11;x++)leds[matrixMap[9][x]]=white;
  FastLED.show();
}
void startWordHardwareTest(){
  gameMode=GAME_NONE;overlayMode=OVERLAY_NONE;wordTestUntilMs=millis()+15000UL;
  ledTestUntilMs=0;renderWordHardwareTest();
}
bool allowedButtonPin(int pin){return pin==19||pin==21||pin==22||pin==23||pin==25||pin==26||pin==27||pin==32||pin==33;}
void loadHardwareButtonSettings(){
  Preferences p;if(p.begin("gpio-button",true)){
    hardwareButtonEnabled=p.getBool("enabled",false);
    int pin=p.getUChar("pin",19);hardwareButtonPin=allowedButtonPin(pin)?pin:19;p.end();
  }
  if(hardwareButtonEnabled)pinMode(hardwareButtonPin,INPUT_PULLUP);
}
void saveHardwareButtonSettings(){
  Preferences p;if(p.begin("gpio-button",false)){
    p.putBool("enabled",hardwareButtonEnabled);p.putUChar("pin",hardwareButtonPin);p.end();
  }
}
void resetHardwareButtonState(){
  lastButtonReading=HIGH;stableButtonState=HIGH;buttonChangedMs=millis();
  buttonPressStartedMs=0;buttonLongActionDone=false;buttonPowerDone=false;
  buttonClicks=0;lastClickReleaseMs=0;
}
void checkPowerCycleWifiReset(){
  Preferences p;
  if(!p.begin("wifi-recovery",false))return;
  const bool powerOn=(esp_reset_reason()==ESP_RST_POWERON);
  const bool previousShortBoot=p.getBool("armed",false);
  const uint8_t count=p.getUChar("count",0);
  if(!powerOn){
    p.putBool("armed",false);p.putUChar("count",0);p.end();
    Serial.println("[WiFi-Recovery] Software-/Fehlerreset wird nicht gezaehlt");
    return;
  }
  const uint8_t next=previousShortBoot ? (uint8_t)min(3,(int)count+1) : 1;
  if(next>=3){
    p.putBool("armed",false);p.putUChar("count",0);p.end();
    WiFi.mode(WIFI_STA);
    WiFiManager recovery;
    recovery.resetSettings();
    wifiPowerCycleResetRequested=true;
    Serial.println("[WiFi-Recovery] 3 kurze Einschaltvorgaenge: WLAN-Daten geloescht");
    return;
  }
  p.putUChar("count",next);p.putBool("armed",true);p.end();
  wifiPowerCycleArmed=true;wifiPowerCycleArmedAt=millis();
  Serial.printf("[WiFi-Recovery] Power-on %u/3, 5 Sekunden Einschaltfenster\n",next);
}
void disarmPowerCycleWifiReset(unsigned long now){
  if(!wifiPowerCycleArmed || now-wifiPowerCycleArmedAt<5000UL)return;
  Preferences p;
  if(p.begin("wifi-recovery",false)){p.putBool("armed",false);p.putUChar("count",0);p.end();}
  wifiPowerCycleArmed=false;
  Serial.println("[WiFi-Recovery] Normalstart: Zaehler zurueckgesetzt");
}
void wifiPortalStarted(WiFiManager*){
  wifiPortalActive=true;lastPortalClientMs=millis();wifiUiMode=WIFI_UI_CONNECTING;wifiPortalPage=0;setWifiUiText("WIFI AP STARTET");
  Serial.print("[WiFi] Portal aktiv: ");Serial.print(wifiApName);Serial.println(" / 192.168.4.1");
}
void connectWifi(){
  String serial=chipSerial();String shortSerial=serial.substring(serial.length()-6);wifiApName="Wortuhr_"+shortSerial;String hostName=cfg.hostName;
  WiFi.mode(WIFI_STA);WiFi.setHostname(hostName.c_str());wifiManager.setHostname(hostName.c_str());wifiManager.setClass("invert");wifiManager.setConnectTimeout(30);wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setAPCallback(wifiPortalStarted);wifiManager.setConfigPortalBlocking(false);
  wifiUiMode=WIFI_UI_CONNECTING;setWifiUiText("WIFI VERBINDEN");
  renderWifiStartup(false,millis());Serial.println("[WiFi] Verbinde oder starte Konfigurationsportal ...");
  if(wifiPowerCycleResetRequested)setWifiUiText("WIFI RESET");
  wifiManager.autoConnect(wifiApName.c_str(),cfg.apPass);
  wifiWasConnected=WiFi.status()==WL_CONNECTED;
  if(wifiWasConnected){WiFi.setSleep(false);wifiSuccessShown=true;wifiUiMode=WIFI_UI_SUCCESS;wifiSuccessUntilMs=millis()+1800UL;setWifiUiText("WIFI VERBUNDEN");Serial.print("[WiFi] Verbunden, IP: ");Serial.println(WiFi.localIP());}
}

void startWebServices(){
  if(webServerStarted)return;
  server.begin();wsServer.begin();wsServer.onEvent(wsEvent);webServerStarted=true;
  Serial.println("[Web] WebGUI + WebSocket gestartet");
}

void updateWifiState(){
  bool connected=WiFi.status()==WL_CONNECTED;
  if(connected&&!wifiWasConnected){
    wifiWasConnected=true;if(wifiPortalActive)wifiManager.stopConfigPortal();wifiPortalActive=false;WiFi.setSleep(false);
    if(!wifiSuccessShown){wifiSuccessShown=true;wifiUiMode=WIFI_UI_SUCCESS;wifiSuccessUntilMs=millis()+1800UL;setWifiUiText("WIFI VERBUNDEN");}
    Serial.print("[WiFi] Verbunden, IP: ");Serial.println(WiFi.localIP());
    startWebServices();
    if(!mdnsStarted&&MDNS.begin(cfg.hostName)){MDNS.addService("http","tcp",80);mdnsStarted=true;Serial.print("[mDNS] http://");Serial.print(cfg.hostName);Serial.println(".local");}
  }else if(!connected&&wifiWasConnected){
    wifiWasConnected=false;
    Serial.println("[WiFi] Verbindung verloren, automatische Wiederverbindung aktiv");
  }
}

String bootHistoryJson(){String j="[";for(uint8_t i=0;i<5;i++){String k="b"+String(i),v=runtimePrefs.getString(k.c_str(),"");if(!v.length())continue;if(j.length()>1)j+=",";j+="\""+jsonEscape(v)+"\"";}j+="]";return j;}
void recordBootHistory(){runtimePrefs.begin("wortuhr-v4",false);for(int i=4;i>0;i--){String a=runtimePrefs.getString(("b"+String(i-1)).c_str(),"");runtimePrefs.putString(("b"+String(i)).c_str(),a);}String entry=String(resetReasonName())+"/"+String(runtimePrefs.getULong("last_uptime",0))+"s";runtimePrefs.putString("b0",entry);runtimePrefs.putULong("last_uptime",0);}
void sendDiagnostics(){String j;j.reserve(700);j="{\"version\":\"" WORTUHR_FW_VERSION "\",\"board\":\"" WORTUHR_BOARD_NAME "\",\"heap\":"+String(ESP.getFreeHeap())+",\"minHeap\":"+String(ESP.getMinFreeHeap())+",\"uptime\":"+String(millis()/1000UL)+",\"fps\":"+String(measuredFps)+",\"ledFrames\":"+String(renderedLedFrames)+",\"skippedFrames\":"+String(skippedStaticFrames)+",\"reset\":\""+String(resetReasonName())+"\",\"rssi\":"+String(WiFi.RSSI())+",\"mqtt\":"+(mqttClient.connected()?"true":"false")+",\"boots\":"+bootHistoryJson()+"}";server.send(200,"application/json",j);}
void exportSettings(){if(!server.authenticate(cfg.adminUser,cfg.adminPass)){server.requestAuthentication();return;}server.sendHeader("Content-Disposition","attachment; filename=wortuhr-4.0.0-settings.bin");server.setContentLength(sizeof(cfg));server.send(200,"application/octet-stream","");server.client().write((const uint8_t*)&cfg,sizeof(cfg));}
void importSettings(){if(!server.authenticate(cfg.adminUser,cfg.adminPass)){server.requestAuthentication();return;}if(server.arg("plain").length()!=sizeof(Settings)){server.send(400,"application/json","{\"ok\":false}");return;}Settings incoming;memcpy(&incoming,server.arg("plain").c_str(),sizeof(Settings));if(incoming.magic!=SETTINGS_MAGIC){server.send(400,"application/json","{\"ok\":false}");return;}cfg=incoming;cfg.version=SETTINGS_VERSION;cfg.v4SettingsVersion=V4_SETTINGS_VERSION;saveSettings();server.send(200,"application/json","{\"ok\":true}");delay(100);ESP.restart();}
unsigned long desiredFrameInterval(){if(!cfg.adaptiveFps)return 40;bool animated=(curFgEffect()!=FX_SOLID||curBgEffect()!=FX_SOLID||cfg.minuteEffect!=FX_SOLID||transitionRunning||displayMode==MODE_SCROLL||overlayMode!=OVERLAY_NONE||gameMode!=GAME_NONE);if(!animated)return 250;uint8_t fastest=max(cfg.foregroundSpeed,max(cfg.backgroundSpeed,cfg.minuteSpeed));return fastest<20?100:(fastest<45?66:40);}

void setup(){
  Serial.begin(115200);checkPowerCycleWifiReset();loadSettings();loadExtendedSettings();recordBootHistory();esp_ota_mark_app_valid_cancel_rollback();for(uint8_t i=0;i<32;i++)fxLife[0][random8(MATRIX_H)][random8(MATRIX_W)]=1;analogReadResolution(10);pinMode(LDR_PIN,INPUT);loadHardwareButtonSettings();
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds,NUM_LEDS);FastLED.setMaxPowerInVoltsAndMilliamps(5,cfg.powerLimitMa);currentBrightness=max((int)cfg.brightnessMin,1);FastLED.setBrightness(currentBrightness);
  FastLED.setDither(1);
  connectWifi();configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3","0.europe.pool.ntp.org","pool.ntp.org");
  mqttNet.setTimeout(2);mqttClient.onMessage(mqttCallback);mqttClient.setOptions(30,true,2000);
  server.on("/",HTTP_GET,[]{
    server.send(200,"text/html; charset=utf-8",
    R"WEB(<!doctype html><html lang="de"><meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Wortuhr von ihmSelbst</title><style>html,body{margin:0;height:100%;font:14px system-ui}
    iframe{border:0;width:100%;height:100%}a{position:fixed;right:12px;bottom:12px;z-index:9;
    background:#234f82;color:white;padding:11px 16px;border-radius:10px;text-decoration:none;
    box-shadow:0 2px 12px #4448}a:hover{background:#12365a}</style>
    <iframe title="Wortuhr Steuerung" src="/steuerung"></iframe>
    <a href="/konfiguration">Konfiguration</a></html>)WEB");
  });
  server.on("/steuerung",HTTP_GET,[]{server.sendHeader("Content-Encoding","gzip");server.sendHeader("Cache-Control","no-cache");server.send_P(200,"text/html",(PGM_P)PAGE_GZ,PAGE_GZ_LEN);});
  server.on("/api/gpio-taster",HTTP_GET,[]{
    server.send(200,"application/json",String("{\"enabled\":")+(hardwareButtonEnabled?"true":"false")+",\"pin\":"+String(hardwareButtonPin)+",\"level\":"+String(hardwareButtonEnabled?digitalRead(hardwareButtonPin):-1)+"}");
  });
  server.on("/api/gpio-taster",HTTP_POST,[]{
    if(!server.authenticate(cfg.adminUser,cfg.adminPass)){server.requestAuthentication();return;}
    const int pin=server.arg("pin").toInt();
    if(!allowedButtonPin(pin)){server.send(400,"application/json","{\"error\":\"GPIO nicht erlaubt\"}");return;}
    bool enabled=server.arg("enabled")=="1";
    if(hardwareButtonEnabled)pinMode(hardwareButtonPin,INPUT);
    hardwareButtonPin=(uint8_t)pin;hardwareButtonEnabled=enabled;
    if(hardwareButtonEnabled)pinMode(hardwareButtonPin,INPUT_PULLUP);
    resetHardwareButtonState();saveHardwareButtonSettings();
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/konfiguration",HTTP_GET,[]{
    server.send(200,"text/html; charset=utf-8",
      R"WEB(<!doctype html><html lang="de"><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8">
      <title>Wortuhr Konfiguration</title><style>body{font:16px system-ui;background:#141c28;color:#eee;max-width:720px;margin:auto;padding:22px}
      section{background:#253344;padding:18px;margin:15px 0;border-radius:12px}a{color:#8bc9ff}
      button,select{padding:10px;font-size:16px}label{display:block;margin:12px 0}</style>
      <h1>Wortuhr - Konfiguration</h1><p><a href="/">Zur Hauptansicht</a></p>
      <section><h2>GPIO-Taster</h2><label><input id="enabled" type="checkbox"> Hardware-Taster aktivieren</label>
      <label>GPIO auswählen: <select id="pin"><option>19</option><option>21</option><option>22</option><option>23</option><option>25</option><option>26</option><option>27</option><option>32</option><option>33</option></select></label>
      <p>Achtung: Alternativer GPIO erfordert Umverdrahtung. Pinbelegung vor Verwendung prüfen.
      Bei abgezogenem Taster deaktiviert lassen. Der WLAN-Reset per Tastendruck ist entfernt.</p>
      <p id="level">Signal: wird geladen</p><button onclick="save()">GPIO speichern</button><p id="msg"></p></section>
      <section><h2>Alle anderen Einstellungen</h2><p>Effekte, Helligkeit, Wetter, MQTT,
      Tasteraktionen, Uhrzeit und weitere Parameter bleiben in der vollständigen Steueroberfläche.</p>
      <a href="/steuerung">Alle Wortuhr-Einstellungen öffnen</a></section>
      <section><h2>WLAN-Wiederherstellung</h2>
      <p>3-mal einschalten, die ersten beiden Male innerhalb von 5 Sekunden wieder ausschalten.
      Beim dritten Einschalten startet das WLAN-Portal. Automatische Neustarts zählen nicht.</p>
      <a href="/wifi-hilfe">WLAN-Hilfe öffnen</a></section>
      <script>
      const e=document.getElementById('enabled'),p=document.getElementById('pin'),l=document.getElementById('level'),m=document.getElementById('msg');
      async function refresh(){try{let v=await(await fetch('/api/gpio-taster')).json();e.checked=v.enabled;p.value=String(v.pin);
      l.textContent='GPIO '+v.pin+': '+(v.enabled?(v.level===0?'LOW (gedrückt)':'HIGH (frei)'):'deaktiviert')}
      catch(err){m.textContent='Statusabfrage fehlgeschlagen';}}
      async function save(){try{let data=new URLSearchParams({enabled:e.checked?'1':'0',pin:p.value});
      let r=await fetch('/api/gpio-taster',{method:'POST',body:data});if(!r.ok)throw Error('Speichern fehlgeschlagen ('+r.status+')');
      m.textContent='Gespeichert';refresh()}catch(err){m.textContent=err.message;}}
      refresh();setInterval(refresh,5000);
      </script></html>)WEB");
  });
  server.on("/api/state",HTTP_GET,sendState);server.on("/api/config",HTTP_POST,receiveConfig);
  server.on("/api/weather",HTTP_POST,[]{startWeatherScroll();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/frame",HTTP_GET,sendFrame);
  server.on("/api/diagnostics",HTTP_GET,sendDiagnostics);
  server.on("/api/favorite",HTTP_POST,[]{uint8_t e=constrain(server.arg("effect").toInt(),0,FX_COUNT-1);setEffectFavorite(e,!effectFavorite(e));server.send(200,"application/json",String("{\"favorite\":")+(effectFavorite(e)?"true":"false")+"}");});
  server.on("/api/settings/export",HTTP_GET,exportSettings);
  server.on("/api/settings/import",HTTP_POST,importSettings);
  server.on("/api/extended",HTTP_GET,sendExtendedState);
  server.on("/api/automation",HTTP_POST,receiveAutomation);
  server.on("/api/user-preset",HTTP_POST,[]{uint8_t slot=constrain(server.arg("slot").toInt(),0,7);String action=server.arg("action");if(action=="save")captureUserPreset(slot,server.arg("name"));else if(action=="load")applyUserPreset(slot);else if(action=="delete"){memset(&userPresets[slot],0,sizeof(UserPreset));saveUserPresets();}server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/preset",HTTP_POST,[]{applyPreset(constrain(server.arg("id").toInt(),0,5));server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/event",HTTP_POST,[]{triggerEvent(server.arg("type"));server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/waste",HTTP_POST,[]{setWaste(server.arg("value"));if(server.arg("show")=="1")startWasteOverlay();if(mqttClient.connected())mqttPublish(mqttTopic("waste/state"),wastePending?String(wasteTypes):"none");server.send(200,"application/json","{\"ok\":true}");});

  server.on("/api/waste/ack",HTTP_POST,[]{acknowledgeWaste();if(mqttClient.connected()){mqttPublish(mqttTopic("waste/state"),"none");mqttPublish(mqttTopic("waste/ack/set"),"ON",false);}server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/climate",HTTP_POST,[]{showRoomClimate();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/game",HTTP_POST,[]{startGame(server.arg("name"));server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/control",HTTP_POST,[]{gameControl(server.arg("cmd"),constrain(server.arg("player").toInt(),1,2));server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/timer",HTTP_POST,[]{uint32_t seconds=constrain(server.arg("seconds").toInt(),1,86400);countdownDurationMs=seconds*1000UL;countdownEndMs=millis()+countdownDurationMs;countdownActive=true;mqttDirtyMask|=MD_TIMER;server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/timer/cancel",HTTP_POST,[]{countdownActive=false;mqttDirtyMask|=MD_TIMER;server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/led-test",HTTP_POST,[]{startLedHardwareTest();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/api/word-test",HTTP_POST,[]{startWordHardwareTest();server.send(200,"application/json","{\"ok\":true}");});
  server.on("/wifi-hilfe",HTTP_GET,[]{
    server.send(200,"text/html; charset=utf-8",
      "<!doctype html><html lang='de'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Wortuhr WLAN-Hilfe</title><body style='font:17px system-ui;max-width:680px;margin:3em auto;padding:1em'>"
      "<h1>Wortuhr: WLAN wiederherstellen</h1>"
      "<p>Wenn die Wortuhr kein WLAN findet, startet automatisch das Konfigurationsportal."
      " Mit dem Handy das Netz <b>Wortuhr_XXXXXX</b> waehlen und <b>192.168.4.1</b> aufrufen.</p>"
      "<h2>WLAN-Daten bewusst loeschen</h2><p>Strom einschalten, innerhalb von 5 Sekunden"
      " wieder ausschalten. Dies insgesamt zweimal tun, beim dritten Einschalten bleiben"
      " die WLAN-Daten geloescht und das Konfigurationsportal startet.</p>"
      "<p>Nur echte Power-on-Starts zaehlen. Nach 5 Sekunden durchgehendem Betrieb"
      " wird die Folge verworfen. Die Zeit ohne Strom wird nicht gemessen.</p>"
      "<p>Ein langer Tastendruck fuehrt <b>keinen</b> WLAN-Reset mehr aus."
      " Alternativ bleibt die geschuetzte WLAN-Resetfunktion der Weboberflaeche verfuegbar.</p>"
      "<p><a href='/'>Zurueck zur Wortuhr</a></p></body></html>");
  });
  server.on("/api/wifi-reset",HTTP_POST,[]{if(!server.authenticate(cfg.adminUser,cfg.adminPass)){server.requestAuthentication();return;}server.send(200,"application/json","{\"ok\":true}");wifiResetPending=true;wifiResetAt=millis()+750;});
  updater.setup(&server,"/update",cfg.adminUser,cfg.adminPass);
  if(WiFi.status()==WL_CONNECTED){startWebServices();if(MDNS.begin(cfg.hostName)){MDNS.addService("http","tcp",80);mdnsStarted=true;}}
  displayMode=MODE_CLOCK;temperatureCycleStartedMs=millis();
  if(cfg.weatherSource==0)nextWeatherAttemptMs=millis()+5000UL;
  Serial.print("[Setup] Resetgrund: ");Serial.println(resetReasonName());Serial.println("[Setup] Zugangsdaten werden nicht im Serial-Monitor ausgegeben");Serial.println("[Setup] WebGUI aktiv; Wetterabruf startet verzoegert");
}

void updatePeriodicDisplay(unsigned long now){
  if(gameMode!=GAME_NONE||overlayMode!=OVERLAY_NONE||displayMode==MODE_SCROLL)return;
  bool temperatureWindow=cfg.showTemperature&&weatherValid&&((now-temperatureCycleStartedMs)%60000UL)>=55000UL;
  displayMode=temperatureWindow?MODE_TEMPERATURE:MODE_CLOCK;
}

void checkWasteReminder(unsigned long now){
  if(!wastePending||!nowTmValid||overlayMode!=OVERLAY_NONE||gameMode!=GAME_NONE||displayMode==MODE_SCROLL)return;
  uint8_t h=nowTm.tm_hour;
  if((h>=17||(h>=6&&h<12))&&(lastWasteReminderMs==0||now-lastWasteReminderMs>=720000UL)){lastWasteReminderMs=now;startWasteOverlay();}
}

void loop(){
  disarmPowerCycleWifiReset(millis());
  wifiManager.process();updateWifiState();

  {
    static unsigned long lostSince=0, lastReconnect=0;
    unsigned long t=millis();
    if(WiFi.status()==WL_CONNECTED){
      lostSince=0;
      if(wifiUiMode==WIFI_UI_CONNECTING && !wifiPortalActive)wifiUiMode=WIFI_UI_NONE;
    }else if(!wifiPortalActive){
      if(!lostSince)lostSince=t;
      if(!lastReconnect || t-lastReconnect>=20000UL){
        lastReconnect=t;Serial.println("[WiFi] Recovery: Reconnect");WiFi.reconnect();
      }
      if(t-lostSince>=180000UL){
        Serial.println("[WiFi] Recovery: 180 s offline, Neustart");
        Serial.flush();ESP.restart();
      }
    }else lostSince=0;
  }

  if(wifiPortalActive && WiFi.status()!=WL_CONNECTED && !wifiAccessPointRunning()){
    static unsigned long lastPortalRetryMs=0;
    unsigned long checkNow=millis();
    if(checkNow>15000UL && (lastPortalRetryMs==0 || checkNow-lastPortalRetryMs>=30000UL)){
      lastPortalRetryMs=checkNow;
      Serial.println("[WiFi] SoftAP fehlt: Konfigurationsportal wird neu gestartet");
      wifiManager.stopConfigPortal();
      WiFi.mode(WIFI_AP_STA);
      wifiManager.startConfigPortal(wifiApName.c_str(),cfg.apPass);
      lastPortalClientMs=millis();
    }
  }
  if(wifiPortalActive && wifiAccessPointRunning() && WiFi.status()!=WL_CONNECTED && wifiUiMode!=WIFI_UI_PORTAL){wifiUiMode=WIFI_UI_PORTAL;wifiPortalPage=0;setWifiUiText("CONFIG WIFI");Serial.print("[WiFi] SoftAP bestaetigt: ");Serial.println(WiFi.softAPIP());}
  if(webServerStarted){server.handleClient();wsServer.loop();}unsigned long now=millis();

  updateWifiUi(now);
  if(wifiResetPending&&(long)(now-wifiResetAt)>=0){wifiManager.resetSettings();delay(100);ESP.restart();}
  if(restartPending&&(long)(now-restartAtMs)>=0){if(settingsSavePending){settingsSavePending=false;saveSettings();}delay(100);ESP.restart();}
  if(wifiPortalActive&&wifiAccessPointRunning()){
    if(WiFi.softAPgetStationNum()>0)lastPortalClientMs=now;
    else if(now-lastPortalClientMs>=300000UL){Serial.println("[WiFi] Portal 5 Minuten ohne Client, Neustart");delay(100);ESP.restart();}
  }
  if(wifiUiMode!=WIFI_UI_NONE){if(now-lastFrameMs>=40){lastFrameMs=now;renderWifiUi(now);renderedFrames++;}yield();return;}
  if(settingsSavePending&&(long)(now-settingsSaveAtMs)>=0){settingsSavePending=false;saveSettings();}
  if(restartPending&&(long)(now-restartAtMs)>=0){if(settingsSavePending){settingsSavePending=false;saveSettings();}delay(100);ESP.restart();}
  if(now-lastFpsMeasureMs>=1000){measuredFps=min(255,(int)((uint32_t)renderedFrames*1000UL/(now-lastFpsMeasureMs)));renderedFrames=0;lastFpsMeasureMs=now;}
  if(cfg.mqttEnabled&&WiFi.status()==WL_CONNECTED){mqttConnect();mqttClient.loop();if(mqttDirtyMask&&mqttClient.connected()){uint32_t dirty=mqttDirtyMask;mqttDirtyMask=0;mqttPublishDirtyState(dirty);}}else if(mqttClient.connected())mqttClient.disconnect();
  if(mqttClient.connected()&&now-lastDiagnosticPublishMs>=60000UL){lastDiagnosticPublishMs=now;mqttPublish(mqttTopic("diagnostic/heap"),String(ESP.getFreeHeap()));mqttPublish(mqttTopic("diagnostic/uptime"),String(now/1000UL));mqttPublish(mqttTopic("diagnostic/rssi"),String(WiFi.RSSI()));if(countdownActive)mqttPublish(mqttTopic("timer/state"),String(max(0L,(long)(countdownEndMs-now))/1000L));}

  if(updateTimeCache(now))checkAutomations();
  if(overlayMode!=OVERLAY_NONE&&(long)(now-overlayUntilMs)>=0){overlayMode=OVERLAY_NONE;if(weatherScrollPending){weatherScrollPending=false;startScroll(weatherText);}else if(wasteScrollPending){wasteScrollPending=false;startScroll(String(wasteDayWord())+"  "+wasteLabel());}}

  if(hardwareButtonEnabled){
  bool reading=digitalRead(hardwareButtonPin);if(reading!=lastButtonReading){buttonChangedMs=now;lastButtonReading=reading;}
  if(now-buttonChangedMs>35&&reading!=stableButtonState){
    stableButtonState=reading;
    if(stableButtonState==LOW){
      Serial.println("[Taster] Gedrueckt");
      if(gameMode==GAME_FLAPPY||gameMode==GAME_DINO)gameControl("flap",1);
      else{buttonPressStartedMs=now;buttonLongActionDone=false;buttonPowerDone=false;}
    }else if(buttonPressStartedMs){
      if(!buttonLongActionDone&&!buttonPowerDone&&wifiUiMode!=WIFI_UI_PORTAL&&gameMode==GAME_NONE){
        if(cfg.buttonMultiEnabled){buttonClicks++;lastClickReleaseMs=now;}else startWeatherScroll();
      }
      buttonPressStartedMs=0;
    }
  }
  if(buttonClicks&&(now-lastClickReleaseMs>=350UL)){uint8_t c=buttonClicks;buttonClicks=0;if(c==1)startWeatherScroll();else nextEffectByButton();}
  if(cfg.buttonMultiEnabled&&stableButtonState==LOW&&buttonPressStartedMs&&!buttonPowerDone&&now-buttonPressStartedMs>=1000UL){
    buttonPowerDone=true;
    if(gameMode!=GAME_NONE)startGame("none");
    else{cfg.power=!cfg.power;scheduleSettingsSave();mqttDirtyMask|=MD_POWER|MD_LIGHT;}
  }
  }

  if(displayMode==MODE_SCROLL&&overlayMode==OVERLAY_NONE&&now-lastScrollStepMs>=cfg.scrollSpeed){lastScrollStepMs=now;scrollX--;int width=scrollText.length()*6;if(scrollX< -width){displayMode=MODE_CLOCK;processMessageQueue();}}
  updatePeriodicDisplay(now);
  updateRotation(now);
  updateCircadian(now);
  if(now-lastLdrMs>=250&&!(ledTestUntilMs&&(long)(now-ledTestUntilMs)<0)&&!(wordTestUntilMs&&(long)(now-wordTestUntilMs)<0)){lastLdrMs=now;currentLdrRaw=analogRead(LDR_PIN);int32_t raw8=(int32_t)currentLdrRaw*8;if(ldrAverage8<0)ldrAverage8=raw8;else ldrAverage8+=(raw8-ldrAverage8)/cfg.ldrSmoothing;updateBrightnessFromLdr();}
  if(cfg.weatherSource==0&&WiFi.status()==WL_CONNECTED&&(weatherRefreshPending||(long)(now-nextWeatherAttemptMs)>=0)){weatherRefreshPending=false;requestWeather(false);}
  checkWasteReminder(now);
  if(weatherTaskResultReady){
    WeatherTaskData result;bool ok;portENTER_CRITICAL(&weatherTaskMux);result=weatherTaskResult;ok=weatherTaskResultOk;weatherTaskResultReady=false;portEXIT_CRITICAL(&weatherTaskMux);
    if(ok&&(cfg.weatherSource==0)&&fabs(cfg.weatherLat-result.lat)<0.0001f&&fabs(cfg.weatherLon-result.lon)<0.0001f){currentTemperature=result.currentTemp;todayMax=result.todayMax;tomorrowMax=result.tomorrowMax;todayMin=result.todayMin;tomorrowMin=result.tomorrowMin;currentWeatherCode=result.currentCode;todayWeatherCode=result.todayCode;tomorrowWeatherCode=result.tomorrowCode;if(result.sunriseMin>=0)sunriseMinutes=result.sunriseMin;if(result.sunsetMin>=0)sunsetMinutes=result.sunsetMin;lastCircadianMs=0;weatherRuntimeLocation=result.location;weatherValid=true;rebuildWeatherText();weatherFailureCount=0;lastWeatherMs=now;nextWeatherAttemptMs=now+900000UL;mqttDirtyMask|=MD_WEATHER;if(weatherShowAfterFetch){weatherShowAfterFetch=false;startWeatherScroll();}}
    else{weatherFailureCount++;nextWeatherAttemptMs=now+300000UL;if(weatherFailureCount>=3){weatherValid=false;weatherText=String(cfg.weatherLocation)+"  WETTER --";}if(weatherShowAfterFetch){weatherShowAfterFetch=false;enqueueMessage("WETTER --",2);}}
  }
  if(gameMode!=GAME_NONE&&now-lastGameInputMs>=(uint32_t)cfg.gameTimeoutSec*1000UL){Serial.println("[Game] Timeout, zurueck zur Uhr");startGame("none");}
  if(countdownActive&&(long)(now-countdownEndMs)>=0){countdownActive=false;mqttDirtyMask|=MD_TIMER;enqueueMessage("TIMER ABGELAUFEN",3);}
  processMessageQueue();
  if(ledTestUntilMs&&(long)(now-ledTestUntilMs)<0){renderLedHardwareTest(now);yield();return;}else if(ledTestUntilMs){ledTestUntilMs=0;ldrAverage8=-1;}
  if(wordTestUntilMs&&(long)(now-wordTestUntilMs)<0){renderWordHardwareTest();yield();return;}else if(wordTestUntilMs){wordTestUntilMs=0;ldrAverage8=-1;}

  if(!cfg.power&&overlayMode==OVERLAY_NONE&&displayMode!=MODE_SCROLL){if(now-lastFrameMs>=250){lastFrameMs=now;fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();}yield();return;}
  updateGame(now);
  if(now-lastRuntimePersistMs>=60000UL){lastRuntimePersistMs=now;runtimePrefs.putULong("last_uptime",now/1000UL);}
  unsigned long frameInterval=desiredFrameInterval();if(now-lastFrameMs>=frameInterval){lastFrameMs=now;renderFrame();renderedFrames++;}else skippedStaticFrames++;yield();
}
