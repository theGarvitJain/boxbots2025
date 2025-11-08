/*
  ESP8266 Client with State Machine, LED Status, and HC-SR04 Sensor
  
  This version adds an HC-SR04 ultrasonic sensor.
  It reads the distance and sends it to the server every 1 second
  *after* it is fully connected to Wi-Fi and the server.

  LED STATUS CODES:
  - Fast Flash (100ms): Connecting to Wi-Fi.
  - Slow Blink (1000ms): Wi-Fi connected, but searching for the server.
  - Solid ON: Wi-Fi and server connected, running normally.
*/

// --- Core Libraries ---
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiUdp.h> // For listening to multicast

// --- Configuration: Wi-Fi ---
const char* ssid = "iPhone";
const char* password = "12345678";

// --- Configuration: HC-SR04 Sensor Pins (Corrected) ---
#define TRIG_PIN 4  // GPIO 4 (D2 on NodeMCU) - Set as OUTPUT
#define ECHO_PIN 5  // GPIO 5 (D1 on NodeMCU) - Set as INPUT

// --- Configuration: Auto-Discovery (Must match Python server) ---
const char* multicast_group = "224.1.1.1";
const int multicast_port = 5007;
const char* server_message = "ESP8266_SERVER_HERE";
const int MAX_MSG_LEN = 50; 

// --- Configuration: Server ---
String server_ip = ""; // Will be populated by discovery
const int server_port = 5000; // Flask server port

// --- Configuration: LED Control ---
#define LED_ON_STATE LOW
#define LED_OFF_STATE HIGH

// --- State Machine ---
#define STATE_CONNECTING_WIFI 0
#define STATE_FINDING_SERVER 1
#define STATE_RUNNING 2
int currentState = STATE_CONNECTING_WIFI; 

// --- Non-Blocking Timers ---
unsigned long lastLedToggleTime = 0; 
unsigned long lastPostTime = 0;
// *** CHANGED: Send data every 1 second (1000ms) ***
unsigned long postInterval = 1000; 

int currentLedState = LED_OFF_STATE;

// --- Global Objects ---
WiFiUDP udp;
char incoming_packet[MAX_MSG_LEN];
int counter = 0;

// =================================================================
// SETUP: Runs once at boot.
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(100); 

  // Configure the built-in LED pin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_OFF_STATE);
  currentLedState = LED_OFF_STATE;

  // --- *** NEW: Configure Sensor Pins *** ---
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  Serial.println();
  Serial.println("Booting... Attempting to connect to Wi-Fi.");
  
  WiFi.begin(ssid, password);

  lastLedToggleTime = millis();
  lastPostTime = millis();
}

// =================================================================
// LOOP: Runs continuously.
// =================================================================
void loop() {
  // This "switch" statement is the core of our state machine.
  switch (currentState) {
    case STATE_CONNECTING_WIFI:
      handleConnectingWifi();
      break;
    case STATE_FINDING_SERVER:
      handleFindingServer();
      break;
    case STATE_RUNNING:
      handleRunning();
      break;
  }
}

