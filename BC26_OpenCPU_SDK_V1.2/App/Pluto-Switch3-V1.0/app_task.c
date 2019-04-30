/*
 * app_task.c
 *
 *  Created on: 2016年3月18日
 *      Author: lort
 */

#include "osCore.h"
#include "pluto_entry.h"
#include "pluto.h"
#include "cJSON.h"
#include "aID.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_lamp.h"

static void recieve_message(address_t *src, uint8 seq, command_t *cmd,uint8 *pdata,uint32 len);
static void set_on_off(uint8 port, uint8 state);
static uint8 read_on_off(uint8 port);
static void factory_reset(int sig, void *arg, int len);
static void app_login_state_cb (uint8 state);
static void app_task(int sig,void *arg,int len);

typedef enum
{
	app_sig_keyscan = 1,
	app_sig_uartRead,
	app_sig_wifi_reconnect
}app_task_e;

const uint32 device_attribute_id[] = {
	aID_PDO_Type_Device_Info,					 //
	aID_PDO_Type_Port_List,
	aID_PDO_Type_Port_Describe,
	aID_PDO_Type_Scene,
	aID_PDO_Type_File,
	aID_PDO_Type_White_Table,
	aID_PDO_Type_Alarm_Record,
	aID_PDO_Type_History_Record,
	aID_PDO_Type_Beacon,
	aID_PDO_Type_Device_Indication,
	aID_PDO_Type_Upgrade,
	aID_Gen_Type_LQI,
};
#define DEVICE_ATTRIBUTE_ID_NUM	(sizeof(device_attribute_id)/sizeof(device_attribute_id[0]))
const device_describe_t device_describe =
{
		.version = 1,
		.MFG = "2017-11-8 15:00:00",
		.bin_name = "user1.4096.new.4.bin",
		.device_name = "Switch",
		.device_type = dev_type_nbiot_normal,
		.guestLock = osFalse,
		.shareLock = osTrue,//not share device by default
		.zone = 8,
		.aIDNum = DEVICE_ATTRIBUTE_ID_NUM,
		.aID = (uint32*)device_attribute_id,
};
const uint32 port_attribute_id[] = {aID_Gen_Type_Switch,aID_Gen_Type_LQI};
#define PORT_ATTRIBUTE_ID_NUM	(sizeof(port_attribute_id)/sizeof(port_attribute_id[0]))

const uint8 test_sn[]={0x14,0xF7,0x90,0x5E,0x85,0x98,0xE5,0x39,0x90,0xDA,0xD1,0x9C,0x0E,0x57,0x95,0xE4};
const uint8 test_addr[]={0x01,0x56,0x05,0x00,0x01,0x00,0x00,0x02};

const pdo_server_t SnInfo= {
		.product_ID=1,//uint32	product_ID;//产品种类编码，HA类产品编码为1
		.vendor_ID = 1,//uint32  vendor_ID;
		.product_date="2018-07-06",//char	*product_date;
		.password="123456",//char	*password;
		.sn = (uint8*)test_sn,//uint8 	*sn;
		.addr = (uint8*)test_addr,//uint8	*addr;
		.url = "www.glalaxy.com",//char	*url;
		.server_ipv4="119.23.8.181",//char	*server_ipv4;
		.server_udp_port=16729,//uint16	server_udp_port;
		.server_tcp_port = 16729,//uint16	server_tcp_port;
		.local_udp_port = 16729,//uint16	local_udp_port;
		.version = 1 //uint16	version;//产品初始版本
};
void  app_task_init(void)
{
	aps_listener_t	mylisten;
	hal_key_init();
	hal_lamp_init();
	hal_led_init();
	hal_led_blink(0x7FFFFFFF,100,100);
	//factory_init();
	//pdo_write_server((pdo_server_t*)&SnInfo);
	pluto_init();
	mylisten.recieve_msg = recieve_message;
	mylisten.send_status = NULL;
	aps_set_listener(&mylisten);
    login_set_state_change_cb(app_login_state_cb);
	osStartReloadTimer(20,app_task,app_sig_keyscan,NULL);
	pluto_update_guest_key(osFalse);
}
extern void login_check(void);
static void  app_task(int sig,void *arg,int len)
{
	command_t 	cmd;
	uint8		state = 0x01;
	static 		uint8 flag = 0;
	uint8 		key,shift;
	switch(sig)
	{
	case app_sig_wifi_reconnect:
		osDeleteTimer(app_task,app_sig_wifi_reconnect);
		break;
	case app_sig_keyscan:
		shift = hal_get_key(&key);
		if(shift==HAL_KEY_DOWN)
		{
			cmd.aID 	= aID_Gen_Type_Switch;
			cmd.cmd_id 	= cmd_return;
			cmd.option = 0x00;
			flag = 0;
			if(key==0x04)
			{
				hal_lamp_tolgle(LAMP2);
				state = hal_lamp_get(LAMP2);
				aps_req_report_command(0x03,pluto_get_seq(),&cmd,&state,1,option_default);
			}
			else if(key==0x02)
			{
				hal_lamp_tolgle(LAMP1);
				state = hal_lamp_get(LAMP1);
				aps_req_report_command(0x02,pluto_get_seq(),&cmd,&state,1,option_default);
			}
			else if(key==0x01)
			{
				hal_lamp_tolgle(LAMP0);
				state = hal_lamp_get(LAMP0);
				aps_req_report_command(0x01,pluto_get_seq(),&cmd,&state,1,option_default);
			}
		}
		if(shift==HAL_KEY_KEEP)
		{
			if(hal_key_keeptime()>4000)
			{
				if(flag==0)
				{
					flag = 1;
					hal_led_blink(4,200,200);
					osStartTimer(2000,factory_reset,1,NULL);
				}
			}
		}
	break;
	}
}

