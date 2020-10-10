/*
**********************************************************************************************************************
*
*						           the Embedded Secure Bootloader System
*
*
*						       Copyright(C), 2006-2014, Allwinnertech Co., Ltd.
*                                           All Rights Reserved
*
* File    :
*
* By      :
*
* Version : V2.00
*
* Date	  :
*
* Descript:
**********************************************************************************************************************
*/
#include <common.h>
#include <private_boot0.h>
#include <private_uboot.h>
#include <asm/arch/clock.h>
#include <asm/arch/timer.h>
#include <asm/arch/uart.h>
#include <asm/arch/dram.h>
#include <asm/arch/rtc_region.h>
#include <asm/arch/gpio.h>
#include "../libs/sbrom_libs.h"
#ifdef CONFIG_BOOT_A15
#include <asm/arch/cpu_switch.h>
#endif

extern const boot0_file_head_t  BT0_head;

static void print_version(void);
static void print_commit_log(void);
static int boot0_clear_env(void);
#ifdef	SUNXI_OTA_TEST
static void print_ota_test(void);
#endif
static int boot0_check_uart_input(void);

extern int load_boot1(void);

extern void set_debugmode_flag(void);

int uboot_workmode_usbback;

void __attribute__((weak)) bias_calibration(void)
{
	return;
}
/*******************************************************************************
*函数名称: Boot0_C_part
*函数原型：void Boot0_C_part( void )
*函数功能: Boot0中用C语言编写的部分的主流程
*入口参数: void
*返 回 值: void
*备    注:
*******************************************************************************/
void main( void )
{
	__u32 status;
	__s32 dram_size;
	int   ddr_aotu_scan = 0;
#ifdef CONFIG_BOOT_A15
	special_gpio_cfg a15_power_gpio;	//a15 extern power enabel gpio
#endif
    __u32 fel_flag;
	__u32 boot_cpu=0;

	bias_calibration();
    timer_init();
    sunxi_serial_init( BT0_head.prvt_head.uart_port, (void *)BT0_head.prvt_head.uart_ctrl, 6 );
        set_debugmode_flag();
	if( BT0_head.prvt_head.enable_jtag )
    {
    	boot_set_gpio((normal_gpio_cfg *)BT0_head.prvt_head.jtag_gpio, 6, 1);
    }
	printf("HELLO! BOOT0 is starting!\n");
	print_version();
        print_commit_log();
	boot0_check_uart_input();
#ifdef	SUNXI_OTA_TEST
	print_ota_test();
#endif

#ifdef CONFIG_ARCH_SUN7I
	reset_cpux(1);
#endif
    fel_flag = rtc_region_probe_fel_flag();
    if(fel_flag == SUNXI_RUN_EFEX_FLAG)
    {
        rtc_region_clear_fel_flag();
    	printf("eraly jump fel\n");

    	goto __boot0_entry_err0;
    } else if (fel_flag == 0xADAD) {
		uboot_workmode_usbback = 1;
		printf("change to usb back\n");
		rtc_region_clear_fel_flag();
	}
#ifdef CONFIG_BOOT_A15
//	printf("BT0_head.boot_head.boot_cpu=0x%x\n", BT0_head.boot_head.boot_cpu);
//	if(BT0_head.boot_head.boot_cpu)
//	{
//		fel_flag = BOOT_A7_FLAG;
//	}
//	else
//	{
//		fel_flag = BOOT_A15_FLAG;
//	}
/*
    boot_cpu  含义

	bit0~7                       bit8~15

	0:不需要保存标志位           1:当前应该切换a15启动
	1:通知u-boot保存             0:当前应该切换a7启动

	每次从brom读取的boot_cpu只能是0x100或者0
*/
	boot_cpu = BT0_head.boot_head.boot_cpu;
	a15_power_gpio = BT0_head.boot_head.a15_power_gpio;
	if(fel_flag == BOOT_A15_FLAG)
	{
		rtc_region_clear_fel_flag();
		if(boot_cpu == 0x00)    //如果原本是a7启动
			boot_cpu = 0x101;   //a15启动，需要保存标志位

		switch_to_a15(a15_power_gpio);
	}
	else if(fel_flag == BOOT_A7_FLAG)
	{
		rtc_region_clear_fel_flag();
		if(boot_cpu == 0x100)      //如果原本是a15启动
			boot_cpu = 0x01;       //a7启动，需要保存标志位
	}
	else
	{
		if(boot_cpu == 0x100)
		{
			switch_to_a15(a15_power_gpio);                //a15启动，不需要保存标志位
		}
		else
		{
			boot_cpu = 0x0;    //a7启动，不需要保存标志位
		}
	}
//  printf("BT0_head.boot_head.boot_cpu=0x%x\n", BT0_head.boot_head.boot_cpu);
#endif
	mmu_setup();

    ddr_aotu_scan = 0;
	dram_size = init_DRAM(ddr_aotu_scan, (void *)BT0_head.prvt_head.dram_para);
	if(dram_size)
	{
	    //mdfs_save_value();
		printf("dram size =%d\n", dram_size);
	}
	else
	{
		printf("initializing SDRAM Fail.\n");

		goto  __boot0_entry_err;
	}
#if defined(CONFIG_ARCH_SUN9IW1P1)
	__msdelay(100);
#endif

#ifdef CONFIG_ARCH_SUN7I
    check_super_standby_flag();
#endif

	status = load_boot1();

	printf("Ready to disable icache.\n");

	mmu_turn_off( );                               // disable instruction cache

	if( status == 0 )
	{
		//跳转之前，把所有的dram参数写到boot1中
		set_dram_para((void *)&BT0_head.prvt_head.dram_para, dram_size, boot_cpu);
		if(uboot_workmode_usbback) {
			struct spare_boot_head_t  *bfh = (struct spare_boot_head_t *) CONFIG_SYS_TEXT_BASE;

			bfh->boot_data.work_mode = WORK_MODE_USB_PRODUCT;
		}

		printf("Jump to secend Boot.\n");

		boot0_jump(CONFIG_SYS_TEXT_BASE);		  // 如果载入Boot1成功，跳转到Boot1处执行
	}

__boot0_entry_err:
#ifdef CONFIG_BOOT_A15
	if(!(boot_cpu & 0xff00))
	{
		boot0_clear_env();

		boot0_jump(FEL_BASE);
	}
	else
	{
		rtc_region_set_flag(SUNXI_RUN_EFEX_FLAG);
		boot0_clear_env();

		watchdog_enable();
	}
#endif
__boot0_entry_err0:
	boot0_clear_env();

	boot0_jump(FEL_BASE);
}
/*
************************************************************************************************************
*
*                                             function
*
*    name          :
*
*    parmeters     :
*
*    return        :
*
*    note          :
*
*
************************************************************************************************************
*/
static void print_version(void)
{

	printf("boot0 version : %s\n", BT0_head.boot_head.platform + 2);

	return;
}

