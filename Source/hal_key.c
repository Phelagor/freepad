/**************************************************************************************************
 *                                            INCLUDES
 **************************************************************************************************/
#include "hal_key.h"
#include "Debug.h"
#include "hal_adc.h"
#include "hal_defs.h"
#include "hal_drivers.h"
#include "hal_led.h"
#include "hal_mcu.h"
#include "hal_types.h"
#include "osal.h"
#include <stdio.h>

/**************************************************************************************************
 *                                              MACROS
 **************************************************************************************************/

/**************************************************************************************************
 *                                            CONSTANTS
 **************************************************************************************************/

#define HAL_KEY_NONE 0x00
#define HAL_KEY_BIT0 0x01
#define HAL_KEY_BIT1 0x02
#define HAL_KEY_BIT2 0x04
#define HAL_KEY_BIT3 0x08
#define HAL_KEY_BIT4 0x10
#define HAL_KEY_BIT5 0x20
#define HAL_KEY_BIT6 0x40
#define HAL_KEY_BIT7 0x80

#define HAL_KEY_RISING_EDGE 0x00
#define HAL_KEY_FALLING_EDGE 0x01

//P0.0 ... P0.7
#define HAL_KEY_P0_INTERRUPT_CONFIG 0x00
//P1.0 ... P1.3
#define HAL_KEY_P1L_INTERRUPT_CONFIG 0x01
//P1.4 ... P1.7
#define HAL_KEY_P1H_INTERRUPT_CONFIG 0x02
//P2.0 ... P0.4
#define HAL_KEY_P2_INTERRUPT_CONFIG 0x03

// IEN1 bit 5
#define HAL_KEY_P0_INTERRUPT_ENABLE_BIT 0x05
// IEN2 bit 4
#define HAL_KEY_P1_INTERRUPT_ENABLE_BIT 0x04
// IEN2 bit 1
#define HAL_KEY_P2_INTERRUPT_ENABLE_BIT 0x01

// Set input mode: Pull or 3-state
#define HAL_KEY_PULL_MODE(MODE_REG, PINS) ((MODE_REG)&(~(GPIO_PINS)))
#define HAL_KEY_3_STATE_MODE(MODE_REG, INPUT_PINS) ((MODE_REG)|(GPIO_PINS))

// Set Pull-up or Pull-down selection at P2INP
#define HAL_KEY_P0_PULL_SELECTION_BIT 0x05
#define HAL_KEY_P1_PULL_SELECTION_BIT 0x06
#define HAL_KEY_P2_PULL_SELECTION_BIT 0x07

#define HAL_KEY_PULL_UP 0x00
#define HAL_KEY_PULL_DOWN 0x1

#define HAL_KEY_DEBOUNCE_VALUE 25 // TODO: adjust this value

#if defined(HAL_BOARD_FREEPAD)
  #define HAL_KEY_P0_GPIO_PINS (HAL_KEY_BIT0 | HAL_KEY_BIT1 | HAL_KEY_BIT2 | HAL_KEY_BIT3 | HAL_KEY_BIT4 | HAL_KEY_BIT5)
  #define HAL_KEY_P0_OUTPUT_PINS (HAL_KEY_P0_GPIO_PINS)
  #define HAL_KEY_P0_INPUT_PINS (~(HAL_KEY_P1_OUTPUT_PINS))
  #define HAL_KEY_P0_INTERRUPT_PINS (HAL_KEY_P0_GPIO_PINS)

  #define HAL_KEY_P1_GPIO_PINS (HAL_KEY_BIT1 | HAL_KEY_BIT2)
  #define HAL_KEY_P1_OUTPUT_PINS (HAL_KEY_P1_GPIO_PINS)
  #define HAL_KEY_P1_INPUT_PINS (~(HAL_KEY_P1_OUTPUT_PINS))
  #define HAL_KEY_P1_INTERRUPT_PINS (HAL_KEY_NONE)

  #define HAL_KEY_P2_GPIO_PINS (HAL_KEY_NONE)
  #define HAL_KEY_P2_OUTPUT_PINS (HAL_KEY_P2_GPIO_PINS)
  #define HAL_KEY_P2_INPUT_PINS (HAL_KEY_P2_GPIO_PINS)
  #define HAL_KEY_P2_INTERRUPT_PINS (HAL_KEY_NONE)