static void recieve_message(address_t *src, uint8 seq, command_t *cmd,uint8 *pdata,uint32 len)
{
	uint8 state;
	uint8 cmd_id = cmd->cmd_id;
	osLogI(1,"recieverMsg port:%d, keyID:%02x,cmd:%02x ,aID:%08x ",src->port,src->keyID,cmd->cmd_id,cmd->aID);
	osLogB(1,"recieverMsg from : ",src->addr,8);
	if(cmd->aID==aID_Gen_Type_Switch)
	{
		cmd->cmd_id |= cmd_return;
		if(cmd_id==cmd_write)
		{
			set_on_off(src->port,pdata[0]);
			aps_req_report_command(src->port,seq,cmd,pdata,1,option_default);
		}
		else if(cmd_id==cmd_read)
		{
			state = read_on_off(src->port);
			aps_req_send_command(src,seq,cmd,&state,1,option_default);
		}
	}
	else if(cmd->aID==aID_Gen_Type_LQI)
	{
		cmd->cmd_id |= cmd_return;
		if(cmd_id==cmd_read)
		{
			state = pluto_get_device_lqi();
			aps_req_send_command(src,seq,cmd,&state,1,option_default);
		}
	}
}
static void  set_on_off(uint8 port, uint8 state)
{
	switch(port)
	{
	case 0x01:
		hal_lamp_set(LAMP0,state);
		break;
	case 0x02:
		hal_lamp_set(LAMP1,state);
		break;
	case 0x03:
		hal_lamp_set(LAMP2,state);
		break;
	default:
		return ;
	}
}
static uint8  read_on_off(uint8 port)
{
	uint8 state = 0x00;
	switch(port)
	{
	case 0x01:
		state = hal_lamp_get(LAMP0);
		break;
	case 0x02:
		state = hal_lamp_get(LAMP1);
		break;
	case 0x03:
		state = hal_lamp_get(LAMP2);
		break;
	}
	return state;
}
static void  factory_reset(int sig, void *arg, int len)
{
	osPrintf(1,"app_task: fs_diskFormat ... \r\n !");
	pluto_disk_format();
	osPrintf(1,"app_task: fs_diskFormat finished !\r\n !");
}
static void  app_login_state_cb (uint8 state)
{
	static uint8 online_flag = osFalse;
	osLogI(1,"app_login_state_cb: %d \r\n",state);
	switch(state)
	{
	case login_state_stop:
	case login_state_start:
	case login_state_logout_failed:
	case login_state_login_failed:
		pluto_registe_led(NULL);
		hal_led_blink(0x0FFFFFFF,1000,1000);
		break;
	case login_state_online:
		pluto_registe_led(hal_led_blink);
		hal_led_blink(1,10,0);
		hal_led_set(LED_STATUS_OFF);
		if(online_flag==osFalse)
		{
			online_flag = osTrue;
			pdo_set_device_describe((device_describe_t*)&device_describe);
			pdo_register_port(1,Application_ID_Switch,(uint32*)port_attribute_id,PORT_ATTRIBUTE_ID_NUM,osFalse);
			pdo_register_port(2,Application_ID_Switch,(uint32*)port_attribute_id,PORT_ATTRIBUTE_ID_NUM,osFalse);
			pdo_register_port(3,Application_ID_Switch,(uint32*)port_attribute_id,PORT_ATTRIBUTE_ID_NUM,osFalse);
		}
		break;
	case login_state_offline:
		pluto_registe_led(NULL);
		hal_led_blink(0x0FFFFFFF,1000,1000);
		if(online_flag==osTrue)
		{
			//wifi_station_disconnect();
			//osStartReloadTimer(5000,app_task,app_sig_wifi_reconnect,NULL);
			online_flag = osFalse;
		}
		break;
	}
}

