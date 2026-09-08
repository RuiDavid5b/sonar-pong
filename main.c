#define F_CPU 16000000UL

#include <avr/io.h>
#include <time.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define LED 1<<PD7		// led a piscar a 1 Hz

#define START 1<<PD0	// pino que deteta botão para começar o jogo
#define PAUSE 1<<PD1	// pino que deteta botão para pausar e despausar o jogo

#define SDI 1<<PC0 // serial data input matriz
#define SHCLK 1<<PB1 // shift clock matriz
#define LACLK 1<<PD4 // latch clock matriz

#define SDI_7 1<<PC1 // serial data input 7 segmentos
#define SHCLK_7 1<<PC2 // shift clock 7 segmentos
#define LACLK_7 1<<PC3 // latch clock 7 segmentos

#define TRIG1 1<<PC4 // sonar 1 trigger
#define ECHO1 1<<PD2 // sonar 1 echo
#define TRIG2 1<<PC5 // sonar 2 trigger
#define ECHO2 1<<PD3 // sonar 2 echo

#define fps 25
#define T_timer2A 8 // período timer2A em ms
#define n_row_col 8 // matriz 8x8
#define v_som 343	// velocidade do som, para calcular a distância a que a mão do jogador se encontra
#define tamanho_paleta 3
#define sonar_MIN 5 // distância mínima em cm da mão ao sonar para controlar a paleta
#define sonar_MAX 50.0 // distância máxima em cm da mão ao sonar para controlar a paleta
#define vel_inicial 2	// velocidade com que a bola começa em cada ronda

typedef struct vector {
	float x;
	float y;
} vector;

typedef struct bola_pong {
	vector posicao;
	vector velocidade;
} bola_pong;

volatile char echo_flag = 0;
volatile uint8_t scan_row = 0, frame_counter = 0, hits = 0, start_flag = 0, pnts_player_1 = 0, pnts_player_2 = 0, idx_pal_1 = 0, idx_pal_2 = 0, fim_refresh_img = 0, fim_flag = 0; // magnitude_v -> magnitude da velocidade em pixeis/segundo, idx_pal -> index do 1º pixel em y ocupado pela paleta
volatile uint16_t start_counter = 0, blink_led = 0, fim_counter = 0;
float magnitude_v = 1.0;//, fps_param = 125/fps;
volatile float n_paleta = (sonar_MAX-sonar_MIN)/(n_row_col-tamanho_paleta+1);	// distância que cada posição da paleta tem
bola_pong bola = {
	{3, 2},		// posição inicial (x,y)
	{-1, -1}		// velocidade inicial (x,y)
};

const vector angulos[tamanho_paleta] = {		// escolher os ângulos que a bola faz após colidir com cada ponto da paleta (vetores unitários)
	{sqrt(3)/2, -0.5},			// -30º
	{1.0, 0},						// 0º
	{sqrt(3)/2, 0.5},			// 30º
};

/*const vector angulos2[n_row_col] = {		// escolher os ângulos que a bola faz após colidir com cada ponto da paleta (vetores unitários)
	
	{0.5, -sqrt(3)/2},			// -60º
	{sqrt(2)/2, -sqrt(2)/2},	// -45º
	{sqrt(3)/2, -0.5},			// -30º
	{0.9659, -0.2588},			// -15º
	{0.9659, 0.2588},			// 15º
	{sqrt(3)/2, 0.5},			// 30º
	{sqrt(2)/2, sqrt(2)/2},		// 45º
	{0.5, sqrt(3)/2}			// 60º
};*/

volatile uint8_t pong_display[n_row_col] = {0};		// array que contém os bits de cada frame, em que só no final do frame é que é alterada para o que está no pong_matrix (para evitar que alguns frames desenhem só certos elementos)
volatile uint8_t pong_matrix[n_row_col] = {0};		// array a que é adicionada informação dos estados lógicos de cada elemento da matriz ao longo do frame (paletas, bola, outros)

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

