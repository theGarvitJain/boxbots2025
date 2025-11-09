# app.py
# This is the main backend server for your application.

# --- Required Imports ---
import socket       # For networking, specifically UDP multicast
import threading    # To run the UDP broadcast and game timers
import time         # To pause threads (e.g., for game sequence timing)

# --- Flask and Web-related Imports ---
from flask import Flask, request, jsonify, render_template
# Flask: The main web framework
# request: Handles incoming data (like the POST from the ESP)
# jsonify: Creates a properly formatted JSON response
# render_template: Finds and sends your 'index.html' file to the browser

from flask_socketio import SocketIO, emit
# SocketIO: Enables real-time, two-way communication with the web browser
# emit: Used to send SocketIO messages

# --- Game Logic Import ---
from game import Game  # Import the Game class from our new 'game.py' file

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

# --- Game & Board Management ---

# This dictionary maps your specific, hard-coded chip IDs to their colors.
BOARD_COLOR_MAP = {
    "9072791": "green",
    "9132300": "red",
    "9073806": "yellow",
    "9132077": "blue"
}

# This dictionary will store the *active* boards that have connected.
# Example: { "123456": "red", "789012": "green" }
connected_boards = {}

# This is the global instance of our game logic.
# It is initialized in the 'main' block at the bottom.
game_instance = None

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
    
    This function now handles:
    1. Registering new boards that connect *if* they are in the map.
    2. Emitting the 'new_message' for the UI to flash the square.
    3. Passing the input to the game logic if it's the player's turn.
    """
    if request.is_json:
        # Parse the JSON data sent from the ESP8266
        data = request.get_json()

        # Read the new chipId field from the JSON.
        chip_id = str(data.get('chipId', 'Unknown')) # Ensure chipId is a string

        # --- 1. Board Registration ---
        # Check if the chipId is one of our known boards
        if chip_id in BOARD_COLOR_MAP:
            # Check if it's not already in our 'active' list
            if chip_id not in connected_boards:
                # This is a known board connecting for the first time
                new_color = BOARD_COLOR_MAP[chip_id]
                connected_boards[chip_id] = new_color
                
                print("=========================")
                print(f"  KNOWN BOARD CONNECTED: {chip_id}")
                print(f"  Assigned color: {new_color}")
                print("=========================")
                
                # Update the game logic with the new list of active board IDs
                game_instance.set_available_boards(connected_boards.keys())
                
                # Send the complete, updated list to ALL web browsers
                socketio.emit('update_boards', connected_boards)
        elif chip_id != 'Unknown':
            # This is an unknown board, we ignore it
            print(f"Ignoring data from unknown board: {chip_id}")
            # We don't proceed to flash or use game logic for unknown boards
            return jsonify({"status": "success", "message": "Ignored unknown board"}), 200

        # If the chipId is unknown, the rest of the function is skipped.
        # Only known boards will trigger flashes and game logic.
        if chip_id not in BOARD_COLOR_MAP:
             return jsonify({"status": "success", "message": "Ignored unknown board"}), 200

        # Print the data to your Python server console (optional)
        print(f"[Data from {chip_id}] Dist: {data.get('distance')} cm")
        
        # --- 2. This is the REAL-TIME flash part ---
        # Send a message (we'll call it 'new_message') to ALL connected
        # web browsers. The front-end uses this to trigger the green flash
        # animation *every* time a board is hit.
        socketio.emit('new_message', data)
        
        # --- 3. Game Logic Integration ---
        # Check if the game is currently waiting for player input
        if game_instance.state == 'PLAYER_TURN':
            # Pass the chipId to the game logic and get the result
            result = game_instance.check_player_input(chip_id)
            
            # Act based on the result from the game logic
            if result == 'WRONG':
                # Player made a mistake
                socketio.emit('game_update', {
                    'status': 'GAME_OVER',
                    'level': game_instance.get_current_level(),
                    'reason': 'wrong_input'
                })
            
            elif result == 'LEVEL_COMPLETE':
                # Player finished the sequence correctly
                # We start a background task to avoid blocking the server
                # This task will pause, then start the next level
                socketio.start_background_task(target=_handle_next_level)
                
            elif result == 'CORRECT':
                # Player input was correct, but the sequence isn't finished
                # We can send a small update, e.g., to play a sound
                socketio.emit('game_update', {'status': 'CORRECT_INPUT'})
        
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

# --- SocketIO Event Handlers (Browser <-> Server) ---

@socketio.on('connect')
def handle_connect():
    """
    This runs when a new web browser client connects to the server.
    We immediately send them the current list of connected boards.
    """
    print(f"New web client connected. Sending current board list.")
    # 'emit' (without 'socketio.' prefix) sends only to the client
    # that just connected.
    emit('update_boards', connected_boards)

@socketio.on('start_game')
def handle_start_game():
    """
    This runs when the user clicks the "Start Game" button on the webpage.
    """
    if game_instance.state == 'PLAYER_TURN':
        print("[Game] Ignoring 'start_game' request, game already in progress.")
        return

    print("[Game] 'start_game' event received. Starting new game...")
    # Reset the game logic to level 1
    game_instance.start_new_game()
    
    # Start a background task to show the sequence, so the server
    # doesn't get blocked by the 'time.sleep' calls.
    socketio.start_background_task(target=show_sequence_to_client)

# --- Game Helper Functions (Run by Server) ---

def show_sequence_to_client():
    """
    This function "plays" the game's sequence for the user.
    It runs in a background thread to avoid blocking the server.
    """
    # Give a brief pause before starting the sequence
    print("[Game] Showing sequence...")
    socketio.emit('game_update', {
        'status': 'SHOWING', 
        'level': game_instance.get_current_level()
    })
    time.sleep(1.5) # Wait for 1.5s so user can read the "Level X" message
    
    # Get the sequence of chipIds from the game instance
    sequence_to_show = game_instance.sequence
    
    for chip_id in sequence_to_show:
        # Check if the board is still connected (it might have disconnected)
        if chip_id in connected_boards:
            # Tell the browser to flash this specific board
            socketio.emit('show_flash', {'chipId': chip_id})
            
            # Wait 1 second before showing the next flash
            # (0.7s for the flash, 0.3s pause)
            time.sleep(1.0) 
            
    # The sequence is finished. Tell the game logic to start the player's turn.
    game_instance.start_player_turn()
    
    # Tell the browser the player's turn has begun
    socketio.emit('game_update', {'status': 'PLAYER_TURN'})
    print("[Game] Player turn has started.")

def _handle_next_level():
    """
    A helper function to manage the transition between levels.
    Runs in a background thread.
    """
    # Tell the client the level is complete
    print("[Game] Level complete. Waiting 2.5s for next level.")
    socketio.emit('game_update', {
        'status': 'LEVEL_COMPLETE',
        'level': game_instance.get_current_level()
    })
    time.sleep(2.5) # Pause so the player can celebrate
    
    # Tell the game instance to advance to the next level
    game_instance.next_level()
    
    # Start *another* background task to show the new, longer sequence
    socketio.start_background_task(target=show_sequence_to_client)

def on_player_timeout_callback():
    """
