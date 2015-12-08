#ifdef __USE_CMSIS
#include "LPC8xx.h"
#include "lpc8xx_gpio.h"
#include "lpc8xx_romapi.h"
#endif
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"

#include <cr_section_macros.h>

static inline void SwitchMatrix_Init()
{	/* Generated by the Switch Matric Tool */

	/* Enable SWM clock (already enabled in system_LPC8xx.c */
	//LPC_SYSCON->SYSAHBCLKCTRL |= (1<<7);

	/* Pin Assign 8 bit Configuration */
	/* U0_TXD */
	/* U0_RXD */
	LPC_SWM->PINASSIGN0 = 0xffff0004UL;
	/* I2C0_SDA */
	LPC_SWM->PINASSIGN7 = 0x0affffffUL;
	/* I2C0_SCL */
	LPC_SWM->PINASSIGN8 = 0xffffff0bUL;

	/* Pin Assign 1 bit Configuration */
	/* SWCLK */
	/* SWDIO */
	/* XTALIN */
	/* XTALOUT */
	/* RESET */
	LPC_SWM->PINENABLE0 = 0xffffff83UL;

	/* Disable the clock to the Switch Matrix to save power */
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1<<7);
}

static inline void IOCON_Init()
{	/* Generated by the Switch Matric Tool */

	/* Enable IOCON clock (already enabled in system_LPC8xx.c) */
	//LPC_SYSCON->SYSAHBCLKCTRL |= (1<<18);

	/* Pin I/O Configuration */
	/* LPC_IOCON->PIO0_0 = 0x90; */
	/* LPC_IOCON->PIO0_1 = 0x90; */
	/* LPC_IOCON->PIO0_2 = 0x90; */
	/* LPC_IOCON->PIO0_3 = 0x90; */
	/* LPC_IOCON->PIO0_4 = 0x90; */
	/* LPC_IOCON->PIO0_5 = 0x90; */
	/* LPC_IOCON->PIO0_6 = 0x90; */
	/* LPC_IOCON->PIO0_7 = 0x90; */
	// Enable XTALIN and XTALOUT on PIO0_8 and PIO0_9 & remove the pull-up/down resistors
	LPC_IOCON->PIO0_8 = 0x80;
	LPC_IOCON->PIO0_9 = 0x80;
	/* LPC_IOCON->PIO0_10 = 0x80; */
	/* LPC_IOCON->PIO0_11 = 0x80; */
	/* LPC_IOCON->PIO0_12 = 0x90; */
	/* LPC_IOCON->PIO0_13 = 0x90; */
	/* LPC_IOCON->PIO0_14 = 0x90; */
	/* LPC_IOCON->PIO0_15 = 0x90; */
	/* LPC_IOCON->PIO0_16 = 0x90; */
	/* LPC_IOCON->PIO0_17 = 0x90; */

	/* Disable the clock to the IOCON to save power */
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1<<18);
}

static inline void Clock_Setup()
{	/* Reconfigure 30MHz system clock derived from System oscillator */
	/* Set up PLL: */
	//   Power up the crystal oscillator & system PLL in the PDRUNCFG register
	LPC_SYSCON->PDRUNCFG &= ~((1 << 5) | (1 << 7));
	//   Select the PLL input in the SYSPLLCLKSEL register
	LPC_SYSCON->SYSPLLCLKSEL = 1;	/* SYSOSC */
	//   Update the PLL clock source in the SYSPLLCLKUEN register
	LPC_SYSCON->SYSPLLCLKUEN = 0;
	LPC_SYSCON->SYSPLLCLKUEN = 1;
	//   Configure the PLL M and N dividers
	LPC_SYSCON->SYSPLLCTRL = (4 | (1 << 5));
	//   Wait for the PLL to lock by monitoring the PLL lock status
	while (!(LPC_SYSCON->SYSPLLSTAT & 1));

	/* Configure the main clock and system clock: */
	//   Select the main clock
	LPC_SYSCON->MAINCLKSEL = 3;		/* PLL output */
	//   Update the main clock source
	LPC_SYSCON->MAINCLKUEN = 0;
	LPC_SYSCON->MAINCLKUEN = 1;
	//   Select the divider value for the system clock to core, memories, and peripherals
	LPC_SYSCON->SYSAHBCLKDIV = 2;

	// Disable the BYPASS bit and select the oscillator frequency range in SYSOSCCTRL register
	LPC_SYSCON->SYSOSCCTRL = 0;
}