uint8_t numeros[10] = {		// array com números de 0-9 para o display de 7 segmentos
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
	DDRB |= SHCLK;												// configura PB1 (OC1A/PCINT1) como saída
	PORTB |= SHCLK;
	
	DDRC |= (TRIG1|TRIG2|SDI|SDI_7|SHCLK_7|LACLK_7);
	PORTC |= (TRIG1|TRIG2|SHCLK_7);
	DDRD &= ~(ECHO1|ECHO2);			// Echo 1/2 input
	
	DDRD |= LED;
	PORTD |= LED;
	
	// Timer 0A - atualiza display
	TCCR0A |= (1 << WGM01);								// modo CTC
	TCCR0B |= (1 << CS01)|(1 << CS00);					// 64 prescaler
	//TCCR0B |= (1 << CS02)|(1 << CS00);					// 1024 prescaler
	OCR0A = 124;													// freq 1 kHz
	TCNT0 = 0;
	TIMSK0 |= (1 << OCIE0A);											//enable compare match interrupt (COMPA)
	
	// Timer 1A - handle inputs, conta o tempo que o sonar demora a receber o echo
	TCCR1B |= (1 << WGM12);								// modo CTC, 8 prescaler
	OCR1A = 23999;													// T = 12 ms (~83,3 Hz)
	TCNT1 = 0;
	TIMSK1 |= (1 << OCIE1A);											//enable compare match interrupt (COMPA)
	
	// Timer 2A - conta o tempo que cada frame demora
	TCCR2A |= (1 << WGM21);								// modo CTC
	TCCR2B |= (1 << CS22)|(1 << CS21);					// prescaler 256
	OCR2A = 249;													// T = 8 ms (125 Hz)
	TCNT2 = 0;
	TIMSK2 |= (1 << OCIE2A);
	
	// interrupções externas
	EICRA |= (1 << ISC10)|(1 << ISC00);								// qualquer alteração lógica gera uma interrupção
	EIMSK |= (1 << INT1)|(1 << INT0);							// enable interrupções externas INT0 e INT1
	
	sei();															// enable a interupções globais
}

void start() {
	
	for (int i = 0; i < n_row_col; i++) pong_display[i] |= on[i];		// display ao simbolo ON
	display_sete_segmentos();			// display aos pontos nos displays de 7 segmentos
	while (!(PIND & START));				// enquanto não for pressionado o botão de START, ecrã está parado a dar display ao simbolo ON
	start_round(gerador_random(1, 2));			// o lado do primeiro serviço é aleatório
}

void pause_game() {
	if ((PIND & PAUSE)) {			// está a ser pressionado o botão de pausar (polling)
		uint8_t temp[n_row_col] = {0};
		memcpy(temp, (uint8_t*)pong_display, sizeof(pong_display));	// copia a array pong_display para uma array temporária. Quando despausar, volta-se a copiar os conteúdos do vetor temp para o display
		memset((uint8_t*)pong_display, 0, sizeof(pong_display));				// pong_display zerado
		for (int i = 0; i < n_row_col; i++) pong_display[i] |= pause[i];		// display ao simbolo ON
		
		while (PIND & PAUSE);
		while (!(PIND & PAUSE));
		while (PIND & PAUSE); //quando deixar de ser verdade, significa que carregou no botão uma segunda vez, então despausa e continua o jogo
		
		memset((uint8_t*)pong_display, 0, sizeof(pong_display));				// pong_display zerado
		memcpy((uint8_t*)pong_display, temp, sizeof(pong_display));	// volta a colocar no display o que já estava do frame anterior
	}
}

void display_sete_segmentos() {		// display aos pontos de cada jogador
	PORTC &= ~(LACLK_7); // Latch clock LOW
		
	for (int j = 0; j < 8; j++) { // envia nº de pontos do jogador 2
		PORTC &= ~(SHCLK_7); // shift clock LOW
		if ((numeros[pnts_player_2] >> j) & 1) PORTC |= SDI_7;
		else PORTC &= ~(SDI_7);
		PORTC |= SHCLK_7; // shift clock HIGH
	}

	for (int j = 0; j < 8; j++) { // envia nº de pontos do jogador 1
		PORTC &= ~(SHCLK_7); // shift clock LOW
		if ((numeros[pnts_player_1] >> j) & 1) PORTC |= SDI_7;
		else PORTC &= ~(SDI_7);
		PORTC |= SHCLK_7; // shift clock HIGH
	}
		
	PORTC |= LACLK_7; // envia output para o display de 7 segmentos
}