#elif defined(HAL_BOARD_CHDTECH_DEV)
  #define HAL_KEY_P0_GPIO_PINS (HAL_KEY_BIT1)
  #define HAL_KEY_P1_GPIO_PINS (HAL_KEY_NONE)
  #define HAL_KEY_P2_GPIO_PINS (HAL_KEY_BIT0)

  #define HAL_KEY_P0_INPUT_PINS (HAL_KEY_BIT1)
  #define HAL_KEY_P1_INPUT_PINS (HAL_KEY_NONE)
  #define HAL_KEY_P2_INPUT_PINS (HAL_KEY_BIT0)

  #define HAL_KEY_P0_INTERRUPT_PINS (HAL_KEY_BIT1)
  #define HAL_KEY_P1_INTERRUPT_PINS (HAL_KEY_NONE)
  #define HAL_KEY_P2_INTERRUPT_PINS (HAL_KEY_BIT0)
#endif


/**************************************************************************************************
 *                                            TYPEDEFS
 **************************************************************************************************/

/**************************************************************************************************
 *                                        GLOBAL VARIABLES
 **************************************************************************************************/
static halKeyCBack_t pHalKeyProcessFunction;
bool Hal_KeyIntEnable;

uint8 halSaveIntKey; /* used by ISR to save state of interrupt-driven keys */

static uint8 halKeyTimerRunning; // Set to true while polling timer is running in interrupt
                                 // enabled mode

/**************************************************************************************************
 *                                        FUNCTIONS - Local
 **************************************************************************************************/
extern void halProcessKeyInterrupt(void);


/*
 * Initialize Columns
 */
#define HAL_KEY_INIT_COLUMNS()  \
{\
  /* P1 is output */\
  \
    /* clear all bits */\
    P1DIR &= 0x00;      /* I/O direction: in P1 */\
    /* set only the bit we need */\
    P1DIR |= 0x06;      /* I/O direction: out P1.1 ... P1.2*/\
    P1INP |= 0xFF;      /* Input Mode: 3-State P1 */\
    P1 |= 0x06;         /* Set output to high */\
}

/*
 * Initialize Rows
 */
#define HAL_KEY_INIT_ROWS() \
{\
  /* P0 is input */\
  \
    P0DIR &= 0x00;       /* I/O direction: in P0 */ \
    P0INP &= 0x00;       /* Input Mode: Pull P0 */\
}

extern void HalKeyInit(void) {
    // Mode selection (GPIO:0 or Peripheral:1)
    P0SEL &= 0x00;
    P1SEL &= 0x00;
    P2SEL &= 0x00;
    
    HAL_KEY_INIT_COLUMNS();
    HAL_KEY_INIT_ROWS();
    
    HAL_BOARD_DELAY_USEC(50); 
    pHalKeyProcessFunction = NULL;
    halKeyTimerRunning = FALSE;
}

void HalKeyConfig(bool interruptEnable, halKeyCBack_t cback) {
    Hal_KeyIntEnable = true;
    pHalKeyProcessFunction = cback;
    
    P0IEN &= 0x00;      /* reset bits */
    P0IEN |= 0x3F;       /* Interrupt Mask: P0.0 ... P0.5 */
    P1IEN &= 0x00;      /* Interrupt Mask: none P1 */
    P2IEN &= 0x00;      /* Interrupt Mask: none P2 */
    
    P2INP &= 0x1F;       /* delete upper 3 bits */
    P2INP |= 0x20;      /* Pullup P1 - P2, Pulldown P0 */
    APCFG &= 0x00;
    IEN1 |= 0x20;       /* Interrupt Enable P0 */
    PICTL |= 0x01;      /* Interrupt Flank: Rising P0 */ 
}