extern char boot0_hash_value[64];
static void print_commit_log(void)
{
        printf("boot0 commit : %s \n",boot0_hash_value);

        return ;
}
/*
************************************************************************************************************
*
*                                             function
*
*    name          :
*
*    parmeters     :
*
*    return        :
*
*    note          :
*
*
************************************************************************************************************
*/
static int boot0_clear_env(void)
{

	reset_pll();
	mmu_turn_off();

	__msdelay(10);

	return 0;
}


/*
************************************************************************************************************
*
*                                             function
*
*    name          :
*
*    parmeters     :
*
*    return        :
*
*    note          :
*
*
************************************************************************************************************
*/
#ifdef	SUNXI_OTA_TEST
static void print_ota_test(void)
{
	printf("*********************************************\n");
	printf("*********************************************\n");
	printf("*********************************************\n");
	printf("*********************************************\n");
	printf("********[OTA TEST]:update boot0 sucess*******\n");
	printf("*********************************************\n");
	printf("*********************************************\n");
	printf("*********************************************\n");
	printf("*********************************************\n");
	return;
}
#endif

static int boot0_check_uart_input(void)
{
        int c = 0;
        int i = 0;
        for(i = 0;i < 3;i++)
        {
                __msdelay(10);
                if(sunxi_serial_tstc())
                {
                        c = sunxi_serial_getc();
                        break;
                }
        }

        if(c == '2')
        {
		printf("enter 0x%x,ready jump to fes\n", c-0x30);  // ASCII to decimal digit
		boot0_clear_env();
		boot0_jump(FEL_BASE);
        }
        return 0;
}
