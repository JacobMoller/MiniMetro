#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

//  WIFI 

const char* ssid = "ssid";
const char* password = "pass";

WebServer server(80);

// PINS

#define SERVO_PIN      18

#define RED_PIN        25
#define GREEN_PIN      26
#define BLUE_PIN       27

#define MOTOR_A1       16
#define MOTOR_A2       4
#define MOTOR_B1       17
#define MOTOR_B2       5

#define HALL_PIN       34

#define FRONT_IR_PIN   32
#define SPEED_IR_PIN   33

// SERVO

Servo doorServo;

const int OPEN_ANGLE   = 0;
const int CLOSED_ANGLE = 160;

// STATES

enum SystemState {
  IDLE,
  RUNNING,
  OBSTACLE_STOP,
  STOPPED_AT_STATION,
  DOORS_OPENING,
  DOORS_OPEN,
  DOORS_CLOSING
};

SystemState state = IDLE;

// TIMERS

unsigned long doorTimer = 0;

// STATIONS

int stationCount = 0;
bool lastHallState = HIGH;

// WEB CONTROL

bool systemStarted = false;

// DIRECTION

bool movingForwardDirection = true;

// SPEED SENSOR

const float STRIP_DISTANCE_CM = 31.2;

bool lastSpeedIR = HIGH;

unsigned long lastStripTime = 0;

float currentSpeedCms = 0.0;

// POSITION

float estimatedDistanceMeters = 0.0;

// FRONT IR

bool obstacleDetected = false;

// LED

void setLED(bool r, bool g, bool b) {
  digitalWrite(RED_PIN, r);
  digitalWrite(GREEN_PIN, g);
  digitalWrite(BLUE_PIN, b);
}

void setLEDWhite() {
  setLED(HIGH, HIGH, HIGH);
}

void setLEDRed() {
  setLED(HIGH, LOW, LOW);
}

void setLEDYellow() {
  setLED(HIGH, HIGH, LOW);
}

// OLED

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

static unsigned long displayTimer = 0;

// STATION NAMES

String stationNames[] = {
  "Forum",
  "DR Byen",
  "Kastrup"
};

const int TOTAL_STATIONS = 3;


// MOTOR
void stopMotors() {

  digitalWrite(MOTOR_A1, LOW);
  digitalWrite(MOTOR_A2, LOW);

  digitalWrite(MOTOR_B1, LOW);
  digitalWrite(MOTOR_B2, LOW);
}

void moveForward() {

  movingForwardDirection = true;

  digitalWrite(MOTOR_A1, HIGH);
  digitalWrite(MOTOR_A2, LOW);

  digitalWrite(MOTOR_B1, LOW);
  digitalWrite(MOTOR_B2, HIGH);
}

void moveBackward() {

  movingForwardDirection = false;

  digitalWrite(MOTOR_A1, LOW);
  digitalWrite(MOTOR_A2, HIGH);

  digitalWrite(MOTOR_B1, HIGH);
  digitalWrite(MOTOR_B2, LOW);
}

// DOORS
void openDoors() {
  doorServo.write(OPEN_ANGLE);
  Serial.println("[DOOR] OPEN");
}

void closeDoors() {
  doorServo.write(CLOSED_ANGLE);
  Serial.println("[DOOR] CLOSE");
}

// TRAIN CONTROL

void resumeTrain() {

  if (movingForwardDirection) {
    moveForward();
    setLEDWhite();
  } else {
    moveBackward();
    setLEDRed();
  }

  state = RUNNING;

  Serial.println("[TRAIN] RESUMED");
}

void reverseDirection() {

  movingForwardDirection = !movingForwardDirection;

  Serial.println("[TRAIN] REVERSING");

  delay(500);

  if (movingForwardDirection) {
    moveForward();
  } else {
    moveBackward();
  }
}

//  WEB PAGE

String webpage() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>

<meta name="viewport" content="width=device-width, initial-scale=1">

<title>ESP32 Metro</title>

<style>

body{
  background:#111;
  color:white;
  font-family:Arial;
  text-align:center;
  padding:20px;
}

.card{
  background:#1e1e1e;
  border-radius:16px;
  padding:20px;
  max-width:800px;
  margin:auto;
}

button{
  width:220px;
  height:65px;
  margin:10px;
  border:none;
  border-radius:12px;
  font-size:18px;
  cursor:pointer;
}

input{
  width:150px;
  height:45px;
  font-size:22px;
  text-align:center;
  border-radius:10px;
  border:none;
}