#define GPIO_LED_BLUE		7
#define GPIO_LED_GREEN		16
#define GPIO_LED_RED		17

static void vLEDTask(void *pvParameters)
{
	GPIOSetDir(0, GPIO_LED_RED,   1);
	GPIOSetDir(0, GPIO_LED_GREEN, 1);
	GPIOSetDir(0, GPIO_LED_BLUE,  1);

	uint32_t cnt;
	for (cnt = 0; 1; cnt++) {
		GPIOSetBitValue(0, GPIO_LED_RED  , cnt & 1);
		GPIOSetBitValue(0, GPIO_LED_GREEN, cnt & 2);
		GPIOSetBitValue(0, GPIO_LED_BLUE , cnt & 4);
		vTaskDelay(configTICK_RATE_HZ);
	}
}

#define MAX_INPUT_LENGTH	40
#define MAX_OUTPUT_LENGTH	80

enum {
	CHAR_BS = 8,
	CHAR_LF = 10,
	CHAR_CR = 13,
};

static UART_HANDLE_T *ghUart;
static uint8_t gucUartRam[40];

static void serial_puts(uint8_t* str)
{
	uint8_t ch;
	while ((ch = *str++) != 0)
		LPC_UARTD_API->uart_put_char(ghUart, ch);
}

static void vUARTConsoleTask(void *pvParameters)
{
	// Enable USART0 & drive the USARTs with the same system clock as core
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 14);
	LPC_SYSCON->PRESETCTRL &= ~(1 << 3);
	LPC_SYSCON->PRESETCTRL |=  (1 << 3);
	LPC_SYSCON->UARTCLKDIV = LPC_SYSCON->SYSAHBCLKDIV;

	ghUart = LPC_UARTD_API->uart_setup((uint32_t)LPC_USART0, gucUartRam);
	static UART_CONFIG_T cfg = {
		0,		/* U_PCLK frequency in Hz */
		115200,	/* Baud Rate in Hz */
		1,		/* 8N1 */
		0,		/* Asynchronous Mode */
		0		/* Enable No Errors */
	};
	cfg.sys_clk_in_hz = SystemCoreClock;
	uint32_t frg_mult = LPC_UARTD_API->uart_init(ghUart, &cfg);
	if (frg_mult) {
		LPC_SYSCON->UARTFRGDIV  = 0xff;
		LPC_SYSCON->UARTFRGMULT = frg_mult;
	}

	uint8_t ch, index = 0;
	static char pcInStr[MAX_INPUT_LENGTH], pcOutStr[MAX_OUTPUT_LENGTH];
	BaseType_t more;
	while (1) {
		ch = LPC_UARTD_API->uart_get_char(ghUart);
		switch (ch) {
		case CHAR_CR :
			serial_puts("\r\n");
			do {
				more = FreeRTOS_CLIProcessCommand(pcInStr, pcOutStr, sizeof(pcOutStr));
				serial_puts(pcOutStr);
			} while (more == pdTRUE);
			index = 0;
			serial_puts("\r\nLPC8xx> ");
		case CHAR_LF :
			break;
		case CHAR_BS :
			if (index > 0) {
				pcInStr[--index] = 0;
				LPC_UARTD_API->uart_put_char(ghUart, ch);
			}
			break;
		default :
			if (index < MAX_INPUT_LENGTH - 1) {
				pcInStr[index++] = ch, pcInStr[index] = 0;
				LPC_UARTD_API->uart_put_char(ghUart, ch);
			}
		}
	}
}

int main(void)
{
	SwitchMatrix_Init();
	IOCON_Init();
	Clock_Setup();
	SystemCoreClockUpdate();
	GPIOInit();

	/* LED toggle thread */
	xTaskCreate(vLEDTask, "vLedTask",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	xTaskCreate(vUARTConsoleTask, "vTaskUartConsole",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(xTaskHandle*)NULL);

	/* Start the scheduler */
	vTaskStartScheduler();
	return 0;
}