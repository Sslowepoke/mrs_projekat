#include <msp430.h>
#include <stdint.h>
#include <function.h>

/** macro to convert ASCII code to digit */
#define ASCII2DIGIT(x)      (x - '0')
/** macro to convert digit to ASCII code */
#define DIGIT2ASCII(x)      (x + '0')

/**
 * @brief Timer period for the 7seg display
 *
 * Timer is clocked by ACLK (32768Hz)
 * It takes 32768 cycles to count to 1s.
 * If we need a period of X ms, then number of cycles
 * that is written to the CCR0 register is
 * 32768/1000 * X
 */
#define TIMER_PERIOD_7SEG           (163)  /* ~5ms (4.97ms)  */

/**
 * @brief Timer period for debouncing buttons
 *
 * Timer is clocked by ACLK (32768Hz).
 * We want ~32ms period, so use 1023 for CCR0
 */
#define TIMER_PERIOD_DEBOUNCE       (1048)  /* ~32ms (31.25ms) */

// timer period for generating random numbers
// Timer is clocked by ACLK (32768Hz).
// We want ~200ms period, so use 6553 forCCR0
#define TIMER_PERIOD_RNG            (6553) /* ~200ms */

// broj brojeva koji se izvlace u lotou
#define NUMBERS_LENGTH              (7)

// baud rate
#define BR9600                      (3)

// array of digits used for display
static volatile uint8_t digits[2] = {0};

// which button was pressed?
enum buttons{NONE, START, STOP, RESET};

static volatile enum buttons button_pressed = NONE;

// array of pulled numbers
static volatile uint8_t numbers[NUMBERS_LENGTH] = {0};
// note - posto ovih brojeva ima 7, mogli smo verovatno
// da ih stavimo u registre da bi kod bio brzi
// ne znam da li kompajler to svakako radi

// index of the number we are currently pulling
static volatile uint8_t number_count = 0;

// current number
static volatile uint8_t number = 0;


/**
 * @brief Function that populates digit array
 */
static inline void display(const uint16_t number)
{
    uint16_t nr = number;
    uint8_t tmp;
    for (tmp = 0; tmp < 2; tmp++)
    {
        digits[tmp] = nr % 10;
        nr /= 10;
    }
}

int main(void)
{
 	WDTCTL = WDTPW | WDTHOLD;	// stop watchdog timer

	/*************************************************************
	* 7 Segment Display init
	*************************************************************/
    // sevenseg 1
    P7DIR |= BIT0;              // set P7.0 as out (SEL1)
    P7OUT |= BIT0;             // disable display 1
    // sevenseg 2
    P6DIR |= BIT4;              // set P6.4 as out (SEL2)
    P6OUT |= BIT4;              // disable display 2

    // a,b,c,d,e,f,g
    P2DIR |= BIT6 | BIT3;              // configure P2.3 and P2.6 as out
    P3DIR |= BIT7;                     // configure P3.7 as out
    P4DIR |= BIT3 | BIT0;              // configure P4.0 and P4.3 as out
    P8DIR |= BIT2 | BIT1;              // configure P8.1 and P8.2 as out

    // init TA0 as compare in up mode
    TA0CCR0 = TIMER_PERIOD_7SEG;     // set timer period in CCR0 register
    TA0CCTL0 = CCIE;            // enable interrupt for TA0CCR0
    TA0CTL = TASSEL__ACLK | MC__UP; //clock select and up mode

    /*************************************************************
	* buttons init
	*************************************************************/
    // configure button S2, S3 and S4 (RESET, STOP, START)
    P1REN |= BIT1 | BIT4 | BIT5;       // enable pull up/down
    P1OUT |= BIT1 | BIT4 | BIT5;       // set pull up
    P1DIR &= ~(BIT1 | BIT4 | BIT5);    // configure P1.4 as in
    P1IES |= BIT1 | BIT4 | BIT5;       // interrupt on falling edge
    P1IFG &= ~(BIT1 | BIT4 | BIT5);     // clear flag
    P1IE  |= BIT1 | BIT4 | BIT5;       // enable interrupt

    /* initialize Timer A1 */
    // timer for debouncing button signals
    TA1CCR0 = TIMER_PERIOD_DEBOUNCE;     // debounce period
    TA1CCTL0 = CCIE;            // enable CCR0 interrupt
    TA1CTL = TASSEL__ACLK;

    /*************************************************************
	* UART init
	*************************************************************/
    // configure UART
    P4SEL |= BIT4 | BIT5;       // configure P4.4 and P4.5 for uart

    UCA1CTL1 |= UCSWRST;        // enter sw reset

    UCA1CTL0 = 0;
    UCA1CTL1 |= UCSSEL__ACLK;   // select ACLK as clk source
    UCA1BRW = BR9600;           // same as UCA1BR0 = 3; UCA1BR1 = 0
    UCA1MCTL |= UCBRS_3 + UCBRF_0;         // configure 9600 bps

    UCA1CTL1 &= ~UCSWRST;       // leave sw reset

    /*************************************************************
    * CRC init
    *************************************************************/
	CRCINIRES |= 0xffff;       // initialize crc module
	CRCDI |= 0x0000;           // nisam siguran da ovo treba al ne kvari nista,
	                           // pomeri na pocetku za 16 nula CRC modul

	// init TA2, triggeruje rng
	TA2CCR0 = TIMER_PERIOD_RNG;        // set timer period in CCR0 register
	TA2CCTL0 = CCIE;                   // enable interrupt for TA2CCR0
	TA2CTL = TASSEL__ACLK | MC__UP;    // clock select and up mode

	// enable interrupts
	__enable_interrupt();

	while(1) {
	    //enter lpm
		__bis_SR_register(LPM3_bits);
	}
}


