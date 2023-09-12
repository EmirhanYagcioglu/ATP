/* p3_2.c: Initialize and display "hello" on the LCD using 8-bit data mode.
* Data pins use Port D, control pins use Port A.
* Polling of the busy bit of the LCD status bit is used for timing.
*/
#include <MKL25Z4.h>

#define RS 0x1000 /* PTA12 mask */
#define RW 0x10 /* PTA4 mask */
#define EN 0x20 /* PTA5 mask */

void delayMs(int n);
void UART0_init(void);
void UART0_IRQHandler(void);
void LCD_init(void);
void LCD_ready(void);
void LCD_command(unsigned char command);
void LCD_command_noWait(unsigned char command);
void LCD_data(unsigned char data);
void LCD_write_string(char data[], int len);
void LCD_write_int(int data);
void LCD_cmd(void);
void txData(char data[]);
void commandInterpret(void);

static const int STANDARD_DATA_PACKET_SIZE = 4;
static int index = 0;

static char incomingCommand[STANDARD_DATA_PACKET_SIZE];

static volatile int receiveFlag = 0;

int main(void) {
	__disable_irq(); 	/* global disable IRQs */
	UART0_init();		/* UART is activated */
	LCD_init();			/* LCD is activated */
	__enable_irq(); 	/* global enable IRQs */
	
	for(;;)	{
		if (receiveFlag) {
			LCD_cmd();
			txData(incomingCommand);
			receiveFlag = 0;
		}
	}
}

/* Delay n milliseconds
 * The CPU core clock is set to MCGFLLCLK at 41.94 MHz in SystemInit().
 */
void delayMs(int n) {
	int i, j;
	for(i = 0 ; i < n; i++) {
		for(j = 0 ; j < 3500; j++) {}
	}
}

/* UART0 initializer
 * Activate UART0 as both reciever and transmitter to enable two-way communication
 * between the commander module ATPAPP and KL25Z
 */
void UART0_init(void) {
	SIM->SCGC4 |= 0x0400; 			/* enable clock for UART0 */
	SIM->SOPT2 |= 0x04000000; 		/* use FLL output for UART Baud rate generator */
	UART0->C2 = 0; 					/* turn off UART0 while changing configurations */
	UART0->BDH = 0x00;
	UART0->BDL = 0x0C; 				/* 115200 Baud */
	UART0->C4 = 0x0F; 				/* Over Sampling Ratio 16 */
	UART0->C1 = 0x00; 				/* 8-bit data */
	UART0->C2 = 0x2C; 				/* enable receive, receive interrupt and transmit*/
	NVIC->ISER[0] |= 0x00001000; 	/* enable INT12 (bit 12 of ISER[0]) */
	SIM->SCGC5 |= 0x0200; 			/* enable clock for PORTA */
	PORTA->PCR[1] = 0x0200; 		/* make PTA1 UART0_Rx pin */
	PORTA->PCR[2] = 0x0200; 		/* make PTA2 UART0_Tx pin */
}

/* UART0 interrupt handler 
 * UART0 receiver. Reads serial commands sent by the commander module in ATPAPP 
 */
void UART0_IRQHandler(void) {
	char c;
	c = UART0->D; /* read the char received */
	incomingCommand[index] = c;
	index++;
	if (index == STANDARD_DATA_PACKET_SIZE) {
		index = 0;
		receiveFlag = 1;
	}
}

/* LCD initializer
 * LCD is used for displaying current KL25Z status.
 */
