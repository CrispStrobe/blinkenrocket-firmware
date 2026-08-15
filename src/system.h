/*
 * Copyright (C) 2016 by Birte Kristina Friesel
 *
 * License: You may use, redistribute and/or modify this file under the terms
 * of either:
 * * The GNU LGPL v3 (see COPYING and COPYING.LESSER), or
 * * The 3-clause BSD License (see COPYING.BSD)
 *
 */

#include <stdlib.h>

// to display firmware version (v2.1) when storage is empty on first turn on
#define FW_REV_MAJOR  2
#define FW_REV_MINOR  2



#define SHUTDOWN_THRESHOLD 2048



/**
 * Contains the system idle loop. Checks for button presses, handles
 * standby/resume, reads data from the Modem and updates the Display.
 */
class System {
	private:
		/**
		 * Shutdown threshold counter. Contains the time since both
		 * buttons were first pressed at the same time in 256µs steps.
		 */
		uint16_t want_shutdown;

		/**
		 * Debounce counter for button presses. Buttons are ignored while
		 * this value is greater than zero
		 */
		uint8_t btn_debounce;


		/**
		 * Shuts down the entire system. Shows a shutdown animation, waits
		 * untel both buttons are released, turns off all hardware and puts
		 * the system to sleep. Re-enables the hardware and shows the
		 * last active animation after a wakeup
		 */
		void shutdown(void);

		/**
		 * Modem receive function. Maintains the internal state machine
		 * (see RxExpect)
		 */
		void receive(void);


		/**
		 * Show the pattern stored in pattern on the display.
		 * Updates the global active_anim object with the metadata
		 * stored in the first four bytes of pattern and cals
		 * Display::show() to display it
		 *
		 * @param pattern array containing the pattern
		 */
		void loadPattern_buf(uint8_t *pattern);

		/**
		 * Load pattern from PROGMEM. Loads the entire pattern stored
		 * at pattern_ptr into the global disp_buf variable, updates
		 * the global active_anim object with the metadata stored in the
		 * first four pattern bytes and calls Display::show() to display
		 * the pattern. The pattern data must not be longer than 128 bytes.
		 *
		 * @param pattern_ptr pointer to pattern data in PROGMEM
		 */
		void loadPattern_P(const uint8_t *pattern_ptr);

		enum TransmissionControl : uint8_t {
			BYTE_END = 0x84,
			// BYTE_START = 0x99,
			// BYTE_PATTERN = 0xa9,
			BYTE_START1 = 0xa5,
			BYTE_START2 = 0x5a,
			BYTE_PATTERN1 = 0x0f,
			BYTE_PATTERN2 = 0xf0,
		};

		enum ButtonMask : uint8_t {
			BUTTON_NONE = 0,
			BUTTON_LEFT = 1,
			BUTTON_RIGHT = 2,
			BUTTON_BOTH = 3
		};

		enum SystemMode : uint8_t {
			NORMAL,
			GAME_MODE
		};

		struct GameState {
			uint8_t player_pos;      // Player paddle position (0-5)
			uint8_t ai_pos;          // AI paddle position (0-5)
			int8_t ball_x;           // Ball X position (0-7)
			int8_t ball_y;           // Ball Y position (0-7)
			int8_t ball_vx;          // Ball X velocity (-1 or 1)
			int8_t ball_vy;          // Ball Y velocity (-1 or 1)
			uint8_t player_score;
			uint8_t ai_score;
			uint8_t update_counter;  // For game speed control
			uint8_t score_display_counter;
			bool showing_score;
			uint8_t frame[8]; // A dedicated buffer for the game's display frame
		};

		enum RxExpect : uint8_t {
			START1,
			START2,
			NEXT_BLOCK,
			PATTERN1,
			PATTERN2,
			HEADER1,
			HEADER2,
			META1,
			META2,
			DATA_FIRSTBLOCK,
			DATA,
		};

		RxExpect rxExpect;
		ButtonMask btnMask;

		// for pong game mode:

		SystemMode current_mode;
		GameState game;
		uint8_t game_debounce;

		void enterGameMode(void);
		void runGameMode(void);
		void exitGameMode(void);
		void updateGame(void);
		void updateAI(void);
		void resetBall(bool player_scored);
		void displayScore(void);

	public:
		System() { 
			want_shutdown = 0; 
			rxExpect = START1; 
			current_anim_no = 0; 
			btnMask = BUTTON_NONE; 
			btn_debounce = 0;
			current_mode = NORMAL;
			game_debounce = 0;
		};

		/**
		 * Initial MCU setup. Turns off unused peripherals to save power
		 * and configures the button pins. Also configures all other pins
		 * and peripherals using the enable function of their respective
		 * classes. Turns on interrupts once that's done.
		 */
		void initialize(void);

		/**
		 * System idle loop. Checks for button presses, handles
		 * standby/resume, reads data from the Modem and updates the Display.
		 *
		 * It is recommended to run this function before going back to sleep
		 * whenever the system is woken up by an interrupt.
		 */
		void loop(void);

		/**
		 * Resets the modem receive state machine and loads the
		 * "Transmission error" message. Called by the Watchdog Timeout
		 * ISR when a transmission was started (2x START received) but not
		 * properly finished (that is, four seconds passed since the last
		 * received byte and END byte was receveid).
		 */
		void handleTimeout(void);

		/**
		 * Index of the currently active animation
		 */
		uint8_t current_anim_no;

		/**
		 * Loads a pattern from the EEPROM and shows it on the display.
		 * Loads the first 132 bytes (4 bytes header + 128 bytes data) of
		 * the pattern into the global disp_buf variable, updates the
		 * global active_anim to reflect the read metadata and calls
		 * Display::show() to display the pattern.
		 *
		 * @param pattern_no index of pattern to show
		 */
		void loadPattern(uint8_t pattern_no);



};

extern System rocket;
