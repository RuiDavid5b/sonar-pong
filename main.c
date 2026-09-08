#define F_CPU 16000000UL

#include <avr/io.h>
#include <time.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define LED 1<<PD7

#define START 1<<PD0	// start button
#define PAUSE 1<<PD1	// pause/unpause button

#define SDI 1<<PC0 // serial data input, LED matrix
#define SHCLK 1<<PB1 // shift clock, LED matrix
#define LACLK 1<<PD4 // latch clock, LED matrix

#define SDI_7 1<<PC1 // serial data input, 7-segment displays
#define SHCLK_7 1<<PC2 // shift clock, 7-segment displays
#define LACLK_7 1<<PC3 // latch clock, 7-segment displays

#define TRIG1 1<<PC4 // sonar 1 trigger
#define ECHO1 1<<PD2 // sonar 1 echo
#define TRIG2 1<<PC5 // sonar 2 trigger
#define ECHO2 1<<PD3 // sonar 2 echo

#define fps 25
#define T_timer2A 8 // timer2A period in ms
#define n_row_col 8 // 8x8 matrix
#define v_som 343	// speed of sound (m/s), used to convert sonar echo time to hand distance
#define tamanho_paleta 3
#define sonar_MIN 5 // min hand distance (cm) mapped to paddle range
#define sonar_MAX 50.0 // max hand distance (cm) mapped to paddle range
#define vel_inicial 2	// ball speed at the start of each round

typedef struct vector {
	float x;
	float y;
} vector;

typedef struct bola_pong {
	vector posicao;
	vector velocidade;
} bola_pong;

volatile char echo_flag = 0;
volatile uint8_t scan_row = 0, frame_counter = 0, hits = 0, start_flag = 0, pnts_player_1 = 0, pnts_player_2 = 0, idx_pal_1 = 0, idx_pal_2 = 0, fim_refresh_img = 0, fim_flag = 0; // idx_pal_x = index of the first row occupied by that player's paddle
volatile uint16_t start_counter = 0, blink_led = 0, fim_counter = 0;
float magnitude_v = 1.0;//, fps_param = 125/fps;
volatile float n_paleta = (sonar_MAX-sonar_MIN)/(n_row_col-tamanho_paleta+1);	// cm of hand movement per paddle position
bola_pong bola = {
	{3, 2},
	{-1, -1}
};

const vector angulos[tamanho_paleta] = {		// unit vectors for bounce angle depending on which part of the paddle is hit
	{sqrt(3)/2, -0.5},			// -30 deg
	{1.0, 0},						// 0 deg
	{sqrt(3)/2, 0.5},			// 30 deg
};

/*const vector angulos2[n_row_col] = {
	
	{0.5, -sqrt(3)/2},			// -60 deg
	{sqrt(2)/2, -sqrt(2)/2},	// -45 deg
	{sqrt(3)/2, -0.5},			// -30 deg
	{0.9659, -0.2588},			// -15 deg
	{0.9659, 0.2588},			// 15 deg
	{sqrt(3)/2, 0.5},			// 30 deg
	{sqrt(2)/2, sqrt(2)/2},		// 45 deg
	{0.5, sqrt(3)/2}			// 60 deg
};*/

volatile uint8_t pong_display[n_row_col] = {0};	// buffer actually shown; only swapped in once a frame is fully built, to avoid tearing
volatile uint8_t pong_matrix[n_row_col] = {0};		// scratch buffer built up during the frame

volatile uint8_t countdown[3][n_row_col] = {
	{
		0b00000000,
		0b00011000,
		0b00100100,
		0b00001000,
		0b00100100,
		0b00011000,
		0b00000000,
		0b00000000
	},
	{
		0b00000000,
		0b00011000,
		0b00100100,
		0b00001000,
		0b00010000,
		0b00111100,
		0b00000000,
		0b00000000
	},
	{
		0b00000000,
		0b00011000,
		0b00101000,
		0b00001000,
		0b00001000,
		0b00011100,
		0b00000000,
		0b00000000
	}
};

uint8_t on[n_row_col] = {
	0b00011000,
	0b00111100,
	0b01011010,
	0b01011010,
	0b01000010,
	0b01000010,
	0b00111100,
	0b00000000
};

uint8_t pause[n_row_col] = {
	0b00000000,
	0b00100100,
	0b00100100,
	0b00100100,
	0b00100100,
	0b00100100,
	0b00100100,
	0b00000000
};