// =================================================================
// STATE 1: CONNECTING TO WI-FI
// =================================================================
void handleConnectingWifi() {
  // --- LED Logic: Fast Flash (100ms) ---
  if (millis() - lastLedToggleTime > 100) {
    lastLedToggleTime = millis(); 
    currentLedState = (currentLedState == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE;
    digitalWrite(LED_BUILTIN, currentLedState);
  }

  // --- State Change Logic ---
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("====================");
    Serial.println("  WiFi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("====================");

    startServerDiscovery();
    
    currentState = STATE_FINDING_SERVER;
    lastLedToggleTime = millis(); 
    digitalWrite(LED_BUILTIN, LED_OFF_STATE);
    currentLedState = LED_OFF_STATE;
  }
}

// =================================================================
// STATE 2: FINDING THE SERVER
// =================================================================
void handleFindingServer() {
  // --- LED Logic: Slow Blink (1000ms) ---
  if (millis() - lastLedToggleTime > 1000) {
    lastLedToggleTime = millis(); 
    currentLedState = (currentLedState == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE;
    digitalWrite(LED_BUILTIN, currentLedState);
  }

  // --- State Change Logic ---
  int packet_size = udp.parsePacket();
  if (packet_size) {
    int len = udp.read(incoming_packet, MAX_MSG_LEN - 1);
    if (len > 0) {
      incoming_packet[len] = '\0'; 
      
      if (strcmp(incoming_packet, server_message) == 0) {
        server_ip = udp.remoteIP().toString(); 
        Serial.println("====================");
        Serial.println("  Server Found!");
        Serial.print("  Server IP set to: ");
        Serial.println(server_ip);
        Serial.println("====================");
        
        udp.stop(); 
        
        currentState = STATE_RUNNING;
        
        digitalWrite(LED_BUILTIN, LED_ON_STATE);
        currentLedState = LED_ON_STATE;
        
        lastPostTime = millis(); 
      }
    }
  }
}

// =================================================================
// STATE 3: RUNNING (Main Application)
// =================================================================
void handleRunning() {
  // Keep LED solid ON
  if (currentLedState != LED_ON_STATE) {
    digitalWrite(LED_BUILTIN, LED_ON_STATE);
    currentLedState = LED_ON_STATE;
  }
  
  // --- Non-Blocking Send Timer ---
  // *** CHANGED: Check if 1 second (postInterval) has passed ***
  if (millis() - lastPostTime > postInterval) {
    lastPostTime = millis(); // Reset the 1-second timer
    sendDataToServer();      // Call the function to read sensor and send data
  }
}

// =================================================================
// HELPER FUNCTION: Get Distance
// =================================================================
float getDistanceCm() {
  // --- This is the standard HC-SR04 pulse sequence ---

  // 1. Clear the TRIG pin (ensure it's low)
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  // 2. Send a 10-microsecond pulse to trigger the sensor
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 3. Read the ECHO pin. pulseIn() waits for the pin to go HIGH,
  //    measures the duration (in microseconds) it stays HIGH,
  //    and then waits for it to go LOW.
  //    A 30000 µs timeout (~5 meters) prevents it from
  //    getting stuck forever if no echo is received.
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  // 4. Calculate the distance
  //    Speed of sound is approx. 343 m/s, or 0.0343 cm/µs.
  //    The pulse travels there AND back, so we divide by 2.
  //    Distance = (Duration * Speed of Sound) / 2
  float distance = (duration * 0.0343) / 2.0;

  // Handle out-of-range readings
  if (distance <= 0 || distance > 400) {
    return 0.0; // Return 0 for invalid readings
  }

  return distance;
}


// =================================================================
// HELPER FUNCTION: Start Server Discovery
// =================================================================
void startServerDiscovery() {
  Serial.println("Starting server discovery...");
  IPAddress multicast_ip;
  multicast_ip.fromString(multicast_group);
  
  if (udp.beginMulticast(WiFi.localIP(), multicast_ip, multicast_port)) {
    Serial.print("Waiting for server broadcast on ");
    Serial.print(multicast_group);
    Serial.print(":");
    Serial.println(multicast_port);
  } else {
    Serial.println("Failed to start multicast listener!");
  }
}

// =================================================================
// HELPER FUNCTION: Send Data to Server
// =================================================================
void sendDataToServer() {
  // Check if Wi-Fi is still connected.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    currentState = STATE_CONNECTING_WIFI; 
    server_ip = ""; 
    digitalWrite(LED_BUILTIN, LED_OFF_STATE);
    currentLedState = LED_OFF_STATE;
    return; 
  }

  // --- *** NEW: Get distance reading *** ---
  float distance = getDistanceCm();
  Serial.print("Distance measured: ");
  Serial.print(distance);
  Serial.println(" cm");

  // --- If we are here, Wi-Fi is good. Proceed with sending. ---
  WiFiClient client;
  HTTPClient http;

  String serverUrl = "http://";
  serverUrl += server_ip;
  serverUrl += ":";
  serverUrl += server_port;
  serverUrl += "/data";

  if (http.begin(client, serverUrl)) {
    http.addHeader("Content-Type", "application/json");

    // --- *** CHANGED: Add "distance" to the JSON payload *** ---
    String jsonPayload = "{\"message\":\"Hello from ESP8266\", \"value\":" + String(counter) + 
                         ", \"chipId\":" + String(ESP.getChipId()) + 
                         ", \"distance\":" + String(distance, 2) + "}"; // String(distance, 2) = 2 decimal places
    
    Serial.print("Sending JSON: ");
    Serial.println(jsonPayload);

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("HTTP Response Code: ");
      Serial.println(httpResponseCode);
      Serial.print("Server Response: ");
      Serial.println(response);
    } else {
      Serial.printf("HTTP Error! Code: %d\n", httpResponseCode);
    }
    http.end(); 
  } else {
    Serial.println("HTTP connection failed!");
  }
  
  counter++; 
}