This function is passed to the Game instance when it's created.
    The Game instance's internal timer will call this from a separate
    thread if the player runs out of time.
    """
    print("[Game] Player timed out (callback triggered).")
    
    # We must emit from within the socketio context
    with app.app_context():
        # Tell all browsers that the game is over due to a timeout
        socketio.emit('game_update', {
            'status': 'GAME_OVER',
            'level': game_instance.get_current_level(),
            'reason': 'timeout'
        })

# --- Main execution ---
if __name__ == '__main__':
    # This block runs only when you execute 'python app.py' directly

    # 1. Initialize the Game instance, passing it the timeout function
    game_instance = Game(player_timeout_callback=on_player_timeout_callback)

    # 2. Start the UDP discovery thread.
    #    'daemon=True' means the thread will automatically close
    #    when the main application (Flask) stops.
    discovery_thread = threading.Thread(target=send_discovery_packets, daemon=True)
    discovery_thread.start()

    # 3. Start the main Flask-SocketIO web server.
    #    'host='0.0.0.0'' means it's accessible from any device on your network
    #    (which is what the ESP needs to find it).
    print("Starting Flask-SocketIO server on 0.0.0.0:5000...")
    # We use 'socketio.run' here instead of 'app.run' to ensure
    # both Flask and SocketIO work correctly together with 'eventlet'.
    socketio.run(app, host='0.0.0.0', port=5000)