uint8_t numeros[10] = {
	0b11111100,
	0b01100000,
	0b11011010,
	0b11110010,
	0b01100110,
	0b10110110,
	0b10111110,
	0b11100000,
	0b11111110,
	0b11100110
};

uint8_t vencedor_1[2][n_row_col] = {
	{
		0b00000001,
		0b10011000,
		0b00101001,
		0b10001000,
		0b00001001,
		0b10011100,
		0b00000001,
		0b10000000
	},
	{
		0b10000000,
		0b00011001,
		0b10101000,
		0b00001001,
		0b10001000,
		0b00011101,
		0b10000000,
		0b00000001
	}
};

uint8_t vencedor_2[2][n_row_col] = {
	{
		0b00000001,
		0b10011000,
		0b00100101,
		0b10001000,
		0b00010001,
		0b10111100,
		0b00000001,
		0b10000000
	},
	{
		0b10000000,
		0b00011001,
		0b10100100,
		0b00001001,
		0b10010000,
		0b00111101,
		0b10000000,
		0b00000001
	}
};

void init();
void start();
void pause_game();
void add_array_to_matrix(volatile uint8_t *array);
void handle_input(uint16_t counter, volatile uint8_t *idx_pal);
void start_round(uint8_t player);
uint8_t gerador_random(int min, int max);
void physics(float delta);	
void matriz_display();
void display_sete_segmentos();
void fim_jogo(uint8_t player_vencedor);

void init()
{
	DDRD &= ~(START);
	
	DDRD |= LACLK;
	DDRB |= SHCLK;
	PORTB |= SHCLK;
	
	DDRC |= (TRIG1|TRIG2|SDI|SDI_7|SHCLK_7|LACLK_7);
	PORTC |= (TRIG1|TRIG2|SHCLK_7);
	DDRD &= ~(ECHO1|ECHO2);
	
	DDRD |= LED;
	PORTD |= LED;
	
	// Timer 0A - drives display refresh, 1 kHz (CTC, /64 prescaler)
	TCCR0A |= (1 << WGM01);
	TCCR0B |= (1 << CS01)|(1 << CS00);
	//TCCR0B |= (1 << CS02)|(1 << CS00);					// 1024 prescaler
	OCR0A = 124;
	TCNT0 = 0;
	TIMSK0 |= (1 << OCIE0A);
	
	// Timer 1A - times how long each sonar takes to echo back, ~83.3 Hz (CTC, /8 prescaler)
	TCCR1B |= (1 << WGM12);
	OCR1A = 23999;
	TCNT1 = 0;
	TIMSK1 |= (1 << OCIE1A);
	
	// Timer 2A - frame clock, 125 Hz / 8 ms per tick (CTC, /256 prescaler)
	TCCR2A |= (1 << WGM21);
	TCCR2B |= (1 << CS22)|(1 << CS21);
	OCR2A = 249;
	TCNT2 = 0;
	TIMSK2 |= (1 << OCIE2A);
	
	EICRA |= (1 << ISC10)|(1 << ISC00);	// INT0/INT1 fire on any logic change
	EIMSK |= (1 << INT1)|(1 << INT0);
	
	sei();
}

void start() {
	
	for (int i = 0; i < n_row_col; i++) pong_display[i] |= on[i];
	display_sete_segmentos();
	while (!(PIND & START));
	start_round(gerador_random(1, 2));		// random side serves first
}

void pause_game() {
	if ((PIND & PAUSE)) {
		uint8_t temp[n_row_col] = {0};
		memcpy(temp, (uint8_t*)pong_display, sizeof(pong_display));	// stash current frame so it can be restored on unpause
		memset((uint8_t*)pong_display, 0, sizeof(pong_display));
		for (int i = 0; i < n_row_col; i++) pong_display[i] |= pause[i];
		
		while (PIND & PAUSE);
		while (!(PIND & PAUSE));
		while (PIND & PAUSE);		// waits for a second press before resuming
		
		memset((uint8_t*)pong_display, 0, sizeof(pong_display));
		memcpy((uint8_t*)pong_display, temp, sizeof(pong_display));
	}
}

void display_sete_segmentos() {
	PORTC &= ~(LACLK_7);
		
	for (int j = 0; j < 8; j++) { // player 2's score
		PORTC &= ~(SHCLK_7);
		if ((numeros[pnts_player_2] >> j) & 1) PORTC |= SDI_7;
		else PORTC &= ~(SDI_7);
		PORTC |= SHCLK_7;
	}

	for (int j = 0; j < 8; j++) { // player 1's score
		PORTC &= ~(SHCLK_7);
		if ((numeros[pnts_player_1] >> j) & 1) PORTC |= SDI_7;
		else PORTC &= ~(SDI_7);
		PORTC |= SHCLK_7;
	}
		
	PORTC |= LACLK_7;
}