void add_array_to_matrix(volatile uint8_t *array) {
	for (int i = 0; i < n_row_col; i++) pong_matrix[i] |= array[i];		// adiciona a informação da array à matriz, sem substituir o que já lá estava
}

void handle_input(uint16_t counter, volatile uint8_t *idx_pal) {		// converte o input que recebe (counter1) para o índice correspondente da paleta
	float dist_cm = (counter-5000)/40000.0*v_som;			// distância em centímetros
	if ((dist_cm >= sonar_MIN) && (dist_cm < sonar_MAX)) *idx_pal = (volatile uint8_t)(dist_cm-sonar_MIN)/n_paleta; // se o input for válido, atualiza posição da paleta
}

void matriz_display() {
	memcpy((uint8_t*)&pong_display[0], (uint8_t*)&pong_matrix[0], sizeof(pong_display));	// copia a array pong_matrix (temporária) para a array que vai ser usada para dar display (pong_display)
	memset((uint8_t*)&pong_matrix[0], 0, sizeof(pong_matrix));					// preenche a array pong_matrix toda com 0
}

void fim_jogo(uint8_t player_vencedor) {		// quando alguém chega aos 9 pontos o jogo termina
	TCCR1B &= ~(1 << CS11);
	TIMSK1 &= ~(1 << OCIE1A);	// já não vai ser preciso ler os inputs dos sonares, então a interrupção é desativada
					
	memset((uint8_t*)pong_display, 0, sizeof(pong_display));					// preenche a array pong_display toda com 0
	
	fim_flag = 1;
	
	uint8_t a = 0;
					
	if (player_vencedor == 1) {
		while (1) {		// loop infinito, programa chegou ao fim. Para recomeçar tem-se de clicar no botão de reset
			if (fim_refresh_img) {		// vai alternando entre duas imagens o ecrã de vitória do jogador 1
				a ^= 1;		// a alterna entre 0 e 1
				memset((uint8_t*)pong_display, 0, sizeof(pong_display));					// preenche a array pong_display toda com 0
				memcpy((uint8_t*)pong_display, vencedor_1[a], sizeof(pong_display));	// é displayed na matriz o ecrã quando o vencedor é o jogador 1
				fim_refresh_img = 0;
			}
		}
	}
	else {
		while (1) {		// loop infinito, programa chegou ao fim. Para recomeçar tem-se de clicar no botão de reset
			if (fim_refresh_img) {		// vai alternando entre duas imagens o ecrã de vitória do jogador 2 (400 ms)
				a ^= 1;		// a alterna entre 0 e 1
				memset((uint8_t*)pong_display, 0, sizeof(pong_display));					// preenche a array pong_display toda com 0
				memcpy((uint8_t*)pong_display, vencedor_2[a], sizeof(pong_display));	// é displayed na matriz o ecrã quando o vencedor é o jogador 2
				fim_refresh_img = 0;
			}
		}
	}
}