// Multiplex the 7seg display. Each ISR activates one digit.
void __attribute__ ((interrupt(TIMER0_A0_VECTOR))) TA0CCR0ISR (void)
{
    static uint8_t current_digit = 0;

    /* algorithm:
     * - turn off previous display (SEL signal)
     * - set a..g for current display
     * - activate current display
     */
    if (current_digit == 1)
    {
        P6OUT |= BIT4;          // turn off SEL2
        // trenutno je inline funkcija, mozda je treba izmestiti van isr
        WriteLed(digits[current_digit]);    // define seg a..g
        P7OUT &= ~BIT0;         // turn on SEL1
    }
    else if (current_digit == 0)
    {
        P7OUT |= BIT0;
        // trenutno je inline funkcija, mozda je treba izmestiti van isr
        WriteLed(digits[current_digit]);
        P6OUT &= ~BIT4;
    }
    current_digit = (current_digit + 1) & 0x01;

    return;
}

// isr za dugmice
void __attribute__ ((interrupt(PORT1_VECTOR))) P1ISR (void)
{
    if ((P1IFG & BIT4) != 0)        // check if P1.4 flag is set
    {
        button_pressed = STOP;
        TA1CTL |= MC__UP;           // start timer

        P1IFG &= ~BIT4;             // clear P1.4 flag
        P1IE &= ~(BIT4 | BIT5 | BIT1);  // disable interrupts for all buttons
        return;
    }

    if ((P1IFG & BIT5) != 0)        // check if P1.5 flag is set
    {
        button_pressed = START;
        TA1CTL |= MC__UP;           // start timer

        P1IFG &= ~BIT5;             // clear P1.5 flag
        P1IE &= ~(BIT4 | BIT5 | BIT1);  // disable interrupts for all buttons
        return;
    }

    if ((P1IFG & BIT1) != 0)        // check if P1.1 flag is set
    {
        button_pressed = RESET;
        TA1CTL |= MC__UP;           // start timer

        P1IFG &= ~BIT1;             // clear P1.1 flag
        P1IE &= ~(BIT4 | BIT5 | BIT1);  // disable interrupts for all buttons
        return;
    }
}

/**
 * @brief TIMERA1 Interrupt service routine
 *
 * debounce i obrada dugmica
 */
void __attribute__ ((interrupt(TIMER1_A0_VECTOR))) TA1CCR0ISR (void)
{
    // button STOP is still pressed
    if ((P1IN & BIT4) == 0 && button_pressed == STOP) // check if button is still pressed
    {
        TA2CTL &= ~(MC0 | MC1);         // stop and clear timer for rng
        TA2CTL |= TACLR;
        UCA1TXBUF = DIGIT2ASCII(number);  // send the current number
        numbers[number_count] = number;
        number_count += 1;
        button_pressed = NONE;
    }
    // button START is still pressed
    else if ((P1IN & BIT5) == 0 && button_pressed == START) // check if button is still pressed
    {
        // end of game
        if(number_count == 6)
        {
            // write En na LED
            digits[0] = 10;
            digits[1] = 11;

            UCA1TXBUF = '\n';       // newline na uart
        }
        else
        {
            TA2CTL |= MC__UP;       // start timer for rng in up mode
        }
        button_pressed = NONE;
    }
    // button RESET is still pressed
    else if ((P1IN & BIT1) == 0 && button_pressed == RESET) // check if button is still pressed
    {
        // clear numbers
        uint8_t i = 0;
        for(i = 0; i < NUMBERS_LENGTH; i++) {
            numbers[i] = 0;
        }
        number_count = 0;

        TA2CTL |= MC__UP;               // start timer for rng in up mode
        UCA1TXBUF = '\n';               // newline na uart

        button_pressed = NONE;
    }

    TA1CTL &= ~(MC0 | MC1);             // stop and clear debounce timer
    TA1CTL |= TACLR;
    P1IFG &= ~(BIT4 | BIT5 | BIT1);     // clear all button interrupt flags
    P1IE |= (BIT4 | BIT5 | BIT1);       // enable all button interrupts
}

// RNG
void __attribute__ ((interrupt(TIMER2_A0_VECTOR))) TA2CCR0ISR (void) {
    uint8_t repeated_number = 1;
    while(repeated_number) {        // sve dok ne dobijemo broj koji nije vec bio, generisemo novi broj
        repeated_number = 0;

        // CRCDI = 0x0000;              //stavimo sve nule u input za crc, ovo ce da generise slucajan broj
        __asm__("MOV.B  #0h, CRCDI");   //stavimo byte nula

        number = CRCINIRES & 0x001f;    // poslednjih 5 bita rezulata je nas broj

        // proveravamo da li smo vec izvukli ovaj broj
        uint8_t i = 0;
        for(i = 0; i < number_count; i++) {
            if(numbers[i] == number) {
                repeated_number = 1;
            }
        }
    }
    display(number);
}
