/*
  ESP8266 Client with State Machine and LED Status
  
  This version has been significantly refactored to be non-blocking
  and to provide clear visual feedback for its connection status
  using the built-in LED.

  LED STATUS CODES:
  - Fast Flash (100ms): Connecting to Wi-Fi.
  - Slow Blink (1000ms): Wi-Fi connected, but searching for the server.
  - Solid ON: Wi-Fi and server connected, running normally.
*/

// --- Core Libraries ---
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>  // For listening to multicast

// --- Configuration: Wi-Fi ---
const char* ssid = "iPhone";
const char* password = "12345678";

// --- Configuration: Auto-Discovery (Must match Python server) ---
const char* multicast_group = "224.1.1.1";
const int multicast_port = 5007;
const char* server_message = "ESP8266_SERVER_HERE";
const int MAX_MSG_LEN = 50;  // Buffer for the message

// --- Configuration: Server ---
String server_ip = "";         // Will be populated by discovery
const int server_port = 5000;  // Flask server port

// --- Configuration: LED Control ---
// We use LED_BUILTIN (usually GPIO 2 on NodeMCU/Wemos).
// This LED is often "active LOW," meaning LOW turns it ON and HIGH turns it OFF.
#define LED_ON_STATE LOW
#define LED_OFF_STATE HIGH

// --- State Machine ---
// We define "states" to track what the ESP is doing.
#define STATE_CONNECTING_WIFI 0
#define STATE_FINDING_SERVER 1
#define STATE_RUNNING 2
int currentState = STATE_CONNECTING_WIFI;  // Start in the first state

// --- Non-Blocking Timers ---
// We use millis() to track time without stopping the code.
unsigned long lastLedToggleTime = 0;  // For LED blinking
unsigned long lastPostTime = 0;       // For the 10-second data send
int currentLedState = LED_OFF_STATE;  // Tracks if LED is on or off

// --- Global Objects ---
WiFiUDP udp;
char incoming_packet[MAX_MSG_LEN];
int counter = 0;

// =================================================================
// SETUP: Runs once at boot.
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(100);  // Wait for Serial to initialize

  // Configure the built-in LED pin as an output
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_OFF_STATE);  // Start with LED off
  currentLedState = LED_OFF_STATE;

  Serial.println();
  Serial.println("Booting... Attempting to connect to Wi-Fi.");

  // Start the Wi-Fi connection. We will NOT wait here.
  // The loop() will handle checking the connection status.
  WiFi.begin(ssid, password);

  // Initialize our timers
  lastLedToggleTime = millis();
  lastPostTime = millis();
}