void start_round(uint8_t player) {
	
	if (player == 1) {
		display_sete_segmentos();		// atualiza display 7 segmentos, pois alguém pontuou
		if (pnts_player_1 == 9) fim_jogo(1); // jogador 1 ganhou
	
		srand(TCNT0);		// o seed usado para gerar um número pseudo-aleatório é o tempo do timer 0, que nunca é parado
		bola.posicao.x = n_row_col/2;		// começa na coluna do meio para o lado do jogador 1
		bola.posicao.y = gerador_random(0, n_row_col-1);
		
		magnitude_v = vel_inicial;		// começa a ronda com a velocidade inicial
		int a = gerador_random(0, 1);
		if (a) {	// se o número sorteado for 1
			bola.velocidade.x = -magnitude_v*sqrt(2)/2;	// começa com ângulo de 45º
			bola.velocidade.y = magnitude_v*sqrt(2)/2;
		}
		else {
			bola.velocidade.x = -magnitude_v*sqrt(2)/2;	// começa com ângulo de -45º
			bola.velocidade.y = -magnitude_v*sqrt(2)/2;
		}
	}
	else {
		display_sete_segmentos();		// atualiza display 7 segmentos, pois alguém pontuou
		if (pnts_player_2 == 9) fim_jogo(2); // jogador 2 ganhou
		
		srand(TCNT0);		// o seed usado para gerar um número pseudo-aleatório é o tempo do timer 0, que nunca é parado
		bola.posicao.x = n_row_col/2-1;		// começa na coluna do meio para o lado do jogador 2
		bola.posicao.y = gerador_random(0, n_row_col-1);
		
		magnitude_v = vel_inicial;		// começa a ronda com a velocidade inicial
		int a = gerador_random(0, 1);
		if (a) {	// se o número sorteado for 1
			bola.velocidade.x = magnitude_v*sqrt(2)/2;	// começa com ângulo de (180-45)º = 135º 
			bola.velocidade.y = magnitude_v*sqrt(2)/2;
		}
		else {
			bola.velocidade.x = magnitude_v*sqrt(2)/2;	// começa com ângulo de (180+45)º = 225º
			bola.velocidade.y = -magnitude_v*sqrt(2)/2;
		}		
	}
	start_flag = 1;
}

uint8_t gerador_random(int min, int max) {		// gera número inteiro aleatório entre o intervalo [min, max]
	uint8_t rand_num = (rand() % (max-min+1) + min);
	return rand_num;
}

void input_sonar() {			// envia os triggers aos sonares para gerar o ultrassom
		//TCCR2B |= (1 << CS22)|(1 << CS21);			// começa a contar os milisegundos neste frame
		echo_flag = 0;
		TCNT1 = 0;
		//EIMSK |= (1 << INT0);
		TCCR1B |= (1 << CS11);						// ativa timer1, para ler os inputs provenientes dos sonares
			
		PORTC |= TRIG1;
		_delay_us(10);
		PORTC &= ~(TRIG1);								// sonar 1 envia pulso de 10 us
		while (echo_flag == 0);								// espera até receber input do sonar 1º
			
		//EIMSK &= ~(1 << INT0);
		//EIMSK |= (1 << INT1),
		PORTC |= TRIG2;
		_delay_us(10);
		PORTC &= ~TRIG2;						// sonar 1 envia pulso de 10 us
		while (echo_flag == 1);						// espera até receber input do sonar 2
			
		//EIMSK &= ~(1 << INT1);
}