void add_array_to_matrix(volatile uint8_t *array) {
	for (int i = 0; i < n_row_col; i++) pong_matrix[i] |= array[i];
}

void handle_input(uint16_t counter, volatile uint8_t *idx_pal) {
	float dist_cm = (counter-5000)/40000.0*v_som;		// echo time -> distance (speed of sound / 2, timer tick scaling folded in)
	if ((dist_cm >= sonar_MIN) && (dist_cm < sonar_MAX)) *idx_pal = (volatile uint8_t)(dist_cm-sonar_MIN)/n_paleta;
}

void matriz_display() {
	memcpy((uint8_t*)&pong_display[0], (uint8_t*)&pong_matrix[0], sizeof(pong_display));
	memset((uint8_t*)&pong_matrix[0], 0, sizeof(pong_matrix));
}

void fim_jogo(uint8_t player_vencedor) {
	TCCR1B &= ~(1 << CS11);
	TIMSK1 &= ~(1 << OCIE1A);	// sonar reading no longer needed once the game is over
					
	memset((uint8_t*)pong_display, 0, sizeof(pong_display));
	
	fim_flag = 1;
	
	uint8_t a = 0;
					
	if (player_vencedor == 1) {
		while (1) {		// game over; only a reset can restart it
			if (fim_refresh_img) {
				a ^= 1;
				memset((uint8_t*)pong_display, 0, sizeof(pong_display));
				memcpy((uint8_t*)pong_display, vencedor_1[a], sizeof(pong_display));
				fim_refresh_img = 0;
			}
		}
	}
	else {
		while (1) {
			if (fim_refresh_img) {
				a ^= 1;
				memset((uint8_t*)pong_display, 0, sizeof(pong_display));
				memcpy((uint8_t*)pong_display, vencedor_2[a], sizeof(pong_display));
				fim_refresh_img = 0;
			}
		}
	}
}

void start_round(uint8_t player) {
	
	if (player == 1) {
		display_sete_segmentos();
		if (pnts_player_1 == 9) fim_jogo(1);
	
		srand(TCNT0);
		bola.posicao.x = n_row_col/2;
		bola.posicao.y = gerador_random(0, n_row_col-1);
		
		magnitude_v = vel_inicial;
		int a = gerador_random(0, 1);
		if (a) {
			bola.velocidade.x = -magnitude_v*sqrt(2)/2;	// +45 deg
			bola.velocidade.y = magnitude_v*sqrt(2)/2;
		}
		else {
			bola.velocidade.x = -magnitude_v*sqrt(2)/2;	// -45 deg
			bola.velocidade.y = -magnitude_v*sqrt(2)/2;
		}
	}
	else {
		display_sete_segmentos();
		if (pnts_player_2 == 9) fim_jogo(2);
		
		srand(TCNT0);
		bola.posicao.x = n_row_col/2-1;
		bola.posicao.y = gerador_random(0, n_row_col-1);
		
		magnitude_v = vel_inicial;
		int a = gerador_random(0, 1);
		if (a) {
			bola.velocidade.x = magnitude_v*sqrt(2)/2;	// 135 deg
			bola.velocidade.y = magnitude_v*sqrt(2)/2;
		}
		else {
			bola.velocidade.x = magnitude_v*sqrt(2)/2;	// 225 deg
			bola.velocidade.y = -magnitude_v*sqrt(2)/2;
		}		
	}
	start_flag = 1;
}

uint8_t gerador_random(int min, int max) {
	uint8_t rand_num = (rand() % (max-min+1) + min);
	return rand_num;
}

void input_sonar() {
		//TCCR2B |= (1 << CS22)|(1 << CS21);
		echo_flag = 0;
		TCNT1 = 0;
		//EIMSK |= (1 << INT0);
		TCCR1B |= (1 << CS11);
			
		PORTC |= TRIG1;
		_delay_us(10);
		PORTC &= ~(TRIG1);
		while (echo_flag == 0);	// wait for sonar 1's echo
			
		//EIMSK &= ~(1 << INT0);
		//EIMSK |= (1 << INT1),
		PORTC |= TRIG2;
		_delay_us(10);
		PORTC &= ~TRIG2;
		while (echo_flag == 1);	// wait for sonar 2's echo
			
		//EIMSK &= ~(1 << INT1);
}

