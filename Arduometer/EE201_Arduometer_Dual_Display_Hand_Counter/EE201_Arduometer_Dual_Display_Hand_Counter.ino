// EE-201 Arduometer Checkout:  Dual Display Hand Counter
// Revision R1:  11/2022
// This code deploys to the EE-201 custom Arduometer PCB
// The target MCU is an Microchip/Atmel ATmega328P, configured to behave like an Arduino Uno
// The Arduometer PCB is designed to accept a Sparkfun FTDI Basic USB-UART bridge to be programmed directly through the Arduino IDE
// Use a USB A to mini-B cable to connect the FTDI Basic to a PC running the Arduino IDE
// Select Board = 'Arduino Uno' if using the Arduino IDE, and use the Arduino bootloader to program through UART Tx/Rx lines
// More adventurous users can also program the Arduometer using the ICSP port and Atmel Studio which will also provide debug capability
// The ICSP is also used to burn the Arduino bootloader into the ATmega328P, which is blank when installed
// Clock is set up for 16.000 MHz using on-board crystal
// The code controls the SPI bus to the MAX7219 8-digit 7-segment LED driver

// The Dual Display Hand Counter implements a simple device that can be used to count events that are being watched.  
// An example is counting people who enter an event where one wants to keep track of the number of adults and children.  
// PB0 increments the upper display and PB1 increments the lower display
// Pressing both PB0 and PB1 together for more than 1 second clears both displays
// This is a simple means to check out the display, pushbuttons, timer0, and interrupt service routine (ISR)

// MCU pin assignments
#define RX 0 // UART RX on digital pin 0
#define TX 1 // UART TX on digital pin 1
#define ISRMKR 2 // ISR timing marker on digital pin 2 for diagnostics
#define PB0 8 // pushbutton PB0 on digital pin 8, normally pulled HI
#define PB1 9 // pushbutton PB1 on digital pin 9, normally pulled HI
#define CS 10 // chip select
#define MOSI 11 // SPI bus MOSI
#define MISO 12 // SPI bus MISO
#define SCLK 13 // SPI bus SCLK

// globals
char pbtns = 0; // pushbutton read variable
unsigned int counterPB0 = 0; // millisecond duration counter for pushbutton PB0 pushed alone
unsigned int counterPB1 = 0; // millisecond duration counter for pushbutton PB1 pushed alone
unsigned int counterPB01 = 0; // millisecond duration counter for both pushbuttons held together
// Code B font used in MAX7219:  
// bit 7 = decimal point
// bits 6..4 = X, don't care
// bits 3..0 = 0,1,2,3,4,5,6,7,8,9,-,E,H,L,P,blank
// display digits:  
char D0 = 0x0F; // digit 0, MSD of upper display, initialize to 0xF = blank
char D1 = 0x0F; // digit 1
char D2 = 0x0F; // digit 2
char D3 = 0x0F; // digit 3, LSD of upper display
char D4 = 0x0F; // digit 4, MSD of lower display
char D5 = 0x0F; // digit 5
char D6 = 0x0F; // digit 6
char D7 = 0x0F; // digit 7, LSD of lower display
// display is arranged like this:  D0 D1 D2 D3 <-- upper display
//                                 D4 D5 D6 D7 <-- lower display

// SPI transfer function
char spi_transfer(volatile char data)
{
  SPDR = data; // load data and start transmission
  while(!(SPSR & (1<<SPIF))) // wait for end of transmission
  {
    // do nothing; SCLK should run at 4 MHz, transmit of one byte should take 2 us.  
  }
  return SPDR; // return the received byte - will be all zeroes for the MAX7219 which does not have a MISO line
}

// 16b load function for MAX7219 display driver
// technically not SPI, but SPI protocol will work with MAX7219
// CS does not need to be LOW to clock data into MAX7219, but do it anyway to frame the data
void display_load(char dataH, char dataL) 
{
  digitalWrite(CS, LOW); // pull CS LOW to start clocking in data
  spi_transfer(dataH); // clock in high byte for address
  spi_transfer(dataL); // clock in low byte for data
  digitalWrite(CS, HIGH); // bring CS HIGH to load data
}

// update upper display with current digit values
void update_upper_display()
{
  display_load(0x01,D0); // load digit 0
  display_load(0x02,D1); // load digit 1
  display_load(0x03,D2); // load digit 2
  display_load(0x04,D3); // load digit 3
}

// update lower display with current digit values
void update_lower_display()
{
  display_load(0x05,D4); // load digit 4
  display_load(0x06,D5); // load digit 5
  display_load(0x07,D6); // load digit 6
  display_load(0x08,D7); // load digit 7
}

// initialization function for MAX7219 display driver
void display_init() 
{
  display_load(0x09, 0xFF); // decode mode register: code B decode for digits 0-7
  display_load(0x0A, 0x0F); // intensity register: 31/32 duty cycle
  display_load(0x0B, 0x07); // scan limit register: scan digits 0-7
  display_load(0x0C, 0x01); // shutdown mode register: normal operation
  display_load(0x0F, 0x00); // display test register: normal operation
  D0 = 0x0F; // blank
  D1 = 0x0F; // blank
  D2 = 0x0F; // blank
  D3 = 0x00; // zero
  update_upper_display();
  D4 = 0x0F; // blank
  D5 = 0x0F; // blank
  D6 = 0x0F; // blank
  D7 = 0x00; // zero
  update_lower_display();
}

// pushbutton PB0 action:  increment upper display count
// this assumes that D3 starts at 0 with D0, D1, and D2 blanked
void actionPB0()
{
  D3++; 
  if(D3 > 0x09)
  {
    D3 = 0x00;
    if(D2 == 0x0F) D2 = 0x00;
    D2++;
    if(D2 > 0x09)
    {
      D2 = 0x00;
      if(D1 == 0x0F) D1 = 0x00;
      D1++;
      if(D1 > 0x09)
      {
        D1 = 0x00;
        if(D0 == 0x0F) D0 = 0x00;
        D0++;
        if(D0 > 0x09)
        {
          D3 = 0x0A;
          D2 = 0x0A;
          D1 = 0x0A;
          D0 = 0x0A; // show ---- to indicate overflow > 9999
        }
      }
    }
  }
  update_upper_display();
}

// pushbutton PB1 action:  increment lower display count
// this assumes that D7 starts at 0 with D4, D5, and D6 blanked
void actionPB1()
{
  D7++; 
  if(D7 > 0x09)
  {
    D7 = 0x00;
    if(D6 == 0x0F) D6 = 0x00;
    D6++;
    if(D6 > 0x09)
    {
      D6 = 0x00;
      if(D5 == 0x0F) D5 = 0x00;
      D5++;
      if(D5 > 0x09)
      {
        D5 = 0x00;
        if(D4 == 0x0F) D4 = 0x00;
        D4++;
        if(D4 > 0x09)
        {
          D7 = 0x0A;
          D6 = 0x0A;
          D5 = 0x0A;
          D4 = 0x0A; // show ---- to indicate overflow > 9999
        }
      }
    }
  }
  update_lower_display();
}

// pushbutton PB0 and PB1 together action
// reset both upper and lower display counts
void actionPB01()
{
  D0 = 0x0F;
  D1 = 0x0F;
  D2 = 0x0F;
  D3 = 0x00;
  update_upper_display();
  D4 = 0x0F;
  D5 = 0x0F;
  D6 = 0x0F;
  D7 = 0x00;
  update_lower_display();
}

// required one-time function for Arduino IDE
void setup() 
{
  cli(); // stop interrupts so that setup does not hang up
  // setup pushbuttons
  pinMode(PB0, INPUT); // sets the digital pin as input
  pinMode(PB1, INPUT); // sets the digital pin as input
  // setup ISR timing marker
  pinMode(ISRMKR, OUTPUT); // sets the digital pin as output
  digitalWrite(ISRMKR, LOW); // start with ISRMRKR = LO
  // setup SPI bus
  pinMode(CS, OUTPUT); // sets the digital pin to output
  pinMode(MOSI, OUTPUT); // sets the digitl pin to output
  pinMode(MISO, INPUT); // sets the digital pin to input
  pinMode(SCLK, OUTPUT); // sets the digital pin to output
  digitalWrite(CS, HIGH); // start with CS = HI to disable SPI transactions
  digitalWrite(MOSI, LOW); // start with MOSI = LO
  digitalWrite(SCLK, LOW); // start with SCLK = LO for its idle state
  SPCR = (1<<SPE) | (1<<MSTR); // setup SPI control register:  enable SPI, msb first, master mode, CPOL=0, CPHA=0, SCLK = fosc/4
  // setup timer0 to trigger interrupt at 1 kHz rate (1 ms period)
  TCCR0A = 0; // clear timer0 control register A
  TCCR0B = 0; // clear timer0 control register B
  TCNT0 = 0; // initialize counter to zero
  OCR0A = 249; // set outpout compare match to (16 MHz)/(1 kHz * 64) - 1 = 249
  TCCR0A |= (1<<WGM01); // turn on CTC mode
  TCCR0B |= (1<<CS01) | (1<<CS00); // use prescaler /64 output
  TIMSK0 |= (1<<OCIE0A); // enable timer0 compare A interrupt, OCF0A flag set on interrupt, cleared by HW when ISR executes
  sei(); // allow interrupts
  display_init(); // initialize the display, put a 0 in digits 3 (upper) and 7 (lower)
}

// ISR for timer0 compare A interrupt
ISR(TIMER0_COMPA_vect)
{
  digitalWrite(ISRMKR, HIGH); // set ISR timing marker HI (oscilloscope diagnostic pin)
  // pushbutton service - provides debounce and hold down times
  pbtns = PINB & 0x3; // read pushbuttons PB0 and PB1, which also happen to be bits 0 and 1 of PORTB
  switch(pbtns)
  {
    case 0: // both pushbuttons depressed
    counterPB01++;
    break;
    case 1: // only PB1 depressed
    counterPB1++;
    break;
    case 2: // only PB0 depressed
    counterPB0++;
    break;
    case 3: // neither pushbutton depressed
    if(counterPB0 > 50) // PB0 held down for more than 0.05 sec for debounce
    {
      actionPB0(); // action for PB0
    }
    if(counterPB1 > 50) // PB1 held down for more than 0.05 sec for debounce
    {
      actionPB1(); // action for PB1
    }
    if(counterPB01 > 1000) // both PB0 and PB1 held down together for more than 1.0 sec
    {
      actionPB01(); // action for both PB0 and PB1 together
    }
    counterPB0 = 0; // clear all of the pushbutton duration counters
    counterPB1 = 0;
    counterPB01 = 0;
    break;
  }
  digitalWrite(ISRMKR, LOW); // set ISR timing marker LO
}

// required loop function for Arduino IDE
void loop() 
{
  //display_init(); // initialize the display for testing
  delayMicroseconds(1000); // brief delay
}