.start{ background:green; color:white; }
.stop{ background:red; color:white; }
.blue{ background:#0066ff; color:white; }
.orange{ background:orange; color:black; }
.purple{ background:purple; color:white; }

.info{
  font-size:24px;
  margin:12px;
}

.track{
  width:90%;
  height:30px;
  background:#333;
  border-radius:20px;
  margin:30px auto;
  position:relative;
}

.train{
  width:40px;
  height:30px;
  background:#00ffcc;
  border-radius:8px;
  position:absolute;
  left:0%;
  transition:0.3s;
}

.station{
  width:6px;
  height:30px;
  background:red;
  position:absolute;
}

</style>
</head>

<body>

<h1>ESP32 Metro Control</h1>

<div class="card">

<div class="info" id="status">State: ---</div>
<div class="info" id="speed">Speed: 0 cm/s</div>
<div class="info" id="station">Station: 0</div>
<div class="info" id="direction">Direction: Forward</div>
<div class="info" id="obstacle">Obstacle: NO</div>

<div class="track">

  <div class="station" style="left:10%"></div>
  <div class="station" style="left:50%"></div>
  <div class="station" style="left:90%"></div>

  <div class="train" id="train"></div>

</div>

<button class="start" onclick="fetch('/start')">START</button>
<button class="stop" onclick="fetch('/stop')">STOP</button>

<br>

<button class="purple" onclick="fetch('/direction')">
SWITCH DIRECTION
</button>

<br><br>

<input id="stationInput" type="number" min="0" max="99" value="0">

<br>

<button class="blue" onclick="setStation()">
SET STATION COUNT
</button>

</div>

<script>

async function updateData(){

  const res = await fetch('/status');
  const data = await res.json();

  document.getElementById("status").innerHTML =
    "State: " + data.state;

  document.getElementById("speed").innerHTML =
    "Speed: " + data.speed + " cm/s";

  document.getElementById("station").innerHTML =
    "Station Count: " + data.station;

  document.getElementById("direction").innerHTML =
    "Direction: " + data.direction;

  document.getElementById("obstacle").innerHTML =
    "Obstacle: " + data.obstacle;

  document.getElementById("train").style.left =
    data.position + "%";
}

async function setStation(){

  let value =
    document.getElementById("stationInput").value;

  await fetch("/setstation?value=" + value);
}

setInterval(updateData, 500);

</script>

</body>
</html>
)rawliteral";

  return html;
}

// STATE STRING

String stateToString() {

  switch(state) {

    case IDLE:
      return "IDLE";

    case RUNNING:
      return "RUNNING";

    case OBSTACLE_STOP:
      return "OBSTACLE";

    case STOPPED_AT_STATION:
      return "STATION";

    case DOORS_OPENING:
      return "OPENING DOORS";

    case DOORS_OPEN:
      return "DOORS OPEN";

    case DOORS_CLOSING:
      return "CLOSING DOORS";
  }

  return "UNKNOWN";
}

// WEB HANDLERS

void handleRoot() {
  server.send(200, "text/html", webpage());
}

void handleStart() {

  systemStarted = true;

  resumeTrain();

  updateDisplay();

  server.send(200, "text/plain", "Started");

  Serial.println("[SYSTEM] STARTED");
}

void handleStop() {

  systemStarted = false;

  state = IDLE;

  stopMotors();

  setLEDRed();

  updateDisplay();

  server.send(200, "text/plain", "Stopped");

  Serial.println("[SYSTEM] STOPPED");
}

void handleDirection() {

  reverseDirection();

  server.send(200, "text/plain", "Direction switched");
}

void handleSetStation() {

  if (server.hasArg("value")) {

    stationCount = server.arg("value").toInt();

    Serial.print("[WEB] Station manually set to ");
    Serial.println(stationCount);
  }

  server.send(200, "text/plain", "Station updated");
}

void handleStatus() {

  String json = "{";

  json += "\"state\":\"" + stateToString() + "\",";
  json += "\"speed\":\"" + String(currentSpeedCms,1) + "\",";
  json += "\"station\":\"" + String(stationCount) + "\",";
  json += "\"direction\":\"" +
          String(movingForwardDirection ? "Forward" : "Backward") + "\",";
  json += "\"obstacle\":\"" +
          String(obstacleDetected ? "YES" : "NO") + "\",";

  float positionPercent =
      fmod(estimatedDistanceMeters, 30.0) / 30.0;

  positionPercent *= 100.0;

  json += "\"position\":\"" +
          String(positionPercent,1) + "\"";

  json += "}";

  server.send(200, "application/json", json);
}

// DISPLAY

void updateDisplay() {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // TITLE

  display.setCursor(0,0);
  display.println("METRO");

  // NEXT STATION

  int nextStation = stationCount;

  if (movingForwardDirection) {

    nextStation++;

    if (nextStation > TOTAL_STATIONS)
      nextStation = 1;

  } else {

    nextStation--;

    if (nextStation < 1)
      nextStation = TOTAL_STATIONS;
  }

  display.setCursor(0,14);
  display.print("Next:");

  display.setCursor(0,26);
  display.setTextSize(2);
  display.println(stationNames[nextStation - 1]);

  display.setTextSize(1);

  // STATUS MESSAGE

  display.setCursor(0,50);

  switch(state) {

    case RUNNING:
      display.println("Train Running");
      break;

    case OBSTACLE_STOP:
      display.println("Obstacle Ahead");
      break;

    case DOORS_OPENING:
      display.println("Doors Opening");
      break;

    case DOORS_OPEN:
      display.println("Doors Open");
      break;

    case DOORS_CLOSING:
      display.println("Doors Closing");
      break;

    case IDLE:
      display.println("System Idle");
      break;

    default:
      display.println("At Station");
      break;
  }

  // SPEED
  display.setCursor(75,0);
  display.setTextSize(2);
  display.println(currentSpeedCms, 1);
  display.setCursor(75,14);
  display.setTextSize(1);
  display.println("cm/s");
  
  

  display.display();
}

