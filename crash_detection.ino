#include <Wire.h>
#include <WiFiS3.h>
#include <TinyGPSPlus.h>

// ==========================================
// 1. SYSTEM CONFIGURATION & HARDWARE PINS
// ==========================================
char ssid[] = "DESKTOP"; 
char pass[] = "hilol123"; 
int status = WL_IDLE_STATUS;
WiFiServer server(80);

char fastApiServer[] = "192.168.137.1"; 
int fastApiPort = 8000;
WiFiClient apiClient;
bool cloudAlertSent = false;

const int BUZZER_PIN = 8;
const int BUTTON_PIN = 7;

unsigned long lastBeepTime = 0;
bool buzzerState = false;
const unsigned long BEEP_INTERVAL = 1000;

const int MPU_ADDR = 0x68; 
const float ACCEL_SCALE = 2048.0; 
float xG = 0, yG = 0, zG = 0, totalG = 0;
bool sensorError = false;
bool crashDetected = false;

const float CRASH_THRESHOLD_G = 4.5; 
int consecutiveImpacts = 0;

unsigned long lastRebootAttempt = 0;
const unsigned long REBOOT_COOLDOWN = 5000; 

TinyGPSPlus gps;
String currentNMEA = "";
String lastValidNMEA = "HARDCODED DEMO MODE";

// ==========================================
// 2. HTTP POST TO FASTAPI
// ==========================================
void sendCrashToCloud() {
  Serial.print("\n[CLOUD] Connecting to FastAPI at ");
  Serial.print(fastApiServer);
  Serial.println("...");

  if (apiClient.connect(fastApiServer, fastApiPort)) {
    String jsonPayload = "{";
    jsonPayload += "\"device_id\": \"Uno-R4-Alpha\",";
    jsonPayload += "\"latitude\": 18.464151,";
    jsonPayload += "\"longitude\": 73.867696,";
    jsonPayload += "\"status\": \"SEVERE CRASH DETECTED (" + String(totalG, 1) + "G)\"";
    jsonPayload += "}";

    apiClient.println("POST /api/incidents HTTP/1.1");
    apiClient.print("Host: "); apiClient.println(fastApiServer);
    apiClient.println("Content-Type: application/json");
    apiClient.print("Content-Length: "); apiClient.println(jsonPayload.length());
    apiClient.println(); 
    apiClient.print(jsonPayload);
    
    Serial.println("[CLOUD] Crash telemetry successfully pushed to database.");
    delay(10);
    apiClient.stop();
  } else {
    Serial.println("[CLOUD ERROR] Failed to connect to FastAPI server.");
  }
}

