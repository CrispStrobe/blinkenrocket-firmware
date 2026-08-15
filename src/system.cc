/* Name: system.cc
 * Version: 2.2 (with robust minigame)
 * Copyright (C) 2016 by Birte Kristina Friesel
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>

#include "display.h"
#include "fecmodem.h"
#include "storage.h"
#include "system.h"
#include "static_patterns.h"

System rocket;
animation_t active_anim;

uint8_t disp_buf[132]; // Buffer for animations and modem data
uint8_t *rx_buf = disp_buf + sizeof(disp_buf) - 33;

void System::initialize()
{
	wdt_disable();
	PORTC |= _BV(PC3) | _BV(PC7);
	display.enable();
	modem.enable();
	storage.enable();
	sei();
	current_anim_no = 0;
	loadPattern_P(turnonPattern);
}

void System::loadPattern_P(const uint8_t *pattern_ptr)
{
	for (uint8_t i = 0; i < 4; i++) disp_buf[i] = pgm_read_byte(pattern_ptr + i);
	for (uint8_t i = 0; i < disp_buf[1]; i++) disp_buf[i+4] = pgm_read_byte(pattern_ptr + i + 4);
	loadPattern_buf(disp_buf);
}

void System::loadPattern_buf(uint8_t *pattern)
{
	active_anim.type = (AnimationType)(pattern[0] >> 4);
	active_anim.length = ((pattern[0] & 0x0f) << 8) + pattern[1];
	if (active_anim.type == AnimationType::TEXT) {
		active_anim.speed = 250 - (pattern[2] & 0xf0);
		active_anim.delay = (pattern[2] & 0x0f);
		active_anim.direction = pattern[3] >> 4;
		active_anim.repeat = (pattern[3] & 0x0f);
	} else {
		active_anim.speed = 250 - ((pattern[2] & 0x0f) << 4);
		active_anim.delay = pattern[3] >> 4;
		active_anim.direction = 0;
		active_anim.repeat = (pattern[3] & 0x0f);
	}
	active_anim.data = pattern + 4;
	display.show(&active_anim);
}

void System::loadPattern(uint8_t anim_no)
{
	if (storage.hasData()) {
		storage.load(anim_no, disp_buf);
		loadPattern_buf(disp_buf);
	} else {
		loadPattern_P(emptyPattern);
	}
}

void System::receive(void)
{
	static uint8_t rx_pos = 0;
	static uint16_t remaining_bytes = 0;
	uint8_t rx_byte = modem.buffer_get();
	if (rxExpect > PATTERN2) {
		rx_buf[rx_pos++] = rx_byte;
		if (rxExpect > META2) remaining_bytes--;
	}
	switch(rxExpect) {
		case START1: if (rx_byte == BYTE_START1) rxExpect = START2; break;
		case START2:
			if (rx_byte == BYTE_START2) {
				rxExpect = NEXT_BLOCK; storage.reset(); loadPattern_P(flashingPattern);
				MCUSR &= ~_BV(WDRF); cli();
				WDTCSR = _BV(WDCE) | _BV(WDE); WDTCSR = _BV(WDIE) | _BV(WDP3); sei();
			} else if (rx_byte != BYTE_START1) rxExpect = START1;
			break;
		case NEXT_BLOCK:
			if (rx_byte == BYTE_PATTERN1) rxExpect = PATTERN2;
			else if (rx_byte == BYTE_END) {
				storage.sync(); current_anim_no = 0; loadPattern(0);
				rxExpect = START1; wdt_disable(); modem.buffer_clear();
			} else rxExpect = START1;
			break;
		case PATTERN2: if (rx_byte == BYTE_PATTERN2) { rxExpect = HEADER1; rx_pos = 0; } else rxExpect = START1; break;
		case HEADER1: rxExpect = HEADER2; remaining_bytes = (rx_byte & 0x0f) << 8; break;
		case HEADER2: rxExpect = META1; remaining_bytes += rx_byte; wdt_reset(); break;
		case META1: rxExpect = META2; break;
		case META2: rxExpect = DATA_FIRSTBLOCK; if (remaining_bytes == 0) rxExpect = NEXT_BLOCK; break;
		case DATA_FIRSTBLOCK:
			if (remaining_bytes == 0) { rxExpect = NEXT_BLOCK; storage.save(rx_buf); }
			else if (rx_pos == 32) { rxExpect = DATA; rx_pos = 0; storage.save(rx_buf); }
			break;
		case DATA:
			if (remaining_bytes == 0) { rxExpect = NEXT_BLOCK; storage.append(rx_buf); }
			else if (rx_pos == 32) { rx_pos = 0; storage.append(rx_buf); wdt_reset(); }
			break;
		default: rxExpect = START1; break;
	}
}

// =================================================================
// MINIGAME LOGIC
// =================================================================

void System::enterGameMode()
{
	current_mode = GAME_MODE;
	game_debounce = 0;
	game.player_score = 0;
	game.ai_score = 0;
	game.showing_score = true;
	game.score_display_counter = 100;
	resetBall(true);

	// Wait for the user to release the buttons before starting the game.
	// This prevents the game from exiting immediately.
	while ((PINC & (_BV(PC3) | _BV(PC7))) == 0);
	_delay_ms(50); // Add a small debounce for clean input
}

void System::exitGameMode()
{
	current_mode = NORMAL;
	loadPattern(current_anim_no);
	while (!((PINC & _BV(PC3)) && (PINC & _BV(PC7))));
	_delay_ms(100);
}

void System::resetBall(bool player_scored)
{
	game.ball_x = 3; game.ball_y = 3;
	game.ball_vx = (rand() % 2) * 2 - 1;
	game.ball_vy = player_scored ? 1 : -1;
	game.player_pos = 2; game.ai_pos = 2;
}

void System::displayScore()
{
	for (uint8_t i = 0; i < 8; i++) game.frame[i] = 0xFF; // All LEDs OFF
	for (uint8_t i = 0; i < game.player_score && i < 8; i++) { game.frame[0] &= ~_BV(i); game.frame[1] &= ~_BV(i); }
	for (uint8_t i = 0; i < game.ai_score && i < 8; i++) { game.frame[6] &= ~_BV(i); game.frame[7] &= ~_BV(i); }
}

void System::updateAI()
{
	if (game.ball_y > 3) {
		if (game.ball_x < game.ai_pos + 1 && game.ai_pos > 0) game.ai_pos--;
		else if (game.ball_x > game.ai_pos + 1 && game.ai_pos < 5) game.ai_pos++;
	}
}

void System::updateGame()
{
	if (game.showing_score) {
		displayScore();
		if (--game.score_display_counter == 0) {
			game.showing_score = false;
			if (game.player_score >= 5 || game.ai_score >= 5) exitGameMode();
		}
		return;
	}

	if (++game.update_counter < 12) return;
	game.update_counter = 0;

	updateAI();
	game.ball_x += game.ball_vx; game.ball_y += game.ball_vy;

	if (game.ball_x <= 0 || game.ball_x >= 7) { game.ball_vx = -game.ball_vx; game.ball_x += game.ball_vx; }

	if (game.ball_y <= 0) {
		if (game.ball_x >= game.player_pos && game.ball_x < game.player_pos + 3) {
			game.ball_vy = 1; game.ball_y = 1;
			if (game.ball_x == game.player_pos) game.ball_vx = -1; else if (game.ball_x == game.player_pos + 2) game.ball_vx = 1;
		} else { game.ai_score++; game.showing_score = true; game.score_display_counter = 150; resetBall(false); return; }
	}

	if (game.ball_y >= 7) {
		if (game.ball_x >= game.ai_pos && game.ball_x < game.ai_pos + 3) {
			game.ball_vy = -1; game.ball_y = 6;
			if (game.ball_x == game.ai_pos) game.ball_vx = -1; else if (game.ball_x == game.ai_pos + 2) game.ball_vx = 1;
		} else { game.player_score++; game.showing_score = true; game.score_display_counter = 150; resetBall(true); return; }
	}

	for (uint8_t i = 0; i < 8; i++) game.frame[i] = 0xFF;
	for (uint8_t i = 0; i < 3; i++) {
		game.frame[game.player_pos + i] &= ~_BV(0);
		game.frame[game.ai_pos + i] &= ~_BV(7);
	}
	game.frame[game.ball_x] &= ~_BV(game.ball_y);
}

void System::runGameMode()
{
	if ((PINC & (_BV(PC3) | _BV(PC7))) == 0) {
		if (++want_shutdown >= (SHUTDOWN_THRESHOLD / 2)) { exitGameMode(); want_shutdown = 0; return; }
	} else { want_shutdown = 0; }

	if (game_debounce > 0) game_debounce--;
	if (game_debounce == 0 && !game.showing_score) {
		if ((PINC & _BV(PC7)) == 0 && game.player_pos > 0) { game.player_pos--; game_debounce = 5; }
		if ((PINC & _BV(PC3)) == 0 && game.player_pos < 5) { game.player_pos++; game_debounce = 5; }
	}

	updateGame();
	display.drawFrame(game.frame);
}

// =================================================================
// MAIN SYSTEM LOOP & SHUTDOWN
// =================================================================

void System::loop()
{
	while (modem.buffer_available()) receive();

	if (current_mode == GAME_MODE) {
		runGameMode();
		return;
	}

	if ((PINC & (_BV(PC3) | _BV(PC7))) == 0) {
		if (want_shutdown < SHUTDOWN_THRESHOLD) want_shutdown++;
		else { enterGameMode(); want_shutdown = 0; }
	} else { want_shutdown = 0; }

	if (btn_debounce == 0) {
		if ((PINC & _BV(PC3)) == 0) btnMask = (ButtonMask)(btnMask | BUTTON_RIGHT);
		if ((PINC & _BV(PC7)) == 0) btnMask = (ButtonMask)(btnMask | BUTTON_LEFT);
		if ((PINC & (_BV(PC3) | _BV(PC7))) == (_BV(PC3) | _BV(PC7))) {
			cli();
			if (btnMask == BUTTON_RIGHT) {
				current_anim_no = (current_anim_no + 1) % storage.numPatterns();
				loadPattern(current_anim_no);
			} else if (btnMask == BUTTON_LEFT) {
				if (current_anim_no == 0) current_anim_no = storage.numPatterns() - 1;
				else current_anim_no--;
				loadPattern(current_anim_no);
			}
			btnMask = BUTTON_NONE;
			sei();
			btn_debounce = 100;
		}
	} else { btn_debounce--; }

	display.update();
}

void System::shutdown()
{
	modem.disable();
	loadPattern_P(shutdownPattern);
	while (!((PINC & _BV(PC3)) && (PINC & _BV(PC7)))) display.update();
	for (uint8_t i = 0; i < 100; i++) { display.update(); _delay_ms(1); }
	display.disable();
	PRR |= _BV(PRADC);
	PCMSK1 |= _BV(PCINT15) | _BV(PCINT11); PCICR |= _BV(PCIE1);
	SMCR = _BV(SM1) | _BV(SE);
	asm("sleep");
	PCMSK1 &= ~(_BV(PCINT15) | _BV(PCINT11));
	loadPattern(current_anim_no);
	display.enable();
	while (!((PINC & _BV(PC3)) && (PINC & _BV(PC7))));
	for (uint8_t i = 0; i < 100; i++) { display.update(); _delay_ms(1); }
	PRR &= ~_BV(PRADC);
	modem.enable();
	rxExpect = START1;
}

void System::handleTimeout()
{
	modem.disable(); modem.buffer_clear(); modem.enable();
	rxExpect = START1; current_anim_no = 0;
	loadPattern_P(timeoutPattern);
}

ISR(PCINT1_vect) {}
ISR(WDT_vect) { wdt_disable(); rocket.handleTimeout(); }