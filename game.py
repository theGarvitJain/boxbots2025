# game.py
# This file contains all the core game logic for the "Simon Says" project.
# It is designed to be imported by the main 'app.py' server and does not
# contain any Flask or SocketIO-specific code.

import random # Used to pick a new board for the sequence
import threading # Used to manage the 10-second player timer

class Game:
    """
    Manages the state and logic of a single "Simon Says" game instance.
    """

    def __init__(self, player_timeout_callback):
        # --- Game State ---
        # 'IDLE': Waiting for the 'Start' button
        # 'SHOWING': The server is showing the sequence to the player
        # 'PLAYER_TURN': The server is waiting for the player's input
        # 'GAME_OVER': The player made a mistake or timed out
        self.state = 'IDLE'

        # --- Board Configuration ---
        # This list will be populated by 'app.py' with the chipIds
        # of the ESP boards that have connected.
        self.available_boards = []

        # --- Sequence Management ---
        # This list stores the correct sequence of chipIds for the current game
        self.sequence = []
        # This tracks how many inputs the player has correctly entered so far
        self.player_input_index = 0

        # --- Game Timer ---
        # Stores the threading.Timer object for the player's 10-second turn
        self.player_timer = None
        # The callback function (provided by 'app.py') to run if the timer expires
        self.on_player_timeout = player_timeout_callback
        # The duration (in seconds) the player has for their turn
        self.turn_timeout_duration = 10.0 

    def set_available_boards(self, board_chip_ids):
        """
        Called by the server to update the list of boards available for the game.
        This allows the game to know which chipIds are valid to use.
        """
        self.available_boards = list(board_chip_ids)
        print(f"[Game] Available boards updated: {self.available_boards}")

    def start_new_game(self):
        """
        Resets all game variables to start a fresh game from level 1.
        """
        # Do not start a game if no boards are connected
        if not self.available_boards:
            print("[Game] Cannot start game, no boards are available.")
            return False

        print("[Game] Starting new game...")
        self.sequence = [] # Clear the sequence
        self.player_input_index = 0 # Reset player's position
        self.state = 'IDLE' # Set to idle before starting the first level
        self._cancel_player_timer() # Ensure any old timer is cancelled
        
        # Automatically move to the first level
        self.next_level()
        return True

    def next_level(self):
        """
        Adds one new random board to the end of the sequence and prepares
        the game state for the server to show the sequence.
        """
        # This should not be called if no boards are available,
        # but 'start_new_game' already checks this.
        if not self.available_boards:
            print("[Game] Cannot advance to next level, no boards available.")
            self.state = 'GAME_OVER'
            return

        # Pick a random chipId from the list of available boards
        random_board_id = random.choice(self.available_boards)
        self.sequence.append(random_board_id)
        
        # Reset the player's input position for the new (longer) sequence
        self.player_input_index = 0
        
        # Set the state so the server knows to play the sequence animation
        self.state = 'SHOWING'
        self._cancel_player_timer() # No timer while the sequence is showing

        print(f"[Game] Advancing to level {len(self.sequence)}.")
        print(f"[Game] New sequence: {self.sequence}")

    def start_player_turn(self):
        """
        Called by the server *after* it has finished showing the sequence.
        This sets the game state and starts the 10-second timer.
        """
        self.state = 'PLAYER_TURN'
        self.player_input_index = 0 # Ensure player starts from the beginning
        
        # Cancel any previous timer and start a new 10-second one
        self._cancel_player_timer()
        self.player_timer = threading.Timer(
            self.turn_timeout_duration, 
            self._handle_timeout
        )
        self.player_timer.start()
        print("[Game] Player turn started. 10-second timer running.")

    def check_player_input(self, chip_id):
        """
        Checks a single chipId input from the player against the sequence.
        Returns a status: 'INVALID', 'CORRECT', 'WRONG', 'LEVEL_COMPLETE'.
        """
        # Ignore any triggers if it's not the player's turn
        if self.state != 'PLAYER_TURN':
            print(f"[Game] Ignoring input {chip_id} (state is {self.state}).")
            return 'INVALID'
        
        # Check if the triggered board's ID is correct for the current index
        if chip_id == self.sequence[self.player_input_index]:
            # The input was correct
            self.player_input_index += 1
            
            # Check if this was the last input for the level
            if self.player_input_index == len(self.sequence):
                # Player completed the level
                print("[Game] Player input correct. LEVEL COMPLETE.")
                self._cancel_player_timer() # Stop the timer, they won
                self.state = 'IDLE' # Go to a neutral state before next level
                return 'LEVEL_COMPLETE'
            else:
                # Player was correct but sequence is not finished
                print(f"[Game] Player input {chip_id} correct. Waiting for next.")
                return 'CORRECT'
        else:
            # The input was wrong
            print(f"[Game] Player input {chip_id} WRONG. Expected {self.sequence[self.player_input_index]}.")
            self.state = 'GAME_OVER'
            self._cancel_player_timer() # Stop the timer, they lost
            return 'WRONG'

    def _handle_timeout(self):
        """
        Internal function called by the threading.Timer if time runs out.
        """
        # Ensure the game is still in PLAYER_TURN (e.g., they didn't
        # win on the very last second, which would cancel the timer).
        if self.state == 'PLAYER_TURN':
            print("[Game] Player TIMED OUT.")
            self.state = 'GAME_OVER'
            # Call the callback function in 'app.py' to notify the client
            self.on_player_timeout()

    def _cancel_player_timer(self):
        """
        Safely cancels the player timer if it exists.
        """
        if self.player_timer:
            self.player_timer.cancel()
            self.player_timer = None

    def get_current_level(self):
        """
        Helper function to get the current level number (1-based).
        """
        return len(self.sequence)