// SETUP

void setup() {

  Serial.begin(115200);

  // LEDs
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  setLEDWhite();

  // Motors
  pinMode(MOTOR_A1, OUTPUT);
  pinMode(MOTOR_A2, OUTPUT);
  pinMode(MOTOR_B1, OUTPUT);
  pinMode(MOTOR_B2, OUTPUT);

  stopMotors();

  // Sensors
  pinMode(HALL_PIN, INPUT);
  pinMode(FRONT_IR_PIN, INPUT);
  pinMode(SPEED_IR_PIN, INPUT);

  // Servo
  ESP32PWM::allocateTimer(1);

  doorServo.setPeriodHertz(50);

  doorServo.attach(SERVO_PIN, 600, 2400);

  doorServo.write(CLOSED_ANGLE);

  // Display
  if(!display.begin(0x3C, true)) {

    Serial.println("SH1106 allocation failed");

    while(true);
  }

  display.clearDisplay();
  display.display();

  // WiFi
  WiFi.begin(ssid, password);

  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {

    delay(300);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  // Routes
  server.on("/", handleRoot);

  server.on("/start", handleStart);
  server.on("/stop", handleStop);

  server.on("/direction", handleDirection);

  server.on("/setstation", handleSetStation);

  server.on("/status", handleStatus);

  server.enableCORS(true);

  server.begin();

  Serial.println("[WEB] SERVER STARTED");
}

// LOOP
void loop() {

  server.handleClient();

  // FRONT IR

  obstacleDetected =
      (digitalRead(FRONT_IR_PIN) == LOW);

  if (systemStarted) {

    if (obstacleDetected &&
        state != OBSTACLE_STOP &&
        state != DOORS_OPEN &&
        state != DOORS_OPENING &&
        state != DOORS_CLOSING) {

      stopMotors();

      state = OBSTACLE_STOP;

      setLEDYellow();

      Serial.println("[SAFETY] OBSTACLE DETECTED");
    }

    if (!obstacleDetected &&
        state == OBSTACLE_STOP) {

      Serial.println("[SAFETY] OBSTACLE CLEARED");

      resumeTrain();
    }
  }

  // SPEED SENSOR

  bool speedIR = digitalRead(SPEED_IR_PIN);

  // Falling edge = strip detected
  if (lastSpeedIR == HIGH &&
      speedIR == LOW) {

    unsigned long now = millis();

    if (lastStripTime > 0) {

      unsigned long delta = now - lastStripTime;

      float seconds = delta / 1000.0;

      // cm/s
      currentSpeedCms =
          (STRIP_DISTANCE_CM / seconds) / 10;

      // update approximate position
      estimatedDistanceMeters +=
          STRIP_DISTANCE_CM / 100.0;

      // check if speed is unrealistic
      if(currentSpeedCms < 0 && currentSpeedCms > 10){
        currentSpeedCms = 0;
      }

      Serial.print("[SPEED] ");
      Serial.print(currentSpeedCms);
      Serial.println(" cm/s");
    }

    lastStripTime = now;
  }

  lastSpeedIR = speedIR;

  // stopped
  if (millis() - lastStripTime > 2000) {
    currentSpeedCms = 0;
  }

  // MAIN SYSTEM

  if (!systemStarted)
    return;

  // STATION DETECTION

  bool hallState = digitalRead(HALL_PIN);

  if (lastHallState == HIGH &&
      hallState == LOW) {

    stationCount++;

    Serial.print("[STATION] ");
    Serial.println(stationCount);

    stopMotors();

    setLEDYellow();

    state = STOPPED_AT_STATION;

    openDoors();

    doorTimer = millis();

    state = DOORS_OPENING;
  }

  lastHallState = hallState;

  // DOOR STATE MACHINE

  if (state == DOORS_OPENING) {

    if (millis() - doorTimer >= 1500) {

      state = DOORS_OPEN;

      doorTimer = millis();
    }
  }

  else if (state == DOORS_OPEN) {

    if (millis() - doorTimer >= 2000) {

      closeDoors();

      state = DOORS_CLOSING;

      doorTimer = millis();
    }
  }

  else if (state == DOORS_CLOSING) {

    if (millis() - doorTimer >= 1500) {

      // Reverse at station 3
      if (stationCount >= 3) {

        stationCount = 1;

        reverseDirection();
      }

      resumeTrain();
    }
  }

  // Display updates
  if (millis() - displayTimer > 250) {

    updateDisplay();

    displayTimer = millis();
  }
}