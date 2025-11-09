/*
  ESP8266 Client with State Machine, LED Status, and HC-SR04 Sensor
  
  It continuously polls the sensor and sends a "Triggered" message
  to the server if the distance is between 5cm and 50cm.
  It will not send a trigger message more than once every 0.5 seconds.

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

// --- Configuration: HC-SR04 Sensor Pins ---
#define TRIG_PIN 4  // GPIO 4 (D2 on NodeMCU) - Set as OUTPUT (Green)
#define ECHO_PIN 5  // GPIO 5 (D1 on NodeMCU) - Set as INPUT (Blue)

// --- Configuration: Auto-Discovery (Matching Python server) ---
const char* multicast_group = "224.1.1.1";
const int multicast_port = 5007;
const char* server_message = "ESP8266_SERVER_HERE";
const int MAX_MSG_LEN = 50;
// --- Configuration: Server ---
String server_ip = ""; // Will be populated by discovery
const int server_port = 5000; // Flask server port

// --- Configuration: LED Control ---
// Define the logic level for turning the built-in LED ON or OFF
// On many ESP8266 boards, the LED is "active-LOW"
#define LED_ON_STATE LOW
#define LED_OFF_STATE HIGH

// --- State Machine ---
#define STATE_CONNECTING_WIFI 0
#define STATE_FINDING_SERVER 1
#define STATE_RUNNING 2
int currentState = STATE_CONNECTING_WIFI;
// --- Non-Blocking Timers ---
unsigned long lastLedToggleTime = 0; 

// --- Trigger logic timers ---
unsigned long lastTriggerTime = 0; // Time of the last successful trigger
const unsigned long triggerInterval = 500; // 0.5 seconds (500ms) debounce time

// --- Global Objects ---
WiFiUDP udp; // UDP object for multicast listening
char incoming_packet[MAX_MSG_LEN]; // Buffer for incoming UDP packets
int currentLedState = LED_OFF_STATE; // Tracks the current state of the LED

// =================================================================
// SETUP: Runs once at boot.
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(100); 

  // Configure the built-in LED pin as an output
  pinMode(LED_BUILTIN, OUTPUT);
  // Start with the LED off
  digitalWrite(LED_BUILTIN, LED_OFF_STATE);
  currentLedState = LED_OFF_STATE;

  // --- Configure Sensor Pins ---
  pinMode(TRIG_PIN, OUTPUT); // Trigger pin sends the pulse
  pinMode(ECHO_PIN, INPUT);  // Echo pin reads the return
  
  Serial.println();
  Serial.println("Booting... Attempting to connect to Wi-Fi.");
  
  // Begin Wi-Fi connection
  WiFi.begin(ssid, password);

  // Initialize the timer for LED blinking
  lastLedToggleTime = millis();
}

// =================================================================
// LOOP: Runs continuously.
// =================================================================
void loop() {
  // This "switch" statement is the core of our state machine.
  // It calls a different function based on the value of 'currentState'.
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
  // This 'if' block is non-blocking. It checks if 100ms
  // have passed since the last LED toggle.
  if (millis() - lastLedToggleTime > 100) {
    lastLedToggleTime = millis(); // Reset the timer
    // Toggle the LED state
    currentLedState = (currentLedState == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE;
    digitalWrite(LED_BUILTIN, currentLedState);
  }

  // --- State Change Logic ---
  // Check if Wi-Fi has successfully connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("====================");
    Serial.println("  WiFi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.println("====================");

    // Prepare to find the server
    startServerDiscovery();
    
    // Change the state
    currentState = STATE_FINDING_SERVER;
    // Reset the LED timer for the next state
    lastLedToggleTime = millis(); 
    // Ensure LED is off before the slow blink starts
    digitalWrite(LED_BUILTIN, LED_OFF_STATE);
    currentLedState = LED_OFF_STATE;
  }
}

// =================================================================
// STATE 2: FINDING THE SERVER
// =================================================================
void handleFindingServer() {
  // --- LED Logic: Slow Blink (1000ms) ---
  // This 'if' block is non-blocking. It checks if 1000ms (1 second)
  // have passed since the last LED toggle.
  if (millis() - lastLedToggleTime > 1000) {
    lastLedToggleTime = millis(); // Reset the timer
    // Toggle the LED state
    currentLedState = (currentLedState == LED_ON_STATE) ? LED_OFF_STATE : LED_ON_STATE;
    digitalWrite(LED_BUILTIN, currentLedState);
  }

  // --- State Change Logic ---
  // Check if a UDP packet has been received
  int packet_size = udp.parsePacket();
  if (packet_size) {
    // Read the packet into the buffer
    int len = udp.read(incoming_packet, MAX_MSG_LEN - 1);
    if (len > 0) {
      incoming_packet[len] = '\0'; // Null-terminate the string
      
      // Compare the received message to the "secret message"
      if (strcmp(incoming_packet, server_message) == 0) {
        // --- Server Found! ---
        // Store the server's IP address
        server_ip = udp.remoteIP().toString(); 
        Serial.println("====================");
        Serial.println("  Server Found!");
        Serial.print("  Server IP set to: ");
        Serial.println(server_ip);
        Serial.println("====================");
        
        // Stop listening for UDP broadcasts
        udp.stop(); 
        
        // Change the state
        currentState = STATE_RUNNING;
        
        // Turn the LED on solid
        digitalWrite(LED_BUILTIN, LED_ON_STATE);
        currentLedState = LED_ON_STATE;
        
        // --- LOGIC FIX ---
        // Immediately send a "registration" packet to the server
        // so it appears on the web UI without needing a sensor trigger.
        // We send a distance of 0.0 to signify this.
        sendTriggerData(0.0);
        
        // Set lastTriggerTime to "now" to prevent
        // an immediate *second* trigger on the first sensor read
        lastTriggerTime = millis();
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
  
  
  // 1. Continuously poll the sensor
  float distance = getDistanceCm();
  
  // 2. Check for trigger condition (between 5cm and 50cm)
  if (distance >= 5.0 && distance <= 50.0) {
    
    // 3. Check for rate limiting (0.5 seconds)
    unsigned long now = millis();
    if (now - lastTriggerTime > triggerInterval) {
      lastTriggerTime = now; // Reset the timer
      
      // 4. Send the trigger message to the server
      Serial.print("--- TRIGGERED! Distance: ");
      Serial.print(distance);
      Serial.println(" cm ---");
      
      sendTriggerData(distance);
    }
    // else: A trigger happened, but we are inside the 500ms
    //       debounce window, so we do nothing.
  }
  // else: The distance is not in range, so we do nothing.
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
  
  // Begin listening for multicast UDP packets on the specified port
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
void sendTriggerData(float distance) {
  // Check if Wi-Fi is still connected.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    // Go back to the 'connecting' state
    currentState = STATE_CONNECTING_WIFI; 
    server_ip = ""; // Forget the server IP
    digitalWrite(LED_BUILTIN, LED_OFF_STATE);
    currentLedState = LED_OFF_STATE;
    return; // Exit the function
  }

  // --- We are here, Wi-Fi is good. Proceed with sending. ---
  WiFiClient client; // A client to handle the TCP connection
  HTTPClient http;   // The main HTTP client object

  // Build the server URL string (e.g., "http://192.168.1.100:5000/data")
  String serverUrl = "http://";
  serverUrl += server_ip;
  serverUrl += ":";
  serverUrl += server_port;
  serverUrl += "/data";

  // Begin the HTTP request
  if (http.begin(client, serverUrl)) {
    // Set the content type header to application/json
    http.addHeader("Content-Type", "application/json");
    
    // Create the JSON payload as a string
    String jsonPayload = String("{\"message\":\"Triggered\"") +
                         ", \"chipId\":" + String(ESP.getChipId()) +
                         ", \"distance\":" + String(distance, 2) + "}";
                         
    Serial.print("Sending JSON: ");
    Serial.println(jsonPayload);
    
    // Execute the POST request
    int httpResponseCode = http.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      // Success (e.g., 200 OK)
      String response = http.getString();
      Serial.print("HTTP Response Code: ");
      Serial.println(httpResponseCode);
      Serial.print("Server Response: ");
      Serial.println(response);
    } else {
      // Error (e.g., -1 for connection failed)
      Serial.printf("HTTP Error! Code: %d. Server lost.\n", httpResponseCode);
      // Server is gone, go back to finding it
      server_ip = ""; // Forget the bad IP
      startServerDiscovery(); // Re-start the UDP listener
      currentState = STATE_FINDING_SERVER; // Go back to "finding" state
      digitalWrite(LED_BUILTIN, LED_OFF_STATE); // Turn off the solid light
      currentLedState = LED_OFF_STATE;
    }
    // Clean up the HTTP client
    http.end();
  } else {
    // This happens if http.begin() fails (e.g., DNS/connection issue)
    Serial.println("HTTP connection failed! Server lost.");
    // --- Server is gone, go back to finding it ---
    server_ip = ""; // Forget the bad IP
    startServerDiscovery(); // Re-start the UDP listener
    currentState = STATE_FINDING_SERVER; // Go back to "finding" state
    digitalWrite(LED_BUILTIN, LED_OFF_STATE); // Turn off the solid light
    currentLedState = LED_OFF_STATE;
  }
}