uint8 HalKeyRead(void) {

    uint8 key = HAL_KEY_CODE_NOKEY;
#if defined(HAL_BOARD_FREEPAD)
    uint8 row, col;    
    row = (P0 & 0x3F); //compare with interupt pins to see if one occured

    if (row) {
        // We got an interrupt from a col pin
        // Now find out which col it was by changing col IO's to input 
        // and driving the row IO's as Outpt.
        col = 0;
        
        // reconfigure cols as output
        P0DIR |= 0x3F;       /* I/O direction: out P0 */
        P0INP |= 0x3F;       /* Input Mode: 3-State P0 */\
        P0 &= 0x00;

    
        // reconfigure row as input
        P1DIR &= 0x00;       /* I/O direction: in P1 */ 
        P1INP &= 0x00;       /* Input Mode: Pull P1 */

        

        HAL_BOARD_DELAY_USEC(50);

        col = ((~P1) & 0x06); /* Compare with input pins */

        // Reset Ports for new Key Press
        HAL_KEY_INIT_COLUMNS();
        HAL_KEY_INIT_ROWS();
        
        // high nibble = row, low nibble = col
        // generate keymap with included javascript (keymap.js)
        
        // Rows
        uint8 result = (row > 0xF) << 2; row >>= result;    
        uint8 shift = (row > 0x3 ) << 1;
        uint8 key = (result|shift|(row >> (shift+1)))<<4;
        
        // Columns
        result = (col > 0xF) << 2; col >>= result;
        shift = (col > 0x3 ) << 1;
        key |= result|shift|(row >> (shift+1));
        
        return key;              
    }

#elif defined(HAL_BOARD_CHDTECH_DEV)

    if (ACTIVE_LOW(P0 & HAL_KEY_P0_INPUT_PINS)) {
        key = 0x01;
    }

    if (ACTIVE_LOW(P2 & HAL_KEY_P2_INPUT_PINS)) {
        key = 0x02;
    }
#endif

    return key;
}
void HalKeyPoll(void) {
    uint8 keys = 0;

    keys = HalKeyRead();

    if (pHalKeyProcessFunction) {
        (pHalKeyProcessFunction)(keys, HAL_KEY_STATE_NORMAL);
    }

    if (keys != HAL_KEY_CODE_NOKEY) {
        osal_start_timerEx(Hal_TaskID, HAL_KEY_EVENT, 200);
    } else {
        halKeyTimerRunning = FALSE;
    }
}
void halProcessKeyInterrupt(void) {
    if (!halKeyTimerRunning) {
        halKeyTimerRunning = TRUE;
        osal_start_timerEx(Hal_TaskID, HAL_KEY_EVENT, HAL_KEY_DEBOUNCE_VALUE);
    }
}

void HalKeyEnterSleep(void) {
    uint8 clkcmd = CLKCONCMD;
    uint8 clksta = CLKCONSTA;
    // Switch to 16MHz before setting the DC/DC to bypass to reduce risk of flash corruption
    CLKCONCMD = (CLKCONCMD_16MHZ | OSC_32KHZ);
    // wait till clock speed stablizes
    while (CLKCONSTA != (CLKCONCMD_16MHZ | OSC_32KHZ))
        ;

    CLKCONCMD = clkcmd;
    while (CLKCONSTA != (clksta))
        ;
}

uint8 HalKeyExitSleep(void) {
    uint8 clkcmd = CLKCONCMD;
    // Switch to 16MHz before setting the DC/DC to on to reduce risk of flash corruption
    CLKCONCMD = (CLKCONCMD_16MHZ | OSC_32KHZ);
    // wait till clock speed stablizes
    while (CLKCONSTA != (CLKCONCMD_16MHZ | OSC_32KHZ))
        ;

    CLKCONCMD = clkcmd;

    /* Wake up and read keys */
    return (HalKeyRead());
}

HAL_ISR_FUNCTION(halKeyPort0Isr, P0INT_VECTOR) {
    HAL_ENTER_ISR();

    if (P0IFG & HAL_KEY_P0_INTERRUPT_PINS) {
        halProcessKeyInterrupt();
    }

    P0IFG &= ~HAL_KEY_P0_INTERRUPT_PINS;
    P0IF = 0;

    CLEAR_SLEEP_MODE();
    HAL_EXIT_ISR();
}

HAL_ISR_FUNCTION(halKeyPort1Isr, P1INT_VECTOR) {
    HAL_ENTER_ISR();

    if (P1IFG & HAL_KEY_P1_INTERRUPT_PINS) {
        halProcessKeyInterrupt();
    }

    P1IFG &= ~HAL_KEY_P1_INTERRUPT_PINS;
    P1IF = 0;

    CLEAR_SLEEP_MODE();
    HAL_EXIT_ISR();
}

HAL_ISR_FUNCTION(halKeyPort2Isr, P2INT_VECTOR) {
    HAL_ENTER_ISR();

    if (P2IFG & HAL_KEY_P2_INTERRUPT_PINS) {
        halProcessKeyInterrupt();
    }

    P2IFG &= ~HAL_KEY_P2_INTERRUPT_PINS;
    P2IF = 0;

    CLEAR_SLEEP_MODE();
    HAL_EXIT_ISR();
}