void physics(float delta) {
	
	vector potencial_pos = {		// where the ball would end up this frame, before collision checks
		bola.posicao.x + bola.velocidade.x*delta, bola.posicao.y + bola.velocidade.y*delta
	};
	vector pos_intermedia = {		// last confirmed position; becomes the collision point if one is found
		bola.posicao.x, bola.posicao.y	
	};
	float fac_tempo = 1.0;		// fraction of this frame's delta still remaining, shrinks after each collision along the way
	char colisao = 1;
	
	while (colisao) {		// re-checks for further collisions after each bounce, within the same frame
		colisao = 0;
			
		if ((potencial_pos.x >= n_row_col-2) && (bola.posicao.x < n_row_col-2)) {		// crossed into player 2's paddle column this frame

			float intersecao_x = n_row_col-2;
			float fac_tempo_temp = fac_tempo*(potencial_pos.x-intersecao_x)/(potencial_pos.x-pos_intermedia.x);	// time fraction at which the crossing actually happens
			float intersecao_y = potencial_pos.y - fac_tempo_temp*bola.velocidade.y*delta;
			uint8_t idx_intersecao_y = (uint8_t)roundf(intersecao_y);
					
			if ((intersecao_y >= 0) && (intersecao_y <= n_row_col-1)) {		// otherwise a wall collision happens first, handled below
				if ((idx_intersecao_y >= idx_pal_1) && (idx_intersecao_y <= idx_pal_1+tamanho_paleta-1)) {		// paddle hit
					if (++hits == 10) {
						magnitude_v *= 1.2;		// speed ramps up every 10 paddle hits, capped at 12
						if (magnitude_v >= 12) magnitude_v = 12.0;
						hits = 0;
					}
					fac_tempo = fac_tempo_temp;
						
					bola.velocidade.x = -magnitude_v*angulos[idx_intersecao_y-idx_pal_1].x;
					bola.velocidade.y = magnitude_v*angulos[idx_intersecao_y-idx_pal_1].y;
							
					potencial_pos.x = intersecao_x + fac_tempo*bola.velocidade.x*delta;
					potencial_pos.y = intersecao_y + fac_tempo*bola.velocidade.y*delta;
						
					pos_intermedia.x = intersecao_x;
					pos_intermedia.y = intersecao_y;
					
					colisao = 1;
				}
				else if (potencial_pos.x > n_row_col-0.5) {	// missed the paddle -> point for player 2
					pnts_player_2++;
					start_round(2);
					//break;
					return;
				}
			}
		}
		if ((potencial_pos.x > n_row_col-0.5) && (bola.posicao.x > n_row_col-2)) {	// already past the paddle column, unreturnable
			pnts_player_2++;
			start_round(2);
			//break;
			return;
		}
		if ((potencial_pos.x < 1) && (bola.posicao.x >= 1)) {		// crossed into player 1's paddle column this frame

			float intersecao_x = 1;
			float fac_tempo_temp = fac_tempo*(potencial_pos.x-intersecao_x)/(potencial_pos.x-pos_intermedia.x);
			float intersecao_y = potencial_pos.y - fac_tempo_temp*bola.velocidade.y*delta;
			uint8_t idx_intersecao_y = (uint8_t)roundf(intersecao_y);
				
			if ((intersecao_y >= 0) && (intersecao_y <= n_row_col-1)) {
				if ((idx_intersecao_y >= idx_pal_2) && (idx_intersecao_y <= idx_pal_2+tamanho_paleta-1)) {
					if (++hits == 10) {
						magnitude_v *= 1.2;
						if (magnitude_v >= 12) magnitude_v = 12.0;
						hits = 0;
					}
					fac_tempo = fac_tempo_temp;
						
					bola.velocidade.x = magnitude_v*angulos[idx_intersecao_y-idx_pal_2].x;
					bola.velocidade.y = magnitude_v*angulos[idx_intersecao_y-idx_pal_2].y;
						
					potencial_pos.x = intersecao_x + fac_tempo*bola.velocidade.x*delta;
					potencial_pos.y = intersecao_y + fac_tempo*bola.velocidade.y*delta;
						
					pos_intermedia.x = intersecao_x;
					pos_intermedia.y = intersecao_y;
					
					colisao = 1;
				}
				else if (potencial_pos.x < 0) {	// missed the paddle -> point for player 1
					pnts_player_1++;
					start_round(1);
					//break;
					return;
				}
			}
		}
		if ((potencial_pos.x < -0.5) && (bola.posicao.x < 1)) {	// already past the paddle column, unreturnable
			pnts_player_1++;
			start_round(1);
			//break;
			return;
		}
		if (potencial_pos.y > n_row_col-1) {		// bounce off bottom wall
				
			float intersecao_y = n_row_col-1;
			float fac_tempo_temp = fac_tempo*(potencial_pos.y-intersecao_y)/(potencial_pos.y-pos_intermedia.y);
			float intersecao_x = potencial_pos.x - fac_tempo_temp*bola.velocidade.x*delta;
			if ((intersecao_x >= 0) && (intersecao_x <= n_row_col-1)) {
				fac_tempo = fac_tempo_temp;
				pos_intermedia.x = intersecao_x;
				pos_intermedia.y = intersecao_y;
				potencial_pos.y = intersecao_y*2 - potencial_pos.y;	// mirror the overshoot back across the wall
				bola.velocidade.y *= -1;
				colisao = 1;
			}
		}
		if (potencial_pos.y < 0) {		// bounce off top wall
				
			float intersecao_y = 0;
			float fac_tempo_temp = fac_tempo*(potencial_pos.y-intersecao_y)/(potencial_pos.y-pos_intermedia.y);
			float intersecao_x = potencial_pos.x - fac_tempo_temp*bola.velocidade.x*delta;
			if ((intersecao_x >= 0) && (intersecao_x <= n_row_col-1)) {
				fac_tempo = fac_tempo_temp;
				pos_intermedia.x = intersecao_x;
				pos_intermedia.y = intersecao_y;
				potencial_pos.y = intersecao_y*2 - potencial_pos.y;
				bola.velocidade.y *= -1;
				colisao = 1;
			}
		}
	}
	bola.posicao.x = potencial_pos.x;
	bola.posicao.y = potencial_pos.y;
}

