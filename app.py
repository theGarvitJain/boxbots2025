# app.py
# This is the main backend server for your application.

# --- Required Imports ---
import socket       # For networking, specifically UDP multicast
import threading    # To run the UDP broadcast in the background
import time         # To pause the broadcast thread

# --- Flask and Web-related Imports ---
from flask import Flask, request, jsonify, render_template
# Flask: The main web framework
# request: Handles incoming data (like the POST from the ESP)
# jsonify: Creates a properly formatted JSON response
# render_template: Finds and sends your 'index.html' file to the browser

from flask_socketio import SocketIO
# SocketIO: Enables real-time, two-way communication with the web browser

# --- Configuration for Auto-Discovery ---
# These must match the settings on your ESP8266
MULTICAST_GROUP = '224.1.1.1'
MULTICAST_PORT = 5007
SERVER_MESSAGE = b'ESP8266_SERVER_HERE' # The "secret message" the ESP listens for

# --- Application Setup ---
# Initialize the Flask app
app = Flask(__name__) 
# By default, Flask looks for 'templates' and 'static' folders in the same directory.

# Initialize SocketIO for real-time communication
# 'eventlet' is a high-performance server needed for SocketIO
socketio = SocketIO(app, async_mode='eventlet')

# --- UDP Multicast Thread (For Auto-Discovery) ---
def send_discovery_packets():
    """
    This function runs in a separate, continuous background thread.
    Its only job is to "shout" a message onto the network every 5 seconds
    so the ESP8266 can find this server's IP address.
    """
    # Create a standard UDP socket
    multicast_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    # Set the "Time To Live" (TTL) for the packet. 2 means it can cross routers.
    multicast_socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)

    print("Starting discovery broadcast thread...")
    while True:
        try:
            # Send the secret message to the multicast group and port
            multicast_socket.sendto(SERVER_MESSAGE, (MULTICAST_GROUP, MULTICAST_PORT))
            print("Sending discovery packet...")
        except Exception as e:
            print(f"Error sending discovery packet: {e}")
        
        # Wait 5 seconds before sending the next packet
        time.sleep(5)

# --- Flask Web Server Routes ---

@app.route('/data', methods=['POST'])
def receive_data():
    """
    This is the API endpoint that the ESP8266 sends its JSON data to.
    It's only ever contacted by the ESP, not by the browser.
    """
    if request.is_json:
        # Parse the JSON data sent from the ESP8266
        data = request.get_json()

        # Read the new chipId field from the JSON.
        # If 'chipId' doesn't exist for some reason, default to 'Unknown'.
        chip_id = data.get('chipId', 'Unknown')

        # Print the data to your Python server console
        print("=========================")
        print(f"[DATA RECEIVED from Board: {chip_id}]")
        print(f"  Message: {data.get('message')}")
        print(f"  Value:   {data.get('value')}")
        print("=========================")
        
        # --- This is the REAL-TIME part ---
        # Send a message (we'll call it 'new_message') to ALL connected
        # web browsers, passing along the data we just got.
        socketio.emit('new_message', data)
        # ---------------------------------
        
        # Send a "200 OK" success response back to the ESP8266
        return jsonify({"status": "success", "received_data": data}), 200
    else:
        # If the ESP sends something that isn't JSON, send an error
        print("Received non-JSON data")
        return jsonify({"status": "error", "message": "Request must be JSON"}), 400

@app.route('/')
def index():
    """
    This is the main webpage route.
    When a user opens 'http://localhost:5000' in their browser,
    this function runs and sends them the 'index.html' file.
    """
    # 'render_template' automatically looks inside your 'templates' folder
    # for the 'index.html' file.
    print("Browser connected, serving index.html")
    return render_template('index.html')

# --- Main execution ---
if __name__ == '__main__':
    # This block runs only when you execute 'python app.py' directly

    # 1. Start the UDP discovery thread.
    #    'daemon=True' means the thread will automatically close
    #    when the main application (Flask) stops.
    discovery_thread = threading.Thread(target=send_discovery_packets, daemon=True)
    discovery_thread.start()

    # 2. Start the main Flask-SocketIO web server.
    #    'host='0.0.0.0'' means it's accessible from any device on your network
    #    (which is what the ESP needs to find it).
    print("Starting Flask-SocketIO server on 0.0.0.0:5000...")
    # We use 'socketio.run' here instead of 'app.run' to ensure
    # both Flask and SocketIO work correctly together with 'eventlet'.
    socketio.run(app, host='0.0.0.0', port=5000)