// =================================================================
// LOOP: Runs continuously and as fast as possible.
// =================================================================
void loop() {
  // This "switch" statement is the core of our state machine.
  // It runs the code for the *current* state.
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
  // This function is called over and over by loop()
  // while we are in STATE_CONNECTING_WIFI.

  // --- LED Logic: Fast Flash (100ms) ---
  if (millis() - lastLedToggleTime > 100) {
    lastLedToggleTime = millis();  // Reset the timer
    // Flip the LED state
    currentLedState = (currentLedState == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE;
    digitalWrite(LED_BUILTIN, currentLedState);
  }

  // --- State Change Logic ---
  // Check if Wi-Fi has finally connected
  if (WiFi.status() == WL_CONNECTED) {
    // --- Connection Successful ---
    Serial.println();
    Serial.println("====================");
    Serial.println("  WiFi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("====================");

    // Now, start the server discovery
    startServerDiscovery();

    // And move to the next state
    currentState = STATE_FINDING_SERVER;
    lastLedToggleTime = millis();              // Reset timer for the new state's blink rate
    digitalWrite(LED_BUILTIN, LED_OFF_STATE);  // Start with LED off
    currentLedState = LED_OFF_STATE;
  }
}

// =================================================================
// STATE 2: FINDING THE SERVER
// =================================================================
void handleFindingServer() {
  // This function is called by loop() while we are in
  // STATE_FINDING_SERVER.

  // --- LED Logic: Slow Blink (1000ms) ---
  if (millis() - lastLedToggleTime > 1000) {
    lastLedToggleTime = millis();  // Reset the timer
    // Flip the LED state
    currentLedState = (currentLedState == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE;
    digitalWrite(LED_BUILTIN, currentLedState);
  }

  // --- State Change Logic ---
  // Check if a UDP packet has arrived
  int packet_size = udp.parsePacket();
  if (packet_size) {
    // Read the packet
    int len = udp.read(incoming_packet, MAX_MSG_LEN - 1);
    if (len > 0) {
      incoming_packet[len] = '\0';  // Null-terminate the string

      Serial.print("Received packet: '");
      Serial.print(incoming_packet);
      Serial.print("' from ");
      Serial.println(udp.remoteIP());

      // Check if this is the server message we expect
      if (strcmp(incoming_packet, server_message) == 0) {
        // --- Server Found! ---
        server_ip = udp.remoteIP().toString();  // Save the server's IP
        Serial.println("====================");
        Serial.println("  Server Found!");
        Serial.print("  Server IP set to: ");
        Serial.println(server_ip);
        Serial.println("====================");

        udp.stop();  // Stop listening

        // Move to the final "running" state
        currentState = STATE_RUNNING;

        // --- LED Logic: Solid ON ---
        digitalWrite(LED_BUILTIN, LED_ON_STATE);
        currentLedState = LED_ON_STATE;

        lastPostTime = millis();  // Reset post timer so it sends first message
      }
    }
  }
}

// =================================================================
// STATE 3: RUNNING (Main Application)
// =================================================================
void handleRunning() {
  // This is the main state. The LED should be solid ON.
  // We just ensure it's on, in case something else toggled it.
  if (currentLedState != LED_ON_STATE) {
    digitalWrite(LED_BUILTIN, LED_ON_STATE);
    currentLedState = LED_ON_STATE;
  }

  // --- Non-Blocking Send Timer ---
  // Check if 10 seconds have passed since the last send
  if (millis() - lastPostTime > 10000) {
    lastPostTime = millis();  // Reset the 10-second timer
    sendDataToServer();       // Call the function to send data
  }
}

// =================================================================
// HELPER FUNCTION: Start Server Discovery
// =================================================================
void startServerDiscovery() {
  Serial.println("Starting server discovery...");

  // We must first convert the char* string "224.1.1.1"
  // into an IPAddress object.
  IPAddress multicast_ip;
  multicast_ip.fromString(multicast_group);

  // Begin listening to the multicast group
  if (udp.beginMulticast(WiFi.localIP(), multicast_ip, multicast_port)) {
    Serial.print("Waiting for server broadcast on ");
    Serial.print(multicast_group);
    Serial.print(":");
    Serial.println(multicast_port);
  } else {
    Serial.println("Failed to start multicast listener!");
    // You could add logic here to reboot or retry
  }
}

// =================================================================
// HELPER FUNCTION: Send Data to Server
// =================================================================
void sendDataToServer() {
  // Check if Wi-Fi is still connected.
  // If not, reset the whole state machine.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    currentState = STATE_CONNECTING_WIFI;      // Go back to state 1
    server_ip = "";                            // We lost the server, so we must find it again
    digitalWrite(LED_BUILTIN, LED_OFF_STATE);  // Turn off LED
    currentLedState = LED_OFF_STATE;
    return;  // Exit this function
  }

  // --- If we are here, Wi-Fi is good. Proceed with sending. ---
  WiFiClient client;
  HTTPClient http;

  // Construct the server URL (using the auto-discovered IP)
  String serverUrl = "http://";
  serverUrl += server_ip;
  serverUrl += ":";
  serverUrl += server_port;
  serverUrl += "/data";  // The endpoint

  Serial.print("Connecting to server: ");
  Serial.println(serverUrl);

  // Start the HTTP request
  if (http.begin(client, serverUrl)) {

    http.addHeader("Content-Type", "application/json");

    // We now add a "chipId" field to the JSON payload.
    // ESP.getChipId() returns a unique 32-bit integer for this board.
    String jsonPayload = "{\"message\":\"Hello from ESP8266\", \"value\":" + String(counter) + ", \"chipId\":" + String(ESP.getChipId()) + "}";

    Serial.print("Sending JSON: ");
    Serial.println(jsonPayload);

    // Send the POST request
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

    http.end();  // Free resources
  } else {
    Serial.println("HTTP connection failed!");
  }

  counter++;  // Increment the message counter
}