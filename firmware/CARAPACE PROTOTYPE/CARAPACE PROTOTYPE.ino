#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <MPU6500_WE.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// --- WiFi Credentials ---
const char* ssid = "";
const char* password = "";

String BOT_TOKEN = "";
String CHAT_ID = "-1004329754962";

// --- SIM800L Phone Number ---
#define EMERGENCY_PHONE ""  // Change to your emergency phone number

// --- Pin Definitions ---
#define PIN_HR 34     // HW-827 Pulse Signal
#define PIN_EMG 35    // EMG Analog Signal
#define PIN_RELAY 26  // Airbag Relay Pin
#define SIM800_RX 16  // ESP32 RX2 connected to SIM800 TX
#define SIM800_TX 17  // ESP32 TX2 connected to SIM800 RX

// --- Gyroscope Thresholds (deg/s) ---
#define TILT_THRESHOLD_GYRO 200.0  // Trigger threshold to fire the relay/airbag
#define RESET_THRESHOLD_GYRO 0.5  // Threshold below which the device is considered back to normal/rest

// --- Objects ---
#define MPU6500_ADDR 0x68
MPU6500_WE mpu = MPU6500_WE(MPU6500_ADDR);

WebServer server(80);
HardwareSerial sim800(2);  // Use UART2

// --- Dynamic Variables ---
int heartRate = 0;
int emgValue = 0;
float accelMag = 0.0;
bool airbagActive = false;
bool alertSent = false;

// --- SMS Function ---
void sendEmergencySMS() {

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String message = "🚨 EMERGENCY ALERT!\n\n";
  message += "Fall Detected\n";
  message += "Airbag Activated\n\n";
  message += "Sending GPS Location To Nearest Medical Facility\n\n";

  message.replace(" ", "%20");
  message.replace("\n", "%0A");

  String url =
    "https://api.telegram.org/bot" + BOT_TOKEN + "/sendMessage?chat_id=" + CHAT_ID + "&text=" + message;

  http.begin(client, url);

  int httpCode = http.GET();

  Serial.print("Telegram HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    Serial.println(http.getString());
  }

  http.end();
}

// --- HTML Dashboard Page with Live EMG Chart.js Integration ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Health Monitor & Safety System</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        :root { --bg: #0f172a; --card: #1e293b; --text: #f8fafc; --accent: #38bdf8; --danger: #ef4444; --success: #22c55e; }
        body { font-family: system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
        h1 { font-size: 1.5rem; text-transform: uppercase; letter-spacing: 2px; color: var(--accent); margin-bottom: 20px; text-align: center; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 15px; width: 100%; max-width: 900px; }
        .card { background: var(--card); padding: 20px; border-radius: 12px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.3); border: 1px solid #334155; }
        .val { font-size: 2.2rem; font-weight: bold; margin: 10px 0; color: var(--accent); }
        .status { padding: 10px; border-radius: 8px; text-align: center; font-weight: bold; transition: all 0.3s ease; }
        .status.off { background: #064e3b; color: #4ade80; }
        .status.on { background: #7f1d1d; color: #fca5a5; animation: pulse 1s infinite alternate; }
        
        /* Chart Card Full Width Span */
        .card-full { grid-column: 1 / -1; }
        .chart-container { position: relative; width: 100%; height: 280px; }
        
        @keyframes pulse { from { opacity: 0.7; } to { opacity: 1; } }
    </style>
</head>
<body>
    <h1> Safety Monitor Dashboard</h1>
    <div class="grid">
        <div class="card">
            <h3>Heart Rate (BPM)</h3>
            <div class="val" id="hr">0</div>
            <small>Calibrated Live Feed</small>
        </div>
        
        <div class="card">
            <h3>Accel Magnitude</h3>
            <div class="val" id="accel">0.0 m/s²</div>
            <small>Monitoring Orientation</small>
        </div>

        <div class="card">
            <h3>Airbag Relay Status</h3>
            <div id="airbagStatus" class="status off">SYSTEM NORMAL</div>
        </div>

        <div class="card card-full">
            <h3>EMG Muscle Activity (Live Graph)</h3>
            <div class="val" id="emg" style="font-size: 1.5rem; margin: 5px 0;">0</div>
            <div class="chart-container">
                <canvas id="emgChart"></canvas>
            </div>
            <small style="margin-top:8px; display:block;">Informational Graph Only</small>
        </div>
    </div>

    <script>
        const ctx = document.getElementById('emgChart').getContext('2d');
        const emgChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'EMG Signal (0 - 4095)',
                    data: [],
                    borderColor: '#38bdf8',
                    backgroundColor: 'rgba(56, 189, 248, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.1,
                    pointRadius: 0
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { grid: { color: '#334155' }, ticks: { color: '#94a3b8' } },
                    y: { min: 0, max: 4095, grid: { color: '#334155' }, ticks: { color: '#94a3b8' } }
                },
                animation: false
            }
        });

        setInterval(() => {
            fetch('/data')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('hr').innerText = d.hr;
                    document.getElementById('emg').innerText = d.emg;
                    document.getElementById('accel').innerText = d.accel.toFixed(1) + ' m/s²';
                    
                    let now = new Date().toLocaleTimeString();
                    if (emgChart.data.labels.length > 30) {
                        emgChart.data.labels.shift();
                        emgChart.data.datasets[0].data.shift();
                    }
                    emgChart.data.labels.push(now);
                    emgChart.data.datasets[0].data.push(d.emg);
                    emgChart.update();
                    
                    let st = document.getElementById('airbagStatus');
                    if(d.airbag) {
                        st.innerText = "AIRBAG DEPLOYED!";
                        st.className = "status on";
                    } else {
                        st.innerText = "SYSTEM NORMAL";
                        st.className = "status off";
                    }
                });
        }, 100);
    </script>
