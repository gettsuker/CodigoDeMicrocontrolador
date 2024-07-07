#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Servo.h>
#include <vector>
#include <sstream>

struct ServoPins {
  Servo servo;
  int servoPin;
  String servoName;
  int initialPosition;  
};

std::vector<ServoPins> servoPins = {
  { Servo(), 27 , "Base", 90},
  { Servo(), 26 , "Shoulder", 90},
  { Servo(), 25 , "Elbow", 90},
  { Servo(), 33 , "Gripper", 90},
   { Servo(), 14 , "Wrist", 90},
};

struct RecordedStep
{
  int servoIndex;
  int value;
  int delayInStep;
};
std::vector<RecordedStep> recordedSteps;

bool recordSteps = false;
bool playRecordedSteps = false;

unsigned long previousTimeInMilli = millis();

const char* ssid = "RobotArm";
const char* password = "12345678";

AsyncWebServer server(80);
AsyncWebSocket wsRobotArmInput("/RobotArmInput");

const char htmlHomePage[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 Robot Arm</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
  <h1>ESP32 Robot Arm Control</h1>
  <p>Use the mobile app to control the robot arm.</p>
</body>
</html>)rawliteral";

void handleRoot(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", htmlHomePage);
}

void onRobotArmInputWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        std::string myData = "";
        myData.assign((char *)data, len);
        std::istringstream ss(myData);
        std::string key, value;
        std::getline(ss, key, ',');
        std::getline(ss, value, ',');

        Serial.printf("Received message - Key: %s, Value: %s\n", key.c_str(), value.c_str());

        int valueInt = atoi(value.c_str());

        if (key == "Base") {
          writeServoValues(0, valueInt);
         // servoPins[0].servo.write(valueInt);
        } else if (key == "Shoulder") {
           writeServoValues(1, valueInt);
          servoPins[1].servo.write(valueInt);
        } else if (key == "Elbow") {
           writeServoValues(2, valueInt);
          servoPins[2].servo.write(valueInt);
        } else if (key == "Gripper") {
           writeServoValues(3, valueInt);
          servoPins[3].servo.write(valueInt);
        } else if (key == "Wrist") {
           writeServoValues(4, valueInt);
          servoPins[4].servo.write(valueInt);
        }else if (key == "Record")
        {
          recordSteps = valueInt;
          if (recordSteps)
          {
            recordedSteps.clear();
            previousTimeInMilli = millis();
          }
        }  
        else if (key == "Play")
        {
          playRecordedSteps = valueInt;
        }
      }
      break;
    }
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  
  WiFi.softAP(ssid, password);
  Serial.println("Setting AP (Access Point)…");
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  wsRobotArmInput.onEvent(onRobotArmInputWebSocketEvent);
  server.addHandler(&wsRobotArmInput);
  
  server.on("/", HTTP_GET, handleRoot);
  server.begin();

  for (auto& servoPin : servoPins) {
    servoPin.servo.attach(servoPin.servoPin);
    servoPin.servo.write(servoPin.initialPosition);
  }
}


void loop() {
  wsRobotArmInput.cleanupClients();
  if (playRecordedSteps)
  { 
    playRecordedRobotArmSteps();
  }
}

void writeServoValues(int servoIndex, int value) {
   if (recordSteps)
  {
    RecordedStep recordedStep;       
    if (recordedSteps.size() == 0) // We will first record initial position of all servos. 
    {
      for (int i = 0; i < servoPins.size(); i++)
      {
        recordedStep.servoIndex = i; 
        recordedStep.value = servoPins[i].servo.read(); 
        recordedStep.delayInStep = 0;
        recordedSteps.push_back(recordedStep);         
      }      
    }
    unsigned long currentTime = millis();
    recordedStep.servoIndex = servoIndex; 
    recordedStep.value = value; 
    recordedStep.delayInStep = currentTime - previousTimeInMilli;
    recordedSteps.push_back(recordedStep);  
    previousTimeInMilli = currentTime;         
  }
  servoPins[servoIndex].servo.write(value);  
  Serial.printf("Moving %s to %d\n", servoPins[servoIndex].servoName.c_str(), value); // Debug statement
}
void playRecordedRobotArmSteps()
{
  if (recordedSteps.size() == 0)
  {
    return;
  }
  //This is to move servo to initial position slowly. First 4 steps are initial position    
  for (int i = 0; i < 4 && playRecordedSteps; i++)
  {
    RecordedStep &recordedStep = recordedSteps[i];
    int currentServoPosition = servoPins[recordedStep.servoIndex].servo.read();
    while (currentServoPosition != recordedStep.value && playRecordedSteps)  
    {
      currentServoPosition = (currentServoPosition > recordedStep.value ? currentServoPosition - 1 : currentServoPosition + 1); 
      servoPins[recordedStep.servoIndex].servo.write(currentServoPosition);
      wsRobotArmInput.textAll(servoPins[recordedStep.servoIndex].servoName + "," + currentServoPosition);
      delay(50);
    }
  }
  delay(2000); // Delay before starting the actual steps.
  
  for (int i = 4; i < recordedSteps.size() && playRecordedSteps ; i++)
  {
    RecordedStep &recordedStep = recordedSteps[i];
    delay(recordedStep.delayInStep);
    servoPins[recordedStep.servoIndex].servo.write(recordedStep.value);
    wsRobotArmInput.textAll(servoPins[recordedStep.servoIndex].servoName + "," + recordedStep.value);
  }
}

void sendCurrentRobotArmState() {
  for (auto& servoPin : servoPins) {
    String message = servoPin.servoName + "," + String(servoPin.servo.read());
    wsRobotArmInput.textAll(message);
  }
}