// interpreta as medições dos sonars para determinar a posição correspondente da paleta
void physics(float delta) {
	
	vector potencial_pos = {		// posição potencial (pode não vir a ser esta a verdadeira posição devido a colisões)
		bola.posicao.x + bola.velocidade.x*delta, bola.posicao.y + bola.velocidade.y*delta
	};
	vector pos_intermedia = {		// em caso de colisão, guarda a posição da bola na interseção. Necessário para usar um novo vetor a partir desse ponto e para preservar a posição original
		bola.posicao.x, bola.posicao.y	
	};
	float fac_tempo = 1.0; // em caso de colisão, reduz o fator de tempo para ter em conta a distância que já percorreu até ao ponto pos_intermedia, corrigindo a magnitude do novo vetor velocidade usado no próximo cálculo
	char colisao = 1;		// inicia-se com 1 para entrar no loop seguinte e verificar se há colisões
	
	while (colisao) { // enquanto houver colisões, corrige a posição e velocidade da bola recursivamente	
		colisao = 0;	// a partir daqui, caso haja alguma colisão este valor passa para 1, e repete o loop para verificar se há mais alguma colisão neste frame
			
		if ((potencial_pos.x >= n_row_col-2) && (bola.posicao.x < n_row_col-2)) {			// se a bola estiver na coluna 7 (ou > 7) e a posição anterior não, verifica se a bola intersetou com a paleta
				
			float intersecao_x = n_row_col-2;
			float fac_tempo_temp = fac_tempo*(potencial_pos.x-intersecao_x)/(potencial_pos.x-pos_intermedia.x); // fator do tempo passado, pois a bola já percorreu alguma distância até à pos_intermedia
			float intersecao_y = potencial_pos.y - fac_tempo_temp*bola.velocidade.y*delta;
			uint8_t idx_intersecao_y = (uint8_t)roundf(intersecao_y);			// índice da interseção
					
			if ((intersecao_y >= 0) && (intersecao_y <= n_row_col-1)) {		// se a condição não for verdade, significa que há outra colisão (com uma parede) que ocorre antes deste caso (colisão/não colisão com a paleta do jogador 2
				if ((idx_intersecao_y >= idx_pal_1) && (idx_intersecao_y <= idx_pal_1+tamanho_paleta-1)) {			// se alguma parte da paleta coincidir com a bola no ponto de interseção
					if (++hits == 10) {																		// a bola colide e conta-se mais um batimento, ao fim de cada 10 batimentos a velocidade aumenta
						magnitude_v *= 1.2; // aumenta a velocidade por cada 3 batimentos da bola a uma paleta
						if (magnitude_v >= 12) magnitude_v = 12.0;
						hits = 0;
					}
					fac_tempo = fac_tempo_temp;
						
					bola.velocidade.x = -magnitude_v*angulos[idx_intersecao_y-idx_pal_1].x; // novo vetor velocidade da bola
					bola.velocidade.y = magnitude_v*angulos[idx_intersecao_y-idx_pal_1].y;
							
					potencial_pos.x = intersecao_x + fac_tempo*bola.velocidade.x*delta;
					potencial_pos.y = intersecao_y + fac_tempo*bola.velocidade.y*delta;
						
					pos_intermedia.x = intersecao_x;		// guarda posição intermédia da bola na interseção com a paleta do jogador 2
					pos_intermedia.y = intersecao_y;
					
					colisao = 1;
				}
				else if (potencial_pos.x > n_row_col-0.5) {		// se a paleta não tiver acertado na bola e a posição da bola for 0 ou menos, é ponto para o jogador 2
					pnts_player_2++;					// +1 ponto para o jogador 2
					start_round(2);						// recomeça jogada, serviço automático do lado do jogador 2
					//break;
					return;
				}
			}
		}
		if ((potencial_pos.x > n_row_col-0.5) && (bola.posicao.x > n_row_col-2)) {		// Se a bola estiver na coluna 7, já é indefensável, então verifica se neste frame já é ponto
			pnts_player_2++;					// +1 ponto para o jogador 2
			start_round(2);						// recomeça jogada, serviço automático do lado do jogador 2
			//break;
			return;
		}
		if ((potencial_pos.x < 1) && (bola.posicao.x >= 1)) {			// se a bola estiver na coluna 0 (ou < 0) e a posição anterior não

			float intersecao_x = 1;
			float fac_tempo_temp = fac_tempo*(potencial_pos.x-intersecao_x)/(potencial_pos.x-pos_intermedia.x); // fator do tempo passado, pois a bola já percorreu alguma distância até à pos_intermedia
			float intersecao_y = potencial_pos.y - fac_tempo_temp*bola.velocidade.y*delta;
			uint8_t idx_intersecao_y = (uint8_t)roundf(intersecao_y);			// índice da interseção
				
			if ((intersecao_y >= 0) && (intersecao_y <= n_row_col-1)) {		// se a condição não for verdade, significa que há outra colisão (com uma parede) que ocorre antes deste caso (colisão/não colisão com a paleta do jogador 1)
				if ((idx_intersecao_y >= idx_pal_2) && (idx_intersecao_y <= idx_pal_2+tamanho_paleta-1)) {			// se alguma parte da paleta coincidir com a bola no ponto de interseção
					if (++hits == 10) {																		// a bola colide e conta-se mais um batimento, ao fim de cada 10 batimentos a velocidade aumenta
						magnitude_v *= 1.2;
						if (magnitude_v >= 12) magnitude_v = 12.0;
						hits = 0;
					}
					fac_tempo = fac_tempo_temp;
						
					bola.velocidade.x = magnitude_v*angulos[idx_intersecao_y-idx_pal_2].x; // novo vetor velocidade da bola
					bola.velocidade.y = magnitude_v*angulos[idx_intersecao_y-idx_pal_2].y;
						
					potencial_pos.x = intersecao_x + fac_tempo*bola.velocidade.x*delta;
					potencial_pos.y = intersecao_y + fac_tempo*bola.velocidade.y*delta;
						
					pos_intermedia.x = intersecao_x;		// guarda posição intermédia da bola na interseção com a paleta do jogador 2
					pos_intermedia.y = intersecao_y;
					
					colisao = 1;
				}
				else if (potencial_pos.x < 0) {		// se a paleta não tiver acertado na bola e a posição da bola for 0 ou menos, é ponto para o jogador 1
					pnts_player_1++;					// +1 ponto para o jogador 1
					start_round(1);						// recomeça jogada, serviço automático do lado do jogador 1
					//break;
					return;
				}
			}
		}
		if ((potencial_pos.x < -0.5) && (bola.posicao.x < 1)) {		// Se a bola estiver na coluna 0, já é indefensável, então verifica se neste frame já é ponto
			pnts_player_1++;					// +1 ponto para o jogador 1
			start_round(1);						// recomeça jogada, serviço automático do lado do jogador 1
			//break;
			return;
		}
		if (potencial_pos.y > n_row_col-1) {		// colisão com parede inferior
				
			float intersecao_y = n_row_col-1;
			float fac_tempo_temp = fac_tempo*(potencial_pos.y-intersecao_y)/(potencial_pos.y-pos_intermedia.y);	// (potencial_pos.y-0)/(potencial_pos.y-pos_intermedia.y); fator do tempo passado para corrigir a magnitude do vetor após interseção (começando no ponto pos_intermedia até à posição potencial)
			float intersecao_x = potencial_pos.x - fac_tempo_temp*bola.velocidade.x*delta;		// guarda posição intermédia da bola na interseção com a parede inferior
			if ((intersecao_x >= 0) && (intersecao_x <= n_row_col-1)) {			// se a condição não for verdade, significa que há outra colisão/ponto que ocorre antes desta colisão com a parede inferior
				fac_tempo = fac_tempo_temp;
				pos_intermedia.x = intersecao_x;
				pos_intermedia.y = intersecao_y;
				potencial_pos.y = intersecao_y*2 - potencial_pos.y;		// posição potencial em y seguinte considerando a tabela
				bola.velocidade.y *= -1;								// inverte velocidade em y	
				colisao = 1;
			}
		}
		if (potencial_pos.y < 0) {					// colisão com parede superior
				
			float intersecao_y = 0;
			float fac_tempo_temp = fac_tempo*(potencial_pos.y-intersecao_y)/(potencial_pos.y-pos_intermedia.y);	// (potencial_pos.y-0)/(potencial_pos.y-pos_intermedia.y); fator do tempo passado para corrigir a magnitude do vetor após interseção (começando no ponto pos_intermedia até à posição potencial)
			float intersecao_x = potencial_pos.x - fac_tempo_temp*bola.velocidade.x*delta;		// guarda posição intermédia da bola na interseção com a parede superior
			if ((intersecao_x >= 0) && (intersecao_x <= n_row_col-1)) {			// se a condição não for verdade, significa que há outra colisão/ponto que ocorre antes desta colisão com a parede superior
				fac_tempo = fac_tempo_temp;
				pos_intermedia.x = intersecao_x;
				pos_intermedia.y = intersecao_y;
				potencial_pos.y = intersecao_y*2 - potencial_pos.y;						// posição potencial em y seguinte considerando a tabela
				bola.velocidade.y *= -1;					// inverte velocidade em y
				colisao = 1;
			}
		}
	}										// nenhum caso especial
	bola.posicao.x = potencial_pos.x;				// atualiza posição em x
	bola.posicao.y = potencial_pos.y;				// atualiza posição em y
}