void LCD_init(void) {
	SIM->SCGC5 |= 0x1000; /* enable clock to Port D */
	PORTD->PCR[0] = 0x100; /* make PTD0 pin as GPIO */
	PORTD->PCR[1] = 0x100; /* make PTD1 pin as GPIO */
	PORTD->PCR[2] = 0x100; /* make PTD2 pin as GPIO */
	PORTD->PCR[3] = 0x100; /* make PTD3 pin as GPIO */
	PORTD->PCR[4] = 0x100; /* make PTD4 pin as GPIO */
	PORTD->PCR[5] = 0x100; /* make PTD5 pin as GPIO */
	PORTD->PCR[6] = 0x100; /* make PTD6 pin as GPIO */
	PORTD->PCR[7] = 0x100; /* make PTD7 pin as GPIO */
	PTD->PDDR = 0xFF; /* make PTD7-0 as output pins */
	SIM->SCGC5 |= 0x0200; /* enable clock to Port A */
	PORTA->PCR[12] = 0x100; /* make PTA12 pin as GPIO */
	PORTA->PCR[4] = 0x100; /* make PTA4 pin as GPIO */
	PORTA->PCR[5] = 0x100; /* make PTA5 pin as GPIO */
	PTA->PDDR |= 0x1030; /* make PTA5, 4, 12 as output pins */
	delayMs(20); /* initialization sequence */
	LCD_command_noWait(0x30); /* LCD does not respond to status poll */
	delayMs(5);
	LCD_command_noWait(0x30);
	delayMs(1);
	LCD_command_noWait(0x30);
	LCD_command(0x38); /* set 8-bit data, 2-line, 5x7 font */
	LCD_command(0x06); /* move cursor right */
	LCD_command(0x01); /* clear screen, move cursor to home */
	LCD_command(0x0F); /* turn on display, cursor blinking */
}

/* This function waits until LCD controller is ready to
 * accept a new command/data before returns.
 */
void LCD_ready(void) {
	char status;
	PTD->PDDR = 0; /* PortD input */
	PTA->PCOR = RS; /* RS = 0 for status */
	PTA->PSOR = RW; /* R/W = 1, LCD output */
	do { /* stay in the loop until it is not busy */
		PTA->PSOR = EN; /* raise E */
		delayMs(0);
		status = (char) PTD->PDIR; /* read status register */
		PTA->PCOR = EN;
		delayMs(0); /* clear E */
	} while (status & 0x80); /* check busy bit */
	PTA->PCOR = RW; /* R/W = 0, LCD input */
	PTD->PDDR = 0xFF; /* PortD output */
}

/* LCD command sender - wait for ready
 * Sends the LCD a command when it is able to receive.
 */
void LCD_command(unsigned char command) {
	LCD_ready(); /* wait until LCD is ready */
	PTA->PCOR = RS | RW; /* RS = 0, R/W = 0 */
	PTD->PDOR = command;
	PTA->PSOR = EN; /* pulse E */
	delayMs(0);
	PTA->PCOR = EN;
}

/* LCD command sender - no wait
 * Sends the LCD a command immediately.
 */
void LCD_command_noWait(unsigned char command) {
	PTA->PCOR = RS | RW; /* RS = 0, R/W = 0 */
	PTD->PDOR = command;
	PTA->PSOR = EN; /* pulse E */
	delayMs(0);
	PTA->PCOR = EN;
}

/* LCD data sender
 * Sends a character to the LCD for display
 */
void LCD_data(unsigned char data) {
	LCD_ready(); /* wait until LCD is ready */
	PTA->PSOR = RS; /* RS = 1, R/W = 0 */
	PTA->PCOR = RW;
	PTD->PDOR = data;
	PTA->PSOR = EN; /* pulse E */
	delayMs(0);
	PTA->PCOR = EN;
}

/* LCD string writer
 * Writes a char array (string) and writes it onto the LCD screen
 */
void LCD_write_string(char data[], int len) {
	int i;
	for (i = 0; i < len; i++){
		LCD_data(data[i]);
	}
}

/* LCD integer writer
 * Writes an integer onto the LCD display
 */
void LCD_write_int(int data) {
	char c;
	while (data > 0) {
		c = (char) (0x30 + (data % 10));
		data /= 10;
		LCD_data(c);
	}
}

/* LCD command display
 * Displays the latest command sent by the commander module ATPAPP on the LCD screen.
 */
void LCD_cmd(void){
	LCD_command(0x01); /* clear display */
	LCD_write_string("Command:", 8);
	LCD_write_string(incomingCommand, STANDARD_DATA_PACKET_SIZE);
	LCD_command(0xC0);
	/*LCD_write_string(desc);*/
}

/* UART0 data transmitter
 * Sends an echo signal to the commander ATPAPP via UART
 */
void txData(char data[]) {
    int i = 0;
    while (i < STANDARD_DATA_PACKET_SIZE) {
        while(!(UART0->S1 & 0x80)) {} /* wait for transmit buffer empty */
        UART0->D = data[i]; /* send a char */
        i++;
    }
}

void commandInterpret(void){
	
}