#ifdef __USE_CMSIS
#include "LPC8xx.h"
#include "lpc8xx_gpio.h"
#endif
#include "FreeRTOS.h"
#include "task.h"

#include <cr_section_macros.h>

static inline void SwitchMatrix_Init()
{	/* Generated by the Switch Matric Tool */

	/* Enable SWM clock (already enabled in system_LPC8xx.c */
	//LPC_SYSCON->SYSAHBCLKCTRL |= (1<<7);

	/* Pin Assign 8 bit Configuration */
	/* U0_TXD @ PIO0_4 */
	/* U0_RXD @ PIO0_0 */
	LPC_SWM->PINASSIGN0 = 0xffff0004UL;
	/* U1_TXD @ PIO0_2 */
	/* U1_RXD @ PIO0_3 */
	LPC_SWM->PINASSIGN1 = 0xff0302ffUL;

	/* Pin Assign 1 bit Configuration */
	/* XTALIN  @ PIO0_8 */
	/* XTALOUT @ PIO0_9 */
	/* RESET   @ PIO0_5 */
	LPC_SWM->PINENABLE0 = 0xffffff8fUL;

	/* Disable the clock to the Switch Matrix to save power */
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1<<7);
}

static inline void IOCON_Init()
{	/* Generated by the Switch Matric Tool */

	/* Enable IOCON clock (already enabled in system_LPC8xx.c) */
	//LPC_SYSCON->SYSAHBCLKCTRL |= (1<<18);

	/* Pin I/O Configuration */
	// Enable XTALIN and XTALOUT on PIO0_8 and PIO0_9 & remove the pull-up/down resistors
	LPC_IOCON->PIO0_8 = 0x80;
	LPC_IOCON->PIO0_9 = 0x80;

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

enum {
	DISP_TIME,
	DISP_SEC,
	MAX_DISP
};
static volatile uint8_t disp_mode = DISP_TIME;
static volatile uint8_t hour = 12, min = 0, sec = 0;

volatile uint16_t gu16LedScratchPad;

static uint8_t bin2bcd(uint8_t v, uint8_t leading_space)
{
	return ((leading_space && v<10)? 0xf0:(v/10)<<4)|(v%10);
}

static void display_update()
{
	switch (disp_mode) {
	case DISP_TIME:
		gu16LedScratchPad = (((uint16_t)bin2bcd(hour,1)) << 8) | (bin2bcd(min,0));
		break;
	case DISP_SEC:
		gu16LedScratchPad = (0xff << 8) | (bin2bcd(sec,0));
	}
}

static void advance_time_by_min(uint8_t delta)
{
	if ((min += delta) >= 60) {
		min -= 60;
		if (++hour >= 24)
			hour = 0;
	}
	sec = 0;
}

/* Clock thread */
static void vClockTask (void *_unused)
{
	while (1) {
		display_update();
		vTaskDelay(configTICK_RATE_HZ);
		if (++sec >= 60)
			advance_time_by_min(1);
	}
}

/* User Input thread */
static void vUserInputTask (void *_unused)
{
	extern volatile uint32_t flex_int_falling_edge_counter[];
	uint32_t last_cnt = flex_int_falling_edge_counter[CHANNEL0];
	GPIOSetDir(PORT0, 4, 0);
	GPIOSetPinInterrupt(CHANNEL0, PORT0, 4, 0, 0);
	while (1) {
		do __WFI(); while (last_cnt == flex_int_falling_edge_counter[CHANNEL0]);
		last_cnt = flex_int_falling_edge_counter[CHANNEL0];
		if (++disp_mode >= MAX_DISP) disp_mode = DISP_TIME;
		uint32_t hold_cnt;
		for (hold_cnt = 0; !GPIOGetPinValue(PORT0, 4); hold_cnt++) {
			vTaskDelay(configTICK_RATE_HZ / 2);
			if (hold_cnt > 6) {
				disp_mode = DISP_TIME;
				advance_time_by_min(hold_cnt / 10 + 1);
				display_update();
			}
		}
	}
}

/* LED display thread */
static void vLEDDisplayTask (void *_unused)
{
	static uint8_t cathode[] = { 14, 6, 11, 10 };
	enum {
		_A = (1 << 7),
		_B = (1 << 13),
		_C = (1 << 17),
		_D = (1 << 15),
		_E = (1 << 1),
		_F = (1 << 16),
		_G = (1 << 12)
	};
	#define digit0	(_A|_B|_C|_D|_E|_F)
	#define digit1	(_B|_C)
	#define digit2	(_A|_B|_G|_E|_D)
	#define digit3	(_A|_B|_G|_C|_D)
	#define digit4	(_F|_G|_B|_C)
	#define digit5	(_A|_F|_G|_C|_D)
	#define digit6	(_A|_F|_E|_D|_C|_G)
	#define digit7	(_A|_B|_C)
	#define digit8	(_A|_B|_C|_D|_E|_F|_G)
	#define digit9	(_G|_F|_A|_B|_C|_D)
	#define digitA	(_A|_B|_C|_E|_F|_G)
	#define digitB	(_F|_E|_D|_C|_G)
	#define digitC	(_A|_F|_E|_D)
	#define digitD	(_B|_C|_D|_E|_G)
	#define digitE	(_A|_D|_E|_F|_G)
//	#define digitF	(_A|_E|_F|_G)
	#define digitF	0		/* space for leading zeros */
	static uint32_t pattern[] = { digit0, digit1, digit2, digit3,
	                              digit4, digit5, digit6, digit7,
	                              digit8, digit9, digitA, digitB,
	                              digitC, digitD, digitE, digitF
	};

	uint8_t i, bit;
	for (i = 0; i < sizeof(cathode); i++) {
		GPIOSetDir(PORT0, cathode[i], 1);
		GPIOSetBitValue(PORT0, cathode[i], 1);
	}
	for (bit = 1; bit <= 17; bit++) {
		if (digit8 & (1 << bit))
			GPIOSetDir(PORT0, bit, 1);
	}

	while (1) {
		if (++i >= sizeof(cathode)) i = 0;
		uint32_t bitmaps = pattern[(gu16LedScratchPad >> (i<<2)) & 0xf];
		for (bit = 1; bit <= 17; bit++) {
			if (digit8 & (1 << bit))
				GPIOSetBitValue(PORT0, bit, bitmaps & (1 << bit));
		}
		GPIOSetBitValue(PORT0, cathode[i], 0);
		vTaskDelay(configTICK_RATE_HZ/150);
		GPIOSetBitValue(PORT0, cathode[i], 1);
	}
}

int main(void)
{
	SwitchMatrix_Init();
	IOCON_Init();
	Clock_Setup();
	SystemCoreClockUpdate();
	GPIOInit();

	/* LED display thread */
	xTaskCreate(vLEDDisplayTask, "LED Display",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	/* Clock thread */
	xTaskCreate(vClockTask, "Clock",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
	/* User input thread */
	xTaskCreate(vUserInputTask, "User Input",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();
	return 0;
}