int main(void)
{
	init();					// chama função para fazer inicializações
	start();
    while (1) 
    {	
		pause_game();			// verifica se o botão para pausar está a ser pressionado (polling)
			
		TCNT2 = 0;													// reset timer2A (começa a contar o tempo deste frame)
		frame_counter = 0;

		input_sonar();		// sonares enviam ultrassons e são lidos os inputs nas interrupções externas

		for (int i = 0; i < tamanho_paleta; i++) pong_matrix[idx_pal_1+i] |= (1 << (n_row_col-1));		// atualiza a matriz adicionando a paleta 1
		for (int i = 0; i < tamanho_paleta; i++) pong_matrix[idx_pal_2+i] |= 1;		// atualiza a matriz adicionando a paleta 2
		
		while (frame_counter < 5);		// enquanto não se passarem 40 ms neste frame (25 fps), esperar até tal tempo
		float delta = (frame_counter + TCNT2/(OCR2A+1))*8.0/1000;		// calcula o tempo que se passou neste frame
		
		if (!start_flag) {					// se start_flag != 1, então é para calcular a posição seguinte da bola. Se start_flag for 1, a bola não é displayed
			physics(delta);									// chamar função physics para calcular a posição e velocidade da bola
			pong_matrix[(uint8_t)roundf(bola.posicao.y)] |= (1 << (uint8_t)roundf(bola.posicao.x));		// atualiza a matriz adicionando a bola
		}
		else {							// start_flag = 1, inicia contagem decrescente até começar nova ronda
			add_array_to_matrix(countdown[start_counter/1500]);	// 50*(8 ms) = 400 ms = 1/3*(1,8 s), tempo de espera até iniciar a próxima ronda
		}
		matriz_display();							// atualiza a matriz usada para o display
	}
}

// atualiza display
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
	
	PORTD &= ~LACLK; // Latch clock LOW
	
	int a = (1 << scan_row);
	for (int j = 0; j < n_row_col; j++) { // envia a linha
		PORTB &= ~SHCLK; // shift clock LOW
		if ((a >> j) & 1) PORTC &= ~SDI;
		else PORTC |= SDI;
		PORTB |= SHCLK; // shift clock HIGH
	}
	
	for (int i = n_row_col-1; i >= 0; i--)	{ // envia as colunas dessa linha
		PORTB &= ~SHCLK; // shift clock LOW
		if ((pong_display[scan_row] >> i) & 1) PORTC |= SDI; // verifica se o i-ésimo bit é 1 (diferente de 0 -> true)
		else PORTC &= ~SDI;
		PORTB |= SHCLK; // shift clock HIGH
	}
	
	if (++scan_row >= n_row_col) scan_row = 0; // avança para a linha seguinte
	
	PORTD |= LACLK; // envia output para a matriz
}

// Trigger sonar
ISR (TIMER1_COMPA_vect) { 
	if (++echo_flag == 2) {		// ambos os inputs dos sonares já foram obtidos
		TCCR1B &= ~(1 << CS11);		// no resto deste frame, para timer1
		TCNT1 = 0;					// reset ao counter1
	}
}

// conta o tempo que cada frame demora
ISR (TIMER2_COMPA_vect) {
	frame_counter++;				// conta 8 ms de cada vez (tempo = j*8 ms)
}

// Sonar 1 echo count
ISR (INT0_vect) {
	uint16_t echo_cnt = TCNT1;				// captura o timer counter 1
	handle_input(echo_cnt, &idx_pal_1);		// e calcula o local onde a paleta tem de estar
}

// Sonar 2 echo count
ISR (INT1_vect) {
	uint16_t echo_cnt = TCNT1;				// captura o timer counter 1
	handle_input(echo_cnt, &idx_pal_2);		// e calcula o local onde a paleta tem de estar
}