int main(void)
{
	init();
	start();
    while (1) 
    {	
		pause_game();
			
		TCNT2 = 0;
		frame_counter = 0;

		input_sonar();

		for (int i = 0; i < tamanho_paleta; i++) pong_matrix[idx_pal_1+i] |= (1 << (n_row_col-1));
		for (int i = 0; i < tamanho_paleta; i++) pong_matrix[idx_pal_2+i] |= 1;
		
		while (frame_counter < 5);		// pace the loop to 25 fps (40 ms/frame)
		float delta = (frame_counter + TCNT2/(OCR2A+1))*8.0/1000;

		if (!start_flag) {
			physics(delta);
			pong_matrix[(uint8_t)roundf(bola.posicao.y)] |= (1 << (uint8_t)roundf(bola.posicao.x));
		}
		else {
			add_array_to_matrix(countdown[start_counter/1500]);	// ~1.8 s countdown before serve, split into 3 stages
		}
		matriz_display();
	}
}

ISR (TIMER0_COMPA_vect) {
	
	if (start_flag) {
		if (++start_counter >= 4500) {
			start_counter = 0, start_flag = 0;
		}
	}
	
	if (++blink_led == 1000) {
		blink_led = 0;
		PORTD ^= LED;
	}
	
	if (fim_flag)
		if (++fim_counter == 800) {
			fim_refresh_img = 1;
			fim_counter = 0;
		}
	
	PORTD &= ~LACLK;
	
	int a = (1 << scan_row);
	for (int j = 0; j < n_row_col; j++) {
		PORTB &= ~SHCLK;
		if ((a >> j) & 1) PORTC &= ~SDI;
		else PORTC |= SDI;
		PORTB |= SHCLK;
	}
	
	for (int i = n_row_col-1; i >= 0; i--)	{
		PORTB &= ~SHCLK;
		if ((pong_display[scan_row] >> i) & 1) PORTC |= SDI;
		else PORTC &= ~SDI;
		PORTB |= SHCLK;
	}
	
	if (++scan_row >= n_row_col) scan_row = 0;
	
	PORTD |= LACLK;
}

// fires once both sonar echoes have been captured
ISR (TIMER1_COMPA_vect) { 
	if (++echo_flag == 2) {
		TCCR1B &= ~(1 << CS11);
		TCNT1 = 0;
	}
}

ISR (TIMER2_COMPA_vect) {
	frame_counter++;
}

ISR (INT0_vect) {
	uint16_t echo_cnt = TCNT1;
	handle_input(echo_cnt, &idx_pal_1);
}

ISR (INT1_vect) {
	uint16_t echo_cnt = TCNT1;
	handle_input(echo_cnt, &idx_pal_2);
}