// ==========================================
// 3. THE HTML/JS FRONTEND
// ==========================================
const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RescueLink Telematics</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #121212; color: #e0e0e0; text-align: center; margin: 0; padding: 20px; }
        h1 { color: #ffffff; letter-spacing: 1px; }
        .grid { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; margin-top: 20px; }
        .card { background: #1e1e1e; padding: 25px; border-radius: 12px; width: 320px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); text-align: left; transition: border 0.3s ease; border: 2px solid transparent; }
        .card h2 { margin-top: 0; border-bottom: 1px solid #333; padding-bottom: 10px; color: #64b5f6; }
        .data-row { display: flex; justify-content: space-between; font-size: 1.1em; margin: 12px 0; }
        .value { font-weight: bold; color: #fff; font-family: monospace; font-size: 1.2em; }
        .alert-banner { display: none; background: #d32f2f; color: white; padding: 15px; font-size: 24px; font-weight: bold; border-radius: 8px; margin-bottom: 20px; animation: pulse 1s infinite; box-shadow: 0 0 20px #d32f2f; }
        .reset-btn { margin-top: 15px; padding: 10px 20px; background: white; color: #d32f2f; border: none; border-radius: 6px; font-size: 16px; font-weight: bold; cursor: pointer; transition: 0.2s; }
        .reset-btn:hover { background: #f5f5f5; transform: scale(1.05); }
        .status-ok { color: #4caf50; font-weight: bold; }
        .status-err { color: #f44336; font-weight: bold; }
        @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.8; } 100% { opacity: 1; } }
    </style>
</head>
<body>
    <h1>RescueLink Live Dashboard</h1>
    
    <div id="crash-banner" class="alert-banner">
        ⚠️ SEVERE IMPACT DETECTED! ⚠️<br>
        <button class="reset-btn" onclick="clearAlert()">Acknowledge & Clear Alert</button>
    </div>
    
    <div class="grid">
        <div class="card" id="mpu-card">
            <h2>MPU6050 Stats</h2>
            <div class="data-row"><span>Sensor Health:</span> <span id="mpu-status" class="status-ok">Connecting...</span></div>
            <div class="data-row"><span>X-Axis:</span> <span class="value"><span id="x">0.00</span> G</span></div>
            <div class="data-row"><span>Y-Axis:</span> <span class="value"><span id="y">0.00</span> G</span></div>
            <div class="data-row"><span>Z-Axis:</span> <span class="value"><span id="z">0.00</span> G</span></div>
            <hr style="border-color:#333; margin: 15px 0;">
            <div class="data-row" style="font-size: 1.4em; color: #ffb74d;"><span>Total Force:</span> <span class="value"><span id="g">0.00</span> G</span></div>
        </div>

        <div class="card">
            <h2>NEO 6M Data</h2>
            <div class="data-row"><span>Latitude:</span> <span class="value" id="lat">18.464151</span></div>
            <div class="data-row"><span>Longitude:</span> <span class="value" id="lon">73.867696</span></div>
        </div>
    </div>

    <script>
        function clearAlert() {
            fetch('/reset')
            .then(() => {
                document.getElementById('crash-banner').style.display = "none";
                document.getElementById('mpu-card').style.border = "2px solid transparent";
            });
        }

        function fetchLiveData() {
            fetch('/data')
            .then(response => response.json())
            .then(data => {
                if (data.mpu.error) {
                    document.getElementById('mpu-status').innerText = "FAULT / REBOOTING";
                    document.getElementById('mpu-status').className = "status-err";
                    document.getElementById('x').innerText = "---";
                    document.getElementById('y').innerText = "---";
                    document.getElementById('z').innerText = "---";
                    document.getElementById('g').innerText = "---";
                } else {
                    document.getElementById('mpu-status').innerText = "Online & Verified";
                    document.getElementById('mpu-status').className = "status-ok";
                    document.getElementById('x').innerText = data.mpu.x;
                    document.getElementById('y').innerText = data.mpu.y;
                    document.getElementById('z').innerText = data.mpu.z;
                    document.getElementById('g').innerText = data.mpu.g;
                }

                if (data.mpu.crash) {
                    document.getElementById('crash-banner').style.display = "block";
                    document.getElementById('mpu-card').style.border = "2px solid #d32f2f";
                } else {
                    document.getElementById('crash-banner').style.display = "none";
                    document.getElementById('mpu-card').style.border = "2px solid transparent";
                }

                document.getElementById('lat').innerText = data.gps.lat;
                document.getElementById('lon').innerText = data.gps.lon;
            })
            .catch(error => console.log("Connection lost..."));
        }

        setInterval(fetchLiveData, 250); 
    </script>
</body>
</html>
)rawliteral";

// ==========================================
// 4. FIRMWARE LOGIC
// ==========================================

void rebootSensor() {
  Wire.end(); 
  delay(10);
  Wire.begin();
  Wire.setClock(400000); 
  
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission(true);
  delay(10); 
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1C); Wire.write(0x18); Wire.endTransmission(true);
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);
  
  delay(2000);

  Serial.println("\n=== RESCUELINK BOOTING ===");
  rebootSensor();

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("CRITICAL: WiFi module failed!");
    while (true); 
  }

  Serial.print("Connecting to Network: "); Serial.println(ssid);
  while (status != WL_CONNECTED) {
    Serial.print(".");
    status = WiFi.begin(ssid, pass);
    delay(3000); 
  }

  server.begin();
  
  Serial.println("\n=== SYSTEM ONLINE ===");
  Serial.print("Dashboard IP: http://");
  Serial.println(WiFi.localIP());
}

String buildJSON() {
  String json = "{";
  json += "\"mpu\":{";
  json += "\"x\":\"" + String(xG, 2) + "\",";
  json += "\"y\":\"" + String(yG, 2) + "\",";
  json += "\"z\":\"" + String(zG, 2) + "\",";
  json += "\"g\":\"" + String(totalG, 2) + "\",";
  json += "\"error\":" + String(sensorError ? "true" : "false") + ",";
  json += "\"crash\":" + String(crashDetected ? "true" : "false");
  json += "},\"gps\":{";
  json += "\"lat\":\"18.464151\",";
  json += "\"lon\":\"73.867696\",";
  json += "\"sats\":0,";
  json += "\"raw\":\"" + lastValidNMEA + "\"";
  json += "}}";
  return json;
}

void loop() {
  if (crashDetected) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      crashDetected = false;
      consecutiveImpacts = 0;
      cloudAlertSent = false; 
      Serial.println("[SYSTEM] Hardware Override: Crash Alert Cleared.");
    } 
    else if (millis() - lastBeepTime >= BEEP_INTERVAL) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      lastBeepTime = millis();
    }
  } else {
    if (buzzerState) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerState = false;
    }
  }

  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) {
    sensorError = true;
    xG = yG = zG = totalG = 0;
    if (millis() - lastRebootAttempt >= REBOOT_COOLDOWN) {
      rebootSensor();
      lastRebootAttempt = millis();
    }
  } else {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x1C); 
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 1, true);
    
    if (Wire.read() != 0x18) {
      sensorError = true;
      if (millis() - lastRebootAttempt >= REBOOT_COOLDOWN) {
        rebootSensor();
        lastRebootAttempt = millis();
      }
    } else {
      sensorError = false;
      Wire.beginTransmission(MPU_ADDR); Wire.write(0x3B); Wire.endTransmission(false);
      Wire.requestFrom(MPU_ADDR, 6, true);
      
      int16_t accelX = (Wire.read() << 8 | Wire.read());
      int16_t accelY = (Wire.read() << 8 | Wire.read());
      int16_t accelZ = (Wire.read() << 8 | Wire.read());

      xG = accelX / ACCEL_SCALE;
      yG = accelY / ACCEL_SCALE;
      zG = accelZ / ACCEL_SCALE;
      totalG = sqrt((xG * xG) + (yG * yG) + (zG * zG));

      if (totalG < 0.1) {
        sensorError = true;
        rebootSensor(); 
      } else if (totalG >= CRASH_THRESHOLD_G) {
        consecutiveImpacts++;
        if (consecutiveImpacts >= 2 && !crashDetected) { 
          crashDetected = true;
          Serial.print("\n[IMPACT DETECTED] Peak Force: ");
          Serial.print(totalG);
          Serial.println(" G");

          if (!cloudAlertSent) {
            sendCrashToCloud();
            cloudAlertSent = true;
          }
        }
      } else {
        consecutiveImpacts = 0;
      }
    }
  }

  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    bool isDataRequest = false;
    bool isResetRequest = false;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            if (isDataRequest) {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
              client.print(buildJSON());
            } else if (isResetRequest) {
              client.println("HTTP/1.1 200 OK");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
            } else {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html");
              client.println("Connection: close");
              client.println();
              client.print(index_html);
            }
            break;
            
          } else {
            if (currentLine.startsWith("GET /data")) {
              isDataRequest = true;
            } else if (currentLine.startsWith("GET /reset")) {
              crashDetected = false;
              consecutiveImpacts = 0;
              cloudAlertSent = false;
              isResetRequest = true;
              Serial.println("[SYSTEM] Web Dashboard Override: Crash Alert Cleared.");
            }
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    delay(1);
    client.stop();
  }
}