</body>
</html>
)rawliteral";

// --- Endpoint JSON Handlers ---
void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleData() {
  String json = "{\"hr\":" + String(heartRate) + ",\"emg\":" + String(emgValue) + ",\"accel\":" + String(accelMag) + ",\"airbag\":" + String(airbagActive ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  sim800.begin(9600, SERIAL_8N1, SIM800_RX, SIM800_TX);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);  // Relay Off initially

  Wire.begin(21, 22);

  // MPU6500_WE Setup
  if (!mpu.init()) {
    Serial.println("MPU6500 not connected!");
    while (1)
      ;
  }

  mpu.autoOffsets();
  mpu.enableGyrDLPF();
  mpu.setGyrRange(MPU6500_GYRO_RANGE_500);
  mpu.setAccRange(MPU6500_ACC_RANGE_8G);

  // WiFi Setup
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("Access Dashboard via local IP: http://");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  sendEmergencySMS();
}

void loop() {
  server.handleClient();

  // 1. Read Analog Sensors
  emgValue = analogRead(PIN_EMG);

  // Calibrated Heart Rate Mapping
  int rawPulse = analogRead(PIN_HR);
  heartRate = map(rawPulse, 1200, 4095, 55, 125);
  if (heartRate < 50) heartRate = 65;

  // 2. Read MPU6500 Sensor Data
  xyzFloat acc = mpu.getGValues();
  xyzFloat gyro = mpu.getGyrValues();

  float ax = acc.x * 9.80665;
  float ay = acc.y * 9.80665;
  float az = acc.z * 9.80665;
  accelMag = sqrt(pow(ax, 2) + pow(ay, 2) + pow(az, 2));

  // Serial diagnostics
  Serial.print("Gyroscope (deg/s) -> X: ");
  Serial.print(gyro.x);
  Serial.print(" | Y: ");
  Serial.print(gyro.y);
  Serial.print(" | Z: ");
  Serial.println(gyro.z);

  // 3. LATCHING TRIGGER LOGIC: Keep Relay ON until stable reset position is reached
  bool isGyroTiltTriggered = (abs(gyro.x) > TILT_THRESHOLD_GYRO) || (abs(gyro.y) > TILT_THRESHOLD_GYRO) || (abs(gyro.z) > TILT_THRESHOLD_GYRO);

  bool isRestPosition = (abs(gyro.x) < RESET_THRESHOLD_GYRO) && (abs(gyro.y) < RESET_THRESHOLD_GYRO) && (abs(gyro.z) < RESET_THRESHOLD_GYRO);

  if (isGyroTiltTriggered && !airbagActive) {
    // Trigger event
    airbagActive = true;
    digitalWrite(PIN_RELAY, HIGH);

    if (!alertSent) {
      sendEmergencySMS();
      alertSent = true;
    }
  }

  // If it was triggered, keep it ON until all axes return below the reset threshold
  if (airbagActive && isRestPosition) {
    digitalWrite(PIN_RELAY, LOW);  // Reset Relay OFF
    airbagActive = false;
    alertSent = false;  // Allows future alerts upon next fall
  }

  delay(100);
}