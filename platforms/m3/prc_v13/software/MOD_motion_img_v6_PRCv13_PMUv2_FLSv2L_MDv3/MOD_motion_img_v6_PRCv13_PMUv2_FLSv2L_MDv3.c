//****************************************************************************************************
//Author:       Gyouho Kim
//Description: 	MODv2.0 System Code
//				Imaging and motion detection with MDv3
//				FLASH storage with FLSv2
//*******************************************************************
#include "PRCv13.h"
#include "PRCv13_RF.h"
#include "mbus.h"
#include "PMUv2_RF.h"
#include "MDv3.h"
#include "RADv9.h"

// uncomment this for debug mbus message
#define DEBUG_MBUS_MSG

// Stack order: PRC->HRV->MD->RAD->FLS->PMU
#define PRC_ADDR 0x1
#define MD_ADDR 0x4
#define RAD_ADDR 0x5
#define HRV_ADDR 0x6
#define FLS_ADDR 0x7
#define PMU_ADDR 0x8

// Common parameters
#define	MBUS_DELAY 200 //Amount of delay between successive messages; 5-6ms
#define WAKEUP_DELAY 20000 // 0.6s
#define DELAY_1 20000 // 5000: 0.5s
#define DELAY_IMG 40000 // 1s

// MDv3 Parameters
#define START_COL_IDX 0 // in words
#define COLS_TO_READ 39 // in # of words: 39 for full frame, 19 for half

// Radio configurations
#define RADIO_DATA_LENGTH 96
#define RADIO_TIMEOUT_COUNT 500
#define WAKEUP_PERIOD_RADIO_INIT 2
#define RADIO_PACKET_DELAY 4000 // Need 100-200ms

// FLSv1 configurations
//#define FLS_RECORD_LENGTH 0x18FE // In words; # of words stored -2
#define FLS_RECORD_LENGTH 6473 // In words; # of words stored -2; Full image with mbus overhead
#define FLS_CHECK_LENGTH 200 // In words

//***************************************************
// Global variables
//***************************************************
//Test Declerations
// "static" limits the variables to this file, giving compiler more freedom
// "volatile" should only be used for MMIO
volatile uint32_t enumerated;
volatile uint32_t exec_count;
volatile uint32_t exec_count_irq;
volatile uint32_t mbus_msg_flag;

volatile bool radio_ready;
volatile bool radio_on;
volatile uint32_t radio_tx_option;
volatile uint32_t radio_tx_img_idx;
volatile uint32_t radio_tx_img_all;
volatile uint32_t radio_tx_img_one;
volatile uint32_t radio_tx_img_num;

volatile uint32_t md_count;
volatile uint32_t img_count;
volatile uint32_t md_capture_img;
volatile uint32_t md_start_motion;
volatile uint32_t USR_MD_INT_TIME;
volatile uint32_t USR_INT_TIME;

volatile mdv3_r0_t mdv3_r0 = MDv3_R0_DEFAULT;
volatile mdv3_r1_t mdv3_r1 = MDv3_R1_DEFAULT;
volatile mdv3_r2_t mdv3_r2 = MDv3_R2_DEFAULT;
volatile mdv3_r3_t mdv3_r3 = MDv3_R3_DEFAULT;
volatile mdv3_r4_t mdv3_r4 = MDv3_R4_DEFAULT;
volatile mdv3_r5_t mdv3_r5 = MDv3_R5_DEFAULT;
volatile mdv3_r6_t mdv3_r6 = MDv3_R6_DEFAULT;
volatile mdv3_r7_t mdv3_r7 = MDv3_R7_DEFAULT;
volatile mdv3_r8_t mdv3_r8 = MDv3_R8_DEFAULT;
volatile mdv3_r9_t mdv3_r9 = MDv3_R9_DEFAULT;

volatile radv9_r0_t radv9_r0 = RADv9_R0_DEFAULT;
volatile radv9_r1_t radv9_r1 = RADv9_R1_DEFAULT;
volatile radv9_r2_t radv9_r2 = RADv9_R2_DEFAULT;
volatile radv9_r3_t radv9_r3 = RADv9_R3_DEFAULT;
volatile radv9_r11_t radv9_r11 = RADv9_R11_DEFAULT;
volatile radv9_r12_t radv9_r12 = RADv9_R12_DEFAULT;
volatile radv9_r13_t radv9_r13 = RADv9_R13_DEFAULT;

volatile prcv13_r0B_t prcv13_r0B = PRCv13_R0B_DEFAULT;

volatile uint32_t WAKEUP_PERIOD_CONT;
volatile uint32_t WAKEUP_PERIOD_CONT_INIT; 

volatile uint32_t flash_read_data[3] = {0, 0, 0}; // Read 3 words at a time; 96 bits of radio packet
volatile uint32_t flash_read_data_single = 0; 

//*******************************************************************
// INTERRUPT HANDLERS
//*******************************************************************
void handler_ext_int_0(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_1(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_2(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_3(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_4(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_5(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_6(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_7(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_8(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_9(void)  __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_10(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_11(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_12(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_13(void) __attribute__ ((interrupt ("IRQ")));
void handler_ext_int_14(void) __attribute__ ((interrupt ("IRQ")));

void handler_ext_int_0(void)  { *NVIC_ICPR = (0x1 << 0);  } // TIMER32
void handler_ext_int_1(void)  { *NVIC_ICPR = (0x1 << 1);  } // TIMER16
void handler_ext_int_2(void)  { *NVIC_ICPR = (0x1 << 2); mbus_msg_flag = 0x10; } // REG0
void handler_ext_int_3(void)  { *NVIC_ICPR = (0x1 << 3); mbus_msg_flag = 0x11; } // REG1
void handler_ext_int_4(void)  { *NVIC_ICPR = (0x1 << 4); mbus_msg_flag = 0x12; } // REG2
void handler_ext_int_5(void)  { *NVIC_ICPR = (0x1 << 5); mbus_msg_flag = 0x13; } // REG3
void handler_ext_int_6(void)  { *NVIC_ICPR = (0x1 << 6); mbus_msg_flag = 0x14; } // REG4
void handler_ext_int_7(void)  { *NVIC_ICPR = (0x1 << 7); mbus_msg_flag = 0x15; } // REG5
void handler_ext_int_8(void)  { *NVIC_ICPR = (0x1 << 8); mbus_msg_flag = 0x16; } // REG6
void handler_ext_int_9(void)  { *NVIC_ICPR = (0x1 << 9); mbus_msg_flag = 0x17; } // REG7
void handler_ext_int_10(void) { *NVIC_ICPR = (0x1 << 10); } // MEM WR
void handler_ext_int_11(void) { *NVIC_ICPR = (0x1 << 11); } // MBUS_RX
void handler_ext_int_12(void) { *NVIC_ICPR = (0x1 << 12); } // MBUS_TX
void handler_ext_int_13(void) { *NVIC_ICPR = (0x1 << 13); } // MBUS_FWD
void handler_ext_int_14(void) { *NVIC_ICPR = (0x1 << 14); } // MBUS_FWD


//************************************
// PMU Related Functions
//************************************
inline static void set_pmu_sleep_clk_default(){
    // PRCv11 Default: 0x8F770049
    // 0x4F773849: use GOC x10-25 for "poly" and "poly2"
    // 			   use GOC x5-10 for "Y2"
	*((volatile uint32_t *) 0xA200000C) =
		( (0x0 << 31) /* PMU_DIV6_OVRD */
		| (0x1 << 30) /* PMU_DIV5_OVRD */
		| (0x0 << 28) /* PMU_LC_TYPE and PMU_SEL_HARV_SRC */
		| (0x3 << 26) /* PMU_WUTH */ | (0x3 << 24) /* PMU_HARV_CLK_SEL */
		| (0x7 << 20) /* PMU_HARV_TUNE_FRAC */ | (0x7 << 16) /* PMU_HARV_STOP_CNT */
		| (0x0 << 14) /* PMU_OSCACT_DIV[1:0] */
		| (0x7 << 11) /* PMU_OSCACT_SEL[2:0] */
		| (0x0 << 9) /* PMU_OSCGOC_DIV[1:0] */
		| (0x0 << 7) /* PMU_OSCSLP_DIV[1:0] */
		| (0x4 << 4) /* PMU_OSCSLP_SEL[2:0] */
		| (0x2 << 2)  /* PMU_OVCHG_SEL */ | (0x1 << 0)  /* PMU_SCNDIV_SEL_TUNE */
		);
}
inline static void set_pmu_sleep_clk_fastest(){
    // PRCv11 Default: 0x8F770049
    // 0x4F773879: use GOC x70-x230 for "poly" and "poly2"
    // 0x4F773879: use GOC x40-
	*((volatile uint32_t *) 0xA200000C) =
		( (0x0 << 31) /* PMU_DIV6_OVRD */
		| (0x1 << 30) /* PMU_DIV5_OVRD */
		| (0x0 << 28) /* PMU_LC_TYPE and PMU_SEL_HARV_SRC */
		| (0x3 << 26) /* PMU_WUTH */ | (0x3 << 24) /* PMU_HARV_CLK_SEL */
		| (0x7 << 20) /* PMU_HARV_TUNE_FRAC */ | (0x7 << 16) /* PMU_HARV_STOP_CNT */
		| (0x0 << 14) /* PMU_OSCACT_DIV[1:0] */
		| (0x7 << 11) /* PMU_OSCACT_SEL[2:0] */
		| (0x0 << 9) /* PMU_OSCGOC_DIV[1:0] */
		| (0x0 << 7) /* PMU_OSCSLP_DIV[1:0] */
		| (0x7 << 4) /* PMU_OSCSLP_SEL[2:0] */
		| (0x2 << 2)  /* PMU_OVCHG_SEL */ | (0x1 << 0)  /* PMU_SCNDIV_SEL_TUNE */
		);
}


//***************************************************
// Radio transmission routines for PPM Radio (RADv9)
//***************************************************

static void radio_power_on(){
	// Need to speed up sleep pmu clock
	set_pmu_sleep_clk_fastest();

    // Release FSM Sleep - Requires >2s stabilization time
    radio_on = 1;
    radv9_r13.RAD_FSM_SLEEP = 0;
    mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
    delay(MBUS_DELAY);
    // Release SCRO Reset
    radv9_r2.SCRO_RESET = 0;
    mbus_remote_register_write(RAD_ADDR,2,radv9_r2.as_int);
    delay(MBUS_DELAY);
    
    // Additional delay required after SCRO Reset release
    delay(MBUS_DELAY*3); // At least 20ms required
    
    // Enable SCRO
    radv9_r2.SCRO_ENABLE = 1;
    mbus_remote_register_write(RAD_ADDR,2,radv9_r2.as_int);
    delay(MBUS_DELAY);

	// Release FSM Isolate
	radv9_r13.RAD_FSM_ISOLATE = 0;
	mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
	delay(MBUS_DELAY);

}

static void radio_power_off(){
	// Need to restore sleep pmu clock
	set_pmu_sleep_clk_default();

    // Turn off everything
    radio_on = 0;
    radv9_r2.SCRO_ENABLE = 0;
    radv9_r2.SCRO_RESET  = 1;
    mbus_remote_register_write(RAD_ADDR,2,radv9_r2.as_int);
    radv9_r13.RAD_FSM_SLEEP 	= 1;
    radv9_r13.RAD_FSM_ISOLATE 	= 1;
    radv9_r13.RAD_FSM_RESETn 	= 0;
    radv9_r13.RAD_FSM_ENABLE 	= 0;
    mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
}

static void send_radio_data_ppm(bool last_packet, uint32_t radio_data){
    // Write Data: Only up to 24bit data for now
    radv9_r3.RAD_FSM_DATA = radio_data;
    mbus_remote_register_write(RAD_ADDR,3,radv9_r3.as_int);
    delay(MBUS_DELAY);

    if (!radio_ready){
		radio_ready = 1;

		// Release FSM Reset
		radv9_r13.RAD_FSM_RESETn = 1;
		mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
		delay(MBUS_DELAY);
    }

    // Set CPU Halt Option as RX --> Use for register read e.g.
    set_halt_until_mbus_rx();

    // Fire off data
    uint32_t count;
    mbus_msg_flag = 0;
    radv9_r13.RAD_FSM_ENABLE = 1;
    mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
    delay(MBUS_DELAY);

    for( count=0; count<RADIO_TIMEOUT_COUNT; count++ ){
		if( mbus_msg_flag ){
    		set_halt_until_mbus_tx();
			mbus_msg_flag = 0;
			if (last_packet){
				radio_ready = 0;
				radio_power_off();
			}else{
				radv9_r13.RAD_FSM_ENABLE = 0;
				mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
				delay(MBUS_DELAY);
			}
			return;
		}else{
			delay(MBUS_DELAY);
		}
    }
	
    set_halt_until_mbus_tx();
	mbus_write_message32(0xBB, 0xFAFAFAFA);
}

static void send_radio_data_ppm_96(bool last_packet, uint32_t radio_data_0, uint32_t radio_data_1, uint32_t radio_data_2, uint32_t radio_data_3){
	// Sends 96 bits of data (3 words, 4 RADv9 registers)
	// radio_data_0: DATA[23:0]
	// radio_data_1: DATA[47:24]
	// radio_data_2: DATA[71:48]
	// radio_data_3: DATA[95:72]
    mbus_remote_register_write(RAD_ADDR,3,radio_data_0);
    delay(MBUS_DELAY);
    mbus_remote_register_write(RAD_ADDR,4,radio_data_1);
    delay(MBUS_DELAY);
    mbus_remote_register_write(RAD_ADDR,5,radio_data_2);
    delay(MBUS_DELAY);
    mbus_remote_register_write(RAD_ADDR,6,radio_data_3);
    delay(MBUS_DELAY);

    if (!radio_ready){
		radio_ready = 1;

		// Release FSM Reset
		radv9_r13.RAD_FSM_RESETn = 1;
		mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
		delay(MBUS_DELAY);
    }

    // Set CPU Halt Option as RX --> Use for register read e.g.
    set_halt_until_mbus_rx();

    // Fire off data
    uint32_t count;
    mbus_msg_flag = 0;
    radv9_r13.RAD_FSM_ENABLE = 1;
    mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
    delay(MBUS_DELAY);

    for( count=0; count<RADIO_TIMEOUT_COUNT; count++ ){
		if( mbus_msg_flag ){
    		set_halt_until_mbus_tx();
			mbus_msg_flag = 0;
			if (last_packet){
				radio_ready = 0;
				radio_power_off();
			}else{
				radv9_r13.RAD_FSM_ENABLE = 0;
				mbus_remote_register_write(RAD_ADDR,13,radv9_r13.as_int);
				delay(MBUS_DELAY);
			}
			return;
		}else{
			delay(MBUS_DELAY);
		}
    }
	
	// Timeout
    set_halt_until_mbus_tx();
	mbus_write_message32(0xBB, 0xFAFAFAFA);
}
//***************************************************
// End of Program Sleep Operation
//***************************************************
static void operation_sleep(void){

	// Reset IRQ14VEC
	*((volatile uint32_t *) IRQ14VEC) = 0;

	// Go to Sleep
    mbus_sleep_all();
	while(1);

}

static void operation_sleep_noirqreset(void){

	// Go to Sleep
	mbus_sleep_all();
	while(1);

}

static void operation_sleep_notimer(void){
    
	// Turn off radio
	radio_power_off();

	// Disable Timer
	set_wakeup_timer(0, 0, 0);

	// Go to sleep without timer
	operation_sleep();

}

//************************************
// FLSv2 Functions
//************************************

uint32_t check_flash_sram(uint8_t addr_stamp, uint32_t length){
	uint32_t idx;

	for(idx=0; idx<length; idx++) {

    	set_halt_until_mbus_rx();
		mbus_copy_mem_from_remote_to_any_bulk(FLS_ADDR, (uint32_t*)(idx << 2), PRC_ADDR, (uint32_t*)&flash_read_data_single, 0);
		//FLSv2MBusGPIO_readMem(FLS_ADDR, 0xEE, 0, ((uint32_t) idx) << 2);
		delay(MBUS_DELAY);
    	set_halt_until_mbus_tx();
		mbus_write_message32(addr_stamp, flash_read_data_single);
		delay(MBUS_DELAY);
		
	}

	return 1;
}

uint32_t send_radio_flash_sram(uint8_t addr_stamp, uint32_t length){
	uint32_t idx;
	uint32_t fls_rx_data;

	for(idx=0; (idx*3)<length; idx++) {

    	set_halt_until_mbus_rx();
		mbus_copy_mem_from_remote_to_any_bulk(FLS_ADDR, (uint32_t*)((idx*3) << 2), PRC_ADDR, (uint32_t*)&flash_read_data, 2);
		//FLSv2MBusGPIO_readMem(FLS_ADDR, 0xEE, 0, ((uint32_t) idx) << 2);
		// Send 96 bits of data
		send_radio_data_ppm_96(0,flash_read_data[0],flash_read_data[1],flash_read_data[2],flash_read_data[2]);
		delay(MBUS_DELAY);
	}

	return 1;
}

static void flash_turn_on(){

    set_halt_until_mbus_rx();
	mbus_remote_register_write(FLS_ADDR,0x11,0x00003F);
    set_halt_until_mbus_tx();
	//check_flash_payload(0xA4, 0x000000B5);

}

static void flash_turn_off(){

    set_halt_until_mbus_rx();
	mbus_remote_register_write(FLS_ADDR,0x11,0x00003D);
    set_halt_until_mbus_tx();
	//check_flash_payload(0xA6, 0x000000BB);

}

static void flash_erase_single_page(volatile uint32_t page_num){
	// page_num should be a multiple of 0x800
    mbus_remote_register_write(FLS_ADDR,0x09,page_num); // Set flash start addr
    set_halt_until_mbus_rx();
	mbus_remote_register_write(FLS_ADDR,0x07,((0x0 << 6) | (0x1 << 5) | (0x4 << 1) | (0x1 << 0))); // Do erase
    set_halt_until_mbus_tx();
	//check_flash_payload(0xA1, 0x00000055);

}

static void flash_copy_flash2sram(volatile uint32_t length){

    mbus_remote_register_write(FLS_ADDR,0x08,0x0); // Set SRAM start addr
    set_halt_until_mbus_rx();
	mbus_remote_register_write(FLS_ADDR,0x07,((length << 6) | (0x1 << 5) | (0x1 << 1) | (0x1 << 0)));
    set_halt_until_mbus_tx();
	//check_flash_payload(0xA8, 0x00000024);

}

static void flash_copy_sram2flash(volatile uint32_t length){

    mbus_remote_register_write(FLS_ADDR,0x08,0x0); // Set SRAM start addr
    set_halt_until_mbus_rx();
	mbus_remote_register_write(FLS_ADDR,0x07,((length << 6) | (0x1 << 5) | (0x2 << 1) | (0x1 << 0)));
    set_halt_until_mbus_tx();
	//check_flash_payload(0xA8, 0x0000003D);

}

//************************************
// MDv3 Functions
//************************************

static void initialize_md_reg(){

	mdv3_r9 = MDv3_R9_DEFAULT;

	mdv3_r0.INT_TIME = USR_INT_TIME;
	mdv3_r0.MD_INT_TIME = USR_MD_INT_TIME;
	mdv3_r1.MD_TH = 10;
	mdv3_r1.MD_LOWRES = 0;
	mdv3_r1.MD_LOWRES_B = 1;
	mdv3_r1.MD_FLAG_TOPAD_SEL = 0; // 1: thresholding, 0: no thresholding

	mdv3_r2.MD_RESULTS_MASK = 0x3FF;

	mdv3_r3.VDD_CC_ENB_0P6 = 0;
	mdv3_r3.VDD_CC_ENB_1P2 = 1;
	mdv3_r3.SEL_VREF = 0;
	mdv3_r3.SEL_VREFP = 7;
	mdv3_r3.SEL_VBN = 3;
	mdv3_r3.SEL_VBP = 3;
	mdv3_r3.SEL_VB_RAMP = 15;
	mdv3_r3.SEL_RAMP = 1;

	mdv3_r4.SEL_CC  = 4;
	mdv3_r4.SEL_CC_B  = 3;

	mdv3_r5.SEL_CLK_RING = 2;
	mdv3_r5.SEL_CLK_DIV = 4;
	mdv3_r5.SEL_CLK_RING_4US = 0;
	mdv3_r5.SEL_CLK_DIV_4US = 1;
	mdv3_r5.SEL_CLK_RING_ADC = 2; 
	mdv3_r5.SEL_CLK_DIV_ADC = 1;
	mdv3_r5.SEL_CLK_RING_LC = 0;
	mdv3_r5.SEL_CLK_DIV_LC = 1;

	mdv3_r6.START_ROW_IDX = 40;
	mdv3_r6.END_ROW_IDX = 120; // Default: 160
	mdv3_r6.ROW_SKIP = 0;
	mdv3_r6.COL_SKIP = 0;
	mdv3_r6.ROW_IDX_EN = 0;

	mdv3_r8.MBUS_REPLY_ADDR_FLAG = 0x18;
	mdv3_r9.MBUS_REPLY_ADDR_DATA = 0x72; // IMG Data return address

	mdv3_r8.MBUS_START_ADDR = 0; // Start column index in words
	mdv3_r8.MBUS_LENGTH_M1 = 39; // Columns to be read; in # of words: 39 for full frame, 19 for half

	//Write Registers
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	mbus_remote_register_write(MD_ADDR,0x1,mdv3_r1.as_int);
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	mbus_remote_register_write(MD_ADDR,0x3,mdv3_r3.as_int);
	mbus_remote_register_write(MD_ADDR,0x4,mdv3_r4.as_int);
	mbus_remote_register_write(MD_ADDR,0x5,mdv3_r5.as_int);
	mbus_remote_register_write(MD_ADDR,0x6,mdv3_r6.as_int);
	mbus_remote_register_write(MD_ADDR,0x7,mdv3_r7.as_int);
	mbus_remote_register_write(MD_ADDR,0x8,mdv3_r8.as_int);
	mbus_remote_register_write(MD_ADDR,0x9,mdv3_r9.as_int);

}

static void start_md(){

	// Optionally release MD GPIO Isolation
	// 7:16
	//mdv3_r7.ISOLATE_GPIO = 0;
	//mbus_remote_register_write(MD_ADDR,0x7,mdv3_r7.as_int);
	//delay(MBUS_DELAY);
	//delay(DELAY_500ms); // about 0.5s

	// Start MD
	// 0:1
	mdv3_r0.START_MD = 1;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	delay(MBUS_DELAY*4);

	mdv3_r0.START_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	delay(MBUS_DELAY);

	delay(DELAY_1); // about 0.5s

	// Enable MD Flag
	// 1:3
	mdv3_r1.MD_TH_EN = 1;
	mbus_remote_register_write(MD_ADDR,0x1,mdv3_r1.as_int);
	delay(MBUS_DELAY);

}

static void clear_md_flag(){

	// Stop MD
	// 0:2
	mdv3_r0.STOP_MD = 1;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	delay(MBUS_DELAY*4);

	mdv3_r0.STOP_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	delay(MBUS_DELAY);

	// Clear MD Flag
	// 1:4
	mdv3_r1.MD_TH_CLEAR = 1;
	mbus_remote_register_write(MD_ADDR,0x1,mdv3_r1.as_int);
	delay(MBUS_DELAY*4);

	mdv3_r1.MD_TH_CLEAR = 0;
	mbus_remote_register_write(MD_ADDR,0x1,mdv3_r1.as_int);
	delay(MBUS_DELAY);

	// Disable MD Flag
	// 1:3
	mdv3_r1.MD_TH_EN = 0;
	mbus_remote_register_write(MD_ADDR,0x1,mdv3_r1.as_int);
	delay(MBUS_DELAY);

}

static void poweron_frame_controller(){

	// Release MD Presleep
	// 2:22
	mdv3_r2.PRESLEEP_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(WAKEUP_DELAY);

	// Release MD Sleep
	// 2:21
	mdv3_r2.SLEEP_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(WAKEUP_DELAY);

	// Release MD Isolation
	// 7:15
	mdv3_r7.ISOLATE_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x7,mdv3_r7.as_int);
	delay(MBUS_DELAY);

	// Release MD Reset
	// 2:23
	mdv3_r2.RESET_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(MBUS_DELAY);

	// Start MD Clock
	// 5:12
	mdv3_r5.CLK_EN_MD = 1;
	mbus_remote_register_write(MD_ADDR,0x5,mdv3_r5.as_int);
	delay(MBUS_DELAY);

}

static void poweron_frame_controller_short(){

	// Release MD Presleep
	// 2:22
	mdv3_r2.PRESLEEP_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(MBUS_DELAY);

	// Release MD Sleep
	// 2:21
	mdv3_r2.SLEEP_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(MBUS_DELAY);

	// Release MD Isolation
	// 7:15
	mdv3_r7.ISOLATE_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x7,mdv3_r7.as_int);
	delay(MBUS_DELAY);

	// Release MD Reset
	// 2:23
	mdv3_r2.RESET_MD = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(MBUS_DELAY);

	// Start MD Clock
	// 5:12
	mdv3_r5.CLK_EN_MD = 1;
	mbus_remote_register_write(MD_ADDR,0x5,mdv3_r5.as_int);
	delay(MBUS_DELAY);

}

static void poweron_array_adc(){

	// Release IMG Presleep 
	// 2:20
	mdv3_r2.PRESLEEP_ADC = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(WAKEUP_DELAY);

	// Release IMG Sleep
	// 2:19
	mdv3_r2.SLEEP_ADC = 0;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(WAKEUP_DELAY);

	// Release ADC Isolation
	// 7:17
	mdv3_r7.ISOLATE_ADC_WRAPPER = 0;
	mbus_remote_register_write(MD_ADDR,0x7,mdv3_r7.as_int);
	delay(MBUS_DELAY);

	// Release ADC Wrapper Reset
	// 6:0
	mdv3_r6.RESET_ADC_WRAPPER = 0;
	mbus_remote_register_write(MD_ADDR,0x6,mdv3_r6.as_int);
	delay(MBUS_DELAY);

	// Start ADC Clock
	// 5:13
	mdv3_r5.CLK_EN_ADC = 1;
	mbus_remote_register_write(MD_ADDR,0x5,mdv3_r5.as_int);
	delay(MBUS_DELAY);

}

static void poweroff_array_adc(){

	// Stop ADC Clock
	// 5:13
	mdv3_r5.CLK_EN_ADC = 0;
	mbus_remote_register_write(MD_ADDR,0x5,mdv3_r5.as_int);
	delay(MBUS_DELAY);

	// Assert ADC Wrapper Reset
	// 6:0
	mdv3_r6.RESET_ADC_WRAPPER = 1;
	mbus_remote_register_write(MD_ADDR,0x6,mdv3_r6.as_int);
	delay(MBUS_DELAY);

	// Assert ADC Isolation
	// 7:17
	mdv3_r7.ISOLATE_ADC_WRAPPER = 1;
	mbus_remote_register_write(MD_ADDR,0x7,mdv3_r7.as_int);
	delay(MBUS_DELAY);

	// Assert IMG Presleep 
	// 2:20
	// Assert IMG Sleep
	// 2:19
	mdv3_r2.PRESLEEP_ADC = 1;
	mdv3_r2.SLEEP_ADC = 1;
	mbus_remote_register_write(MD_ADDR,0x2,mdv3_r2.as_int);
	delay(MBUS_DELAY);

}


static void capture_image_single(){

	// Capture Image
	// 0:0
	mdv3_r0.TAKE_IMAGE = 1;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	delay(MBUS_DELAY*4); //Need >10ms

	mdv3_r0.TAKE_IMAGE = 0;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);

}

static void capture_image_start(){

	// Capture Image
	// 0:0
	mdv3_r0.TAKE_IMAGE = 1;
	mbus_remote_register_write(MD_ADDR,0x0,mdv3_r0.as_int);
	delay(MBUS_DELAY);

}

static void capture_image_single_with_flash(uint32_t page_offset){

	// Start imaging
	capture_image_single();
	delay(DELAY_IMG);

	// Power-gate MD
	poweroff_array_adc();

	// Check Flash SRAM after image
	check_flash_sram(0xE1, FLS_CHECK_LENGTH);

	// Turn on Flash macro
	flash_turn_on();

	delay(MBUS_DELAY);
	mbus_write_message32(0xF1, 0x11111111);
	delay(MBUS_DELAY);

	// Copy SRAM to Flash
    mbus_remote_register_write(FLS_ADDR,0x09,page_offset); // Set flash start addr; should be a multiple of 0x800
	flash_copy_sram2flash(0x1FFF); // 4 pages

	// Turn off Flash
	flash_turn_off();

	delay(MBUS_DELAY);
	mbus_write_message32(0xF1, 0x33333333);
	delay(MBUS_DELAY);

}


static void operation_flash_erase(uint32_t page_offset){

	// Set START ADDRESS
    mbus_remote_register_write(FLS_ADDR,0x08,0x0);

	// Turn on Flash
	flash_turn_on();

	// Flash Erase Page 2-5
	flash_erase_single_page(page_offset); // Should be a multiple of 0x800
	flash_erase_single_page(page_offset+0x800); // Should be a multiple of 0x800
	flash_erase_single_page(page_offset+0x1000); // Should be a multiple of 0x800
	flash_erase_single_page(page_offset+0x1800); // Should be a multiple of 0x800

	// Turn off Flash
	flash_turn_off();

}

static void operation_flash_read(uint32_t page_offset){

	delay(MBUS_DELAY);
	mbus_write_message32(0xF2, 0x11111111);
	delay(MBUS_DELAY);

	// Turn on Flash
	flash_turn_on();

	delay(MBUS_DELAY);
	mbus_write_message32(0xF2, 0x22222222);
	delay(MBUS_DELAY);

	// Copy Flash to SRAM
    mbus_remote_register_write(FLS_ADDR,0x09,page_offset); // Set flash start addr; should be a multiple of 0x800
	flash_copy_flash2sram(0x1FFF); // 4 pages

	// Turn off Flash
	flash_turn_off();

	// Check Flash SRAM after recovery
	check_flash_sram(0xE3, FLS_CHECK_LENGTH);
}

static void operation_md(void){

	// Release power gates, isolation, and reset for frame controller
	if (md_count == 0) {
		initialize_md_reg();
		poweron_frame_controller();
	}else{
		// This wakeup is due to motion detection
		// Let the world know!
		mbus_write_message32(0xAA, 0x22222222);
  		delay(MBUS_DELAY);
		clear_md_flag();
		if (radio_tx_option){
			// radio
			send_radio_data_ppm(0,0xFAFA1234);	
		}
	}

	if (md_capture_img){
		// Release power gates, isolation, and reset for imager array
		poweron_array_adc();

		// Capture a single image
		//capture_image_single();
		if (img_count < 60){

		#ifdef DEBUG_MBUS_MSG
			mbus_write_message32(0xAF, img_count);
			delay(MBUS_DELAY);
			mbus_write_message32(0xAF, 0x800 + img_count*0x2000);
			delay(MBUS_DELAY);
		#endif
			capture_image_single_with_flash(0x800+img_count*0x2000);
			img_count++;
			// Erase the next section of flash
			operation_flash_erase(0x800+img_count*0x2000);
		}

		poweroff_array_adc();

		if (radio_tx_option){
			// Radio out image data stored in flash
			send_radio_data_ppm(0,0xFAF000);
			send_radio_flash_sram(0xE4, 6475); // Full image
			send_radio_data_ppm(0,0xFAF000);
		}
	}

	if (md_start_motion){
		// Only need to set sleep PMU settings
        set_pmu_sleep_clk_fastest();
		
		md_count++;

		// Start motion detection
		start_md();
		delay(DELAY_1);
		clear_md_flag();
		delay(DELAY_1);
		start_md();

	}else{
		// Restore PMU_CTRL setting
		//set_pmu_sleep_clk_default();
	}

	// Go to sleep w/o timer
	operation_sleep_notimer();
}

static void operation_tx_image(void){

	#ifdef DEBUG_MBUS_MSG
		mbus_write_message32(0xAF, radio_tx_img_idx);
		delay(MBUS_DELAY);
		mbus_write_message32(0xAF, 0x800 + radio_tx_img_idx*0x2000);
		delay(MBUS_DELAY);
	#endif

	// Read image from Flash 
	operation_flash_read(0x800 + radio_tx_img_idx*0x2000);

	// Send image to radio
	//send_radio_flash_sram(0xE4, 6475); // Full image

	if (!radio_tx_img_one && (radio_tx_img_idx < radio_tx_img_num)){
		radio_tx_img_idx++;
		// Send next image after sleep/wakeup
		set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
		operation_sleep_noirqreset();
    }else{
		delay(RADIO_PACKET_DELAY); //Set delays between sending subsequent packet
		send_radio_data_ppm(1, 0xFAF000);

		// This is also the end of this IRQ routine
		exec_count_irq = 0;
		// Go to sleep without timer
		operation_sleep_notimer();
    }

}

static void operation_init(void){
  
	// Set CPU & Mbus Clock Speeds
    prcv13_r0B.CLK_GEN_RING = 0x3; // Default 0x1
    prcv13_r0B.CLK_GEN_DIV_MBC = 0x0; // Default 0x1
    prcv13_r0B.CLK_GEN_DIV_CORE = 0x2; // Default 0x3
	*((volatile uint32_t *) REG_CLKGEN_TUNE ) = prcv13_r0B.as_int;

	// Set PMU settings
	// FIXME
	set_pmu_sleep_clk_default();
    delay(DELAY_1);

    //Enumerate & Initialize Registers
    enumerated = 0xDEADBEEF;
    exec_count = 0;
    exec_count_irq = 0;
    mbus_msg_flag = 0;

    // Set CPU Halt Option as RX --> Use for register read e.g.
    set_halt_until_mbus_rx();

    // Enumeration
	// Stack order: PRC->HRV->MD->RAD->FLS->PMU
    mbus_enumerate(HRV_ADDR);
    mbus_enumerate(MD_ADDR);
    mbus_enumerate(RAD_ADDR);
    mbus_enumerate(FLS_ADDR);
    //mbus_enumerate(PMU_ADDR);
    delay(MBUS_DELAY);

    // Set CPU Halt Option as TX --> Use for register write e.g.
    set_halt_until_mbus_tx();

	// Initialize MDv3
	initialize_md_reg();

	delay(MBUS_DELAY);

    // Radio Settings --------------------------------------
    radv9_r0.RADIO_TUNE_CURRENT_LIMITER = 0x2F; //Current Limiter 2F = 30uA, 1F = 3uA
    radv9_r0.RADIO_TUNE_FREQ1 = 0x0; //Tune Freq 1
    radv9_r0.RADIO_TUNE_FREQ2 = 0x0; //Tune Freq 2 //0x0,0x0 = 902MHz on Pblr005
    radv9_r0.RADIO_TUNE_TX_TIME = 0x6; //Tune TX Time
  
    mbus_remote_register_write(RAD_ADDR,0,radv9_r0.as_int);

    // FSM data length setups
    radv9_r11.RAD_FSM_H_LEN = 16; // N
    radv9_r11.RAD_FSM_D_LEN = RADIO_DATA_LENGTH-1; // N-1
    radv9_r11.RAD_FSM_C_LEN = 10;
    mbus_remote_register_write(RAD_ADDR,11,radv9_r11.as_int);
  
    // Configure SCRO
    radv9_r1.SCRO_FREQ_DIV = 3;
    radv9_r1.SCRO_AMP_I_LEVEL_SEL = 2; // Default 2
    radv9_r1.SCRO_I_LEVEL_SELB = 0x60; // Default 0x6F
    mbus_remote_register_write(RAD_ADDR,1,radv9_r1.as_int);
  
    // LFSR Seed
    radv9_r12.RAD_FSM_SEED = 4;
    mbus_remote_register_write(RAD_ADDR,12,radv9_r12.as_int);

	// Mbus return address; Needs to be between 0x18-0x1F
    mbus_remote_register_write(RAD_ADDR,0xF,0x19);
    delay(MBUS_DELAY);

    // Flash Settings --------------------------------------
	// Option to Slow down FLSv2L clock 
	mbus_remote_register_write(FLS_ADDR, 0x18, 
	( (0xC << 2)  /* CLK_RING_SEL[3:0] Default 0xC */
	| (0x1 << 0)  /* CLK_DIV_SEL[1:0] Default 0x1 */
	));

	// Voltage Clamp & Timing settings
	mbus_remote_register_write(FLS_ADDR, 0x0, 0x41205); // Tprog
	mbus_remote_register_write(FLS_ADDR, 0x4, 0x000500); // Tcyc_prog
	mbus_remote_register_write(FLS_ADDR, 0x19, 0x3C4303); // Default: 0x3C4103

	mbus_write_message32(0xAA, 0x11111111);

	md_start_motion = 0;
	md_capture_img = 0;
	md_count = 0;
	img_count = 0;
	
	radio_on = 0;
	radio_ready = 0;
	radio_tx_option = 0;
	radio_tx_img_idx = 0;
	radio_tx_img_all = 0;
	radio_tx_img_one = 0;
	radio_tx_img_num = 0;

	// Reset global variables
	WAKEUP_PERIOD_CONT = 2;
	WAKEUP_PERIOD_CONT_INIT = 2; 

	USR_MD_INT_TIME = 12;
	USR_INT_TIME = 30;

	// Go to sleep w/o timer
	operation_sleep_notimer();
}


//***************************************************************************************
// MAIN function starts here             
//***************************************************************************************
int main() {
  
    // Initialize Interrupts
    // Only enable register-related interrupts
	enable_reg_irq();
  
	// Disable the watch-dog timer
	disable_timerwd();

    // Initialization sequence
    if (enumerated != 0xDEADBEEF){
        operation_init();

    }
	
    // Check if wakeup is due to GOC interrupt  
    // 0x78 is reserved for GOC-triggered wakeup (Named IRQ14VEC)
    // 8 MSB bits of the wakeup data are used for function ID
    uint32_t wakeup_data = *((volatile uint32_t *) IRQ14VEC);	// IRQ14VEC[31:0]
    uint32_t wakeup_data_header = wakeup_data>>24;				// IRQ14VEC[31:24]
    uint32_t wakeup_data_field_0 = wakeup_data & 0xFF;			// IRQ14VEC[7:0]
    uint32_t wakeup_data_field_1 = wakeup_data>>8 & 0xFF;		// IRQ14VEC[15:8]
    uint32_t wakeup_data_field_2 = wakeup_data>>16 & 0xFF;		// IRQ14VEC[23:16]

    if(wakeup_data_header == 1){
        // Debug mode: Transmit something via radio and go to sleep w/o timer
        // wakeup_data[7:0] is the # of transmissions
        // wakeup_data[15:8] is the user-specified period
        // wakeup_data[23:16] is the MSB of # of transmissions
        WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;
        delay(MBUS_DELAY);
        if (exec_count_irq < (wakeup_data_field_0 + (wakeup_data_field_2<<8))){
            exec_count_irq++;
			if (exec_count_irq == 1){
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				// radio
				send_radio_data_ppm(0,0xFAF000+exec_count_irq);	
				// set timer
				set_wakeup_timer (WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
        }else{
            exec_count_irq = 0;
            // radio
            send_radio_data_ppm(1,0xFAF000);	
            // Go to sleep without timer
            operation_sleep_notimer();
        }
   
    }else if(wakeup_data_header == 2){
		// Start motion detection
        // wakeup_data[7:0] is the md integration period
        // wakeup_data[15:8] is the img integration period
        // wakeup_data[17:16] indicates whether or not to to take an image
		// 						1: md only, 2: img only, 3: md+img
        // wakeup_data[18] indicates whether or not to radio out the result
		USR_MD_INT_TIME = wakeup_data_field_0;
		USR_INT_TIME = wakeup_data_field_1;

		// Reset IRQ14VEC
		*((volatile uint32_t *) IRQ14VEC) = 0;

		// Run Program
		exec_count_irq = 0;
		exec_count = 0;
		md_count = 0;
		img_count = 0;
		radio_ready = 0;
		md_start_motion = wakeup_data_field_2 & 0x1;
		md_capture_img = (wakeup_data_field_2 >> 1) & 0x1;
		radio_tx_option = (wakeup_data_field_2 >> 2) & 0x1;
		if (radio_tx_option & !radio_on){
			// Prepare radio TX
			radio_power_on();
			// Go to sleep for SCRO stabilitzation
			set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
			operation_sleep_noirqreset();
		}
		if (md_capture_img){
			operation_flash_erase(0x800);
		}

		operation_md();
   
    }else if(wakeup_data_header == 3){
		// Stop MD program and transmit the execution count n times
        // wakeup_data[7:0] is the # of transmissions
        // wakeup_data[15:8] is the user-specified period 
        WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;
		
		clear_md_flag();
        set_pmu_sleep_clk_default();
		md_start_motion = 0;
		md_capture_img = 0;
		radio_ready = 0;

        if (exec_count_irq < wakeup_data_field_0){
            exec_count_irq++;
			if (exec_count_irq == 1){
				// Initialize MD registers
				initialize_md_reg();
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				// radio
				send_radio_data_ppm(0,0xFAF000+md_count);	
				// set timer
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
        }else{
            exec_count_irq = 0;
            send_radio_data_ppm(1,0xFAF000);	
            // Go to sleep without timer
            operation_sleep_notimer();
        }

    }else if(wakeup_data_header == 4){
		// Read out stored image from flash and transmit
        // wakeup_data[7:0] is the # of image to transmit; Valid range is from 0 (first and oldest image) to img_count-1
        // wakeup_data[15:8] is the user-specified period 
        // wakeup_data[16]: transmit all stored images 
        // wakeup_data[17]: transmit only one image according to the image ID 
		radio_tx_img_num = wakeup_data_field_0;
        WAKEUP_PERIOD_CONT_INIT = wakeup_data_field_1;
		radio_tx_img_all = wakeup_data_field_2 & 0x1;
		radio_tx_img_one = (wakeup_data_field_2>>1) & 0x1;

		if (exec_count_irq == 0){ // Only do this once
			if (radio_tx_img_one){
				radio_tx_img_idx = radio_tx_img_num;
			}else{
				radio_tx_img_idx = 0; // start from oldest image
			}
			if (radio_tx_img_all){
				radio_tx_img_num = img_count;
			}
		}
		
        if (exec_count_irq < 2){
			exec_count_irq++;
			if (exec_count_irq == 1){
				// Prepare radio TX
				radio_power_on();
				// Go to sleep for SCRO stabilitzation
				set_wakeup_timer(WAKEUP_PERIOD_RADIO_INIT, 0x1, 0x1);
				operation_sleep_noirqreset();
			}else{
				send_radio_data_ppm(0, 0xFAF000+exec_count_irq);
				// set timer
				set_wakeup_timer(WAKEUP_PERIOD_CONT_INIT, 0x1, 0x1);
				// go to sleep and wake up with same condition
				operation_sleep_noirqreset();
			}
		}else{
			operation_tx_image();
		}
		
    }else if(wakeup_data_header == 0x12){
		// Restore PMU sleep osc and go to sleep for further programming
        set_pmu_sleep_clk_default();
        // Go to sleep without timer
        operation_sleep_notimer();

    }else if(wakeup_data_header == 0x13){
        set_pmu_sleep_clk_fastest();
        // Go to sleep without timer
        operation_sleep_notimer();
    }

    // Proceed to continuous mode
    while(1){
        operation_md(); 
    }

    // Should not reach here
    operation_sleep_notimer();

    while(1);
}

