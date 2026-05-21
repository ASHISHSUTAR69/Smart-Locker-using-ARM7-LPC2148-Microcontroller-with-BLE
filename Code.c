#include <lpc214x.h>
#include <string.h>

// LCD Pin Definitions
#define RS (1 << 2)
#define E  (1 << 3)
#define D4 (1 << 4)
#define D5 (1 << 5)
#define D6 (1 << 6)
#define D7 (1 << 7)

// Keypad Pins (Port 1)
#define ROW1 (1 << 20)
#define ROW2 (1 << 21)
#define ROW3 (1 << 22)
#define ROW4 (1 << 23)

#define COL1 (1 << 16)
#define COL2 (1 << 17)
#define COL3 (1 << 18)
#define COL4 (1 << 19)

// Globals
volatile char received = 0;
volatile int command_flag = 0;
char password[5] = "1234";
char keypad[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// ---------- Delay ----------
void delay(unsigned int d) {
  unsigned int i;
  for (i = 0; i < d * 6000; i++);
}

// ---------- UART1 Interrupt ----------
void uart1_isr(void) __irq {
  unsigned int iir = U1IIR;  // Read interrupt identification register
 
  if ((iir & 0x0E) == 0x04) {  // RDA interrupt (Receive Data Available)
    received = U1RBR;
    command_flag = 1;
  }
 
  VICVectAddr = 0;  // Clear interrupt
}

void uart1_init(void) {
  // Configure pins for UART1
  PINSEL0 |= (1 << 16) | (1 << 18);  // P0.8 = TXD1, P0.9 = RXD1
  PINSEL0 &= ~((1 << 17) | (1 << 19)); // Clear other bits
 
  // Configure UART1
  U1LCR = 0x83;    // 8-bit, 1 stop bit, no parity, enable divisor latch
  U1DLL = 97;      // 9600 baud at 15MHz (assuming PCLK = 15MHz)
  U1DLM = 0;
  U1LCR = 0x03;    // Disable divisor latch access
  U1FCR = 0x07;    // Enable FIFO, clear RX and TX FIFOs
  U1IER = 0x01;    // Enable RDA interrupt
 
  // Configure VIC for UART1
  VICVectAddr1 = (unsigned int)uart1_isr;
  VICVectCntl1 = 0x20 | 7;  // Enable vector slot 1 for UART1 (IRQ 7)
  VICIntEnable |= (1 << 7); // Enable UART1 interrupt
}

// ---------- UART1 Send Functions ----------
void uart1_send_char(char c) {
  while (!(U1LSR & 0x20));  // Wait until THR is empty
  U1THR = c;
}

void uart1_send_string(char *str) {
  while (*str) {
    uart1_send_char(*str++);
  }
}

// ---------- LCD ----------
void lcd_cmd(int cmd) {
  // Send upper nibble
  IOCLR0 = RS | D4 | D5 | D6 | D7;
  IOSET0 = ((cmd >> 4) & 0x0F) << 4;
  IOCLR0 = RS;  // Command mode
  IOSET0 = E;
  delay(1);
  IOCLR0 = E;

  // Send lower nibble
  IOCLR0 = D4 | D5 | D6 | D7;
  IOSET0 = (cmd & 0x0F) << 4;
  IOCLR0 = RS;  // Command mode
  IOSET0 = E;
  delay(1);
  IOCLR0 = E;
  delay(2);
}

void lcd_data(char data) {
  // Send upper nibble
  IOCLR0 = D4 | D5 | D6 | D7;
  IOSET0 = ((data >> 4) & 0x0F) << 4;
  IOSET0 = RS;  // Data mode
  IOSET0 = E;
  delay(1);
  IOCLR0 = E;

  // Send lower nibble
  IOCLR0 = D4 | D5 | D6 | D7;
  IOSET0 = (data & 0x0F) << 4;
  IOSET0 = RS;  // Data mode
  IOSET0 = E;
  delay(1);
  IOCLR0 = E;
  delay(2);
}

void lcd_string(char *str) {
  while (*str) {
    lcd_data(*str++);
  }
}

void lcd_init(void) {
  // Set LCD pins as output
  IODIR0 |= RS | E | D4 | D5 | D6 | D7;
 
  delay(50);  // Wait for LCD to power up
 
  // Initialize LCD in 4-bit mode
  lcd_cmd(0x33);  // Initialize
  lcd_cmd(0x32);  // Set to 4-bit mode
  lcd_cmd(0x28);  // 4-bit mode, 2 lines, 5x8 font
  lcd_cmd(0x0C);  // Display on, cursor off
  lcd_cmd(0x06);  // Entry mode: increment cursor
  lcd_cmd(0x01);  // Clear display
  delay(2);
}

// ---------- Keypad ----------
char get_key(void) {
  int row;
 
  // Set rows as output, columns as input
  IO1DIR |= ROW1 | ROW2 | ROW3 | ROW4;
  IO1DIR &= ~(COL1 | COL2 | COL3 | COL4);
 
  // Enable pull-up resistors for columns
  IO1SET = COL1 | COL2 | COL3 | COL4;

  while (1) {
    for (row = 0; row < 4; row++) {
      // Set all rows high
      IO1SET = ROW1 | ROW2 | ROW3 | ROW4;
      // Set current row low
      IO1CLR = (1 << (20 + row));
      delay(2);

      // Check each column
      if (!(IO1PIN & COL1)) {
        while (!(IO1PIN & COL1)); // Wait for key release
        delay(10); // Debounce
        return keypad[row][0];
      }
      if (!(IO1PIN & COL2)) {
        while (!(IO1PIN & COL2));
        delay(10);
        return keypad[row][1];
      }
      if (!(IO1PIN & COL3)) {
        while (!(IO1PIN & COL3));
        delay(10);
        return keypad[row][2];
      }
      if (!(IO1PIN & COL4)) {
        while (!(IO1PIN & COL4));
        delay(10);
        return keypad[row][3];
      }
    }
  }
}

// ---------- Character Check ----------
int is_alphabet(char c) {
  return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

int is_number(char c) {
  return (c >= '0' && c <= '9');
}

// ---------- Main ----------
int main(void) {
  char entered[5];
  int i;
  int timeout;
 
  // Initialize system
  lcd_init();
  uart1_init();
 
  // Send startup message
  uart1_send_string("Security System Ready\r\n");
 
  while (1) {
    // Clear LCD and show password prompt
    lcd_cmd(0x01);
    lcd_string("Enter Password:");
    lcd_cmd(0xC0);  // Move to second line

    // Get 4-digit password
    for (i = 0; i < 4; i++) {
      entered[i] = get_key();
      lcd_data('*');  // Show asterisk for security
    }
    entered[4] = '\0';  // Null terminate

    // Check password
    if (strcmp(entered, password) == 0) {
      lcd_cmd(0x01);
      lcd_string("Wait for admin");
      lcd_cmd(0xC0);
      lcd_string("access...");
     
      // Send notification to admin
      uart1_send_string("Password correct. Waiting for admin command...\r\n");
     
      delay(2000);
      command_flag = 0;
      received = 0;

      // Wait for Bluetooth/UART command with timeout
      timeout = 0;
      while (!command_flag && timeout < 10000) {
        delay(1);
        timeout++;
      }

      if (command_flag) {
        command_flag = 0;
        lcd_cmd(0x01);
       
        if (is_alphabet(received)) {
          lcd_string("Access Granted");
          uart1_send_string("Access GRANTED - Alpha command received\r\n");
        } else if (is_number(received)) {
          lcd_string("Access Denied");
          uart1_send_string("Access DENIED - Numeric command received\r\n");
        } else {
          lcd_string("Access DENIED");
          uart1_send_string("Invalid command received\r\n");
        }
        delay(3000);
      } else {
        lcd_cmd(0x01);
        lcd_string("Timeout");
        uart1_send_string("Admin timeout - no command received\r\n");
        delay(2000);
      }
    } else {
      lcd_cmd(0x01);
      lcd_string("Wrong Password");
      lcd_cmd(0xC0);
      lcd_string("Access Denied");
     
      // Send breach notification
      uart1_send_string("SECURITY BREACH DETECTED!\r\n");
      uart1_send_string("Wrong password entered: ");
      uart1_send_string(entered);
      uart1_send_string("\r\n");
     
      delay(3000);
    }

    delay(1000);  // Brief pause before next cycle
  }
}
