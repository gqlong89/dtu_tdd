
#include "cmsis_os.h"                                           // CMSIS RTOS header file
#include "osObjects.h"                      // RTOS object definitions
#include "gprs.h"
#include "sdhError.h"
#include "dtuConfig.h"
#include "string.h"
#include "debug.h"
#include "TTextConfProt.h"
#include "times.h"
#include "led.h"
#include "dtu.h"

#include "modbusRTU_cli.h"
//#include "adc.h"
/*----------------------------------------------------------------------------
 *      Thread 1 'Thread_Name': Sample thread
 *---------------------------------------------------------------------------*/
#define DTU_BUF_LEN		256
#define TMP_BUF_LEN		64


#define NEW_CODE			1

//static void dtu_conf(void);
//$Id$

void thrd_dtu (void const *argument);                             // thread function
osThreadId tid_ThrdDtu;                                          // thread id
osThreadDef (thrd_dtu, osPriorityNormal, 1, 0);                   // thread object

gprs_t *SIM800 ;

char	DTU_Buf[DTU_BUF_LEN];
static void Config_ack( char *data, void *arg);

#define TTEXTSRC_485 0
#define TTEXTSRC_SMS(n)	( n + 1)
static int TText_source = TTEXTSRC_485;	



StateContext *MyContext;

static void prnt_485( char *data)
{
	if( Dtu_config.output_mode)
	{
		
		s485_Uart_write(data, strlen(data) );
	}
	
	DPRINTF(" %s \n", data);
	
}

int Init_ThrdDtu (void) {
	
#ifdef NEW_CODE	
	gprs_t *sim800 ;
	DtuContextFactory* factory = DCFctGetInstance();
	
	MyContext = factory->createContext( Dtu_config.work_mode);
	
	if( MyContext == NULL)
		return 0;
	
	prnt_485("gprs threat startup ! \r\n");
	if( NEED_GPRS( Dtu_config.work_mode)) 
	{
		sim800 = GprsGetInstance();
		sim800->init( sim800);
		if( Dtu_config.multiCent_mode == 0)
		{
			
			Grps_SetCipmode( CIPMODE_TRSP);
			Grps_SetCipmux(0);
		}
		sim800->startup(sim800);
	}
	
	MyContext->init( MyContext, DTU_Buf, DTU_BUF_LEN);
	MyContext->initState( MyContext);
	
#else	
	//使用用户的配置来重新启动485串口
	SIM800 = GprsGetInstance();
	s485_uart_init( &Dtu_config.the_485cfg, NULL);
//	s485_uart_init( &Conf_S485Usart_default, NULL);

	s485_Uart_ioctl(S485_UART_CMD_SET_RXBLOCK);
	s485_Uart_ioctl(S485UART_SET_RXWAITTIME_MS, 200);
	s485_Uart_ioctl(S485_UART_CMD_SET_TXBLOCK);
	s485_Uart_ioctl(S485UART_SET_TXWAITTIME_MS, 200);
#endif	

	tid_ThrdDtu = osThreadCreate (osThread(thrd_dtu), NULL);
	if (!tid_ThrdDtu) return(-1);

	return(0);
}





void thrd_dtu (void const *argument) {
	short step = 0;
	short cnnt_seq = 0;
	int ret = 0;
	int lszie = 0;
	short i = 0;
//	char ser_confmode = 0;
	char count = 0;
	void *gprs_event;
	int retry = 20;
	sprintf(DTU_Buf, "starting up gprs ...");
	
#ifdef NEW_CODE		
	while(1)
	{
		threadActive();
		MyContext->curState->run( MyContext->curState, MyContext);
		osThreadYield();         
		
	}
	
#else	
	
	prnt_485( DTU_Buf);
	if( Dtu_config.work_mode != MODE_LOCALRTU)
	{
		while(retry)
		{
			
			if( SIM800->check_simCard(SIM800) == ERR_OK)
			{	
				sprintf(DTU_Buf, "succeed !\n");
				break;
			}
			else {
				SIM800->startup(SIM800);
				retry --;
				osDelay(1000);
			}
			
		}
	}	
	else
	{
		step = 4 ;
	}
	
	

	
	
	

	sprintf(DTU_Buf, "Begain DTU thread ...");
	prnt_485( DTU_Buf);
	

	while (1) {
		switch( step)
		{
			
			case 0:
				if( SIM800->check_simCard(SIM800) == ERR_OK)
				{
					sprintf(DTU_Buf, "detected sim succeed! ...");
					prnt_485( DTU_Buf);
					step += 2;
					
				}
				else 
				{
					step ++;
					
				}
				
				
				break;
			case 1:
				SIM800->startup(SIM800);
				step = 0;
				break;
			case 2:
				
				
				sprintf(DTU_Buf, "cnnnect DC :%d,%s,%d,%s ...", cnnt_seq,Dtu_config.DateCenter_ip[ cnnt_seq],\
								Dtu_config.DateCenter_port[cnnt_seq],Dtu_config.protocol[cnnt_seq] );
				prnt_485( DTU_Buf);
				ret = SIM800->tcpip_cnnt( SIM800, cnnt_seq,Dtu_config.protocol[cnnt_seq], Dtu_config.DateCenter_ip[cnnt_seq], Dtu_config.DateCenter_port[cnnt_seq]);
				
				if( ret == ERR_OK)
				{
					while(1)
					{
						ret = SIM800->sendto_tcp( SIM800, cnnt_seq, Dtu_config.registry_package, strlen(Dtu_config.registry_package) );
						if( ret == ERR_OK)
						{
							//启动心跳包的闹钟
							set_alarmclock_s( ALARM_GPRSLINK(cnnt_seq), Dtu_config.hartbeat_timespan_s);
							prnt_485("succeed !\n");
							if( Dtu_config.multiCent_mode == 0)
								step ++;
							break;
							
						}
						
						
						if( ret == ERR_UNINITIALIZED )
						{
							prnt_485("can not send data !\n");
							break;
						}
					}
					
					
				}
				else
				{
					prnt_485(" failed !\n");
				}
				cnnt_seq ++;
				if( cnnt_seq >= IPMUX_NUM)
				{
					step ++;
					cnnt_seq = 0;
				}
				break;
			case 3:
				
				//读取一条短信
			
				memset( DtuTempBuf, 0, sizeof( DtuTempBuf));
				ret = SIM800->read_phnNmbr_TextSMS( SIM800, DtuTempBuf, DTU_Buf,  DTU_Buf, &lszie);				
				if( ret >= 0)
				{
					for( i = 0; i < ADMIN_PHNOE_NUM; i ++)
					{
						if( compare_phoneNO( DtuTempBuf, Dtu_config.admin_Phone[i]) == 0)
						{
							TText_source = TTEXTSRC_SMS( i);
							Config_server( DTU_Buf, Config_ack, &TText_source);
							if( Dtu_config.work_mode == MODE_SMS)
								s485_Uart_write(DTU_Buf, lszie);
							break;
						}
						
					}
					SIM800->delete_sms( SIM800, ret);
				}
				
			
			
				//处理所有捕获到的事件
				while(1)
				{
					lszie = DTU_BUF_LEN;
					ret = SIM800->report_event( SIM800, &gprs_event, DTU_Buf, &lszie);
					if( ret != ERR_OK)
					{	
						step ++;
						break;
					}
					ret = SIM800->deal_tcprecv_event( SIM800, gprs_event, DTU_Buf,  &lszie);
					if( ret >= 0)
					{
						//接收到数据就将闹钟的起始时间设置为当前时间
						set_alarmclock_s( ALARM_GPRSLINK(ret), Dtu_config.hartbeat_timespan_s);
						sprintf( DtuTempBuf, "TCP[%d] recv %d Byte, event %p!", ret, lszie, gprs_event);
						prnt_485( DtuTempBuf);
						if( Dtu_config.work_mode == MODE_REMOTERTU)
						{
							if( modbusRTU_getID( (uint8_t *)DTU_Buf) == Dtu_config.rtu_addr)
							{
								lszie = modbusRTU_data( (uint8_t *)DTU_Buf, lszie, (uint8_t *)DtuTempBuf, sizeof( DtuTempBuf));
								SIM800->sendto_tcp( SIM800, ret, DtuTempBuf, lszie);
							}
						}
						else
						{
							s485_Uart_write( DTU_Buf, lszie);

						}
						
						
//						prnt_485(DTU_Buf);
						SIM800->free_event( SIM800, gprs_event);
						continue;
					}
					
					lszie = DTU_BUF_LEN;
					memset( DtuTempBuf, 0, sizeof( DtuTempBuf));
					ret = SIM800->deal_smsrecv_event( SIM800, gprs_event, DTU_Buf,  &lszie, DtuTempBuf);				
					if( ret > 0)
					{
						for( i = 0; i < ADMIN_PHNOE_NUM; i ++)
						{
							if( compare_phoneNO( DtuTempBuf, Dtu_config.admin_Phone[i]) == 0)
							{
								TText_source = TTEXTSRC_SMS( i);
								Config_server( DTU_Buf, Config_ack, &TText_source);
								if( Dtu_config.work_mode == MODE_SMS)
									s485_Uart_write(DTU_Buf, lszie);
								break;
							}
							
						}
						SIM800->delete_sms( SIM800, ret);
						SIM800->free_event( SIM800, gprs_event);
						continue;
					}
					
					ret = SIM800->deal_tcpclose_event( SIM800, gprs_event);
					if( ret >= 0)
					{
						sprintf( DTU_Buf, "tcp close : %d ", ret);
						prnt_485( DTU_Buf);
					}
					SIM800->free_event( SIM800, gprs_event);
				
				}	//while(1)
				
				break;
			case 4:
				//MODE_LOCALRTU 之下不需要执行其他步骤，只需要读取485的数据并处理即可
				ret = s485_Uart_read( DTU_Buf, DTU_BUF_LEN);
				
				
				if( ret <= 0)
				{
					if( Dtu_config.work_mode != MODE_LOCALRTU)
						step++;
					break;
				}
				
				if( Dtu_config.work_mode == MODE_LOCALRTU || Dtu_config.work_mode == MODE_REMOTERTU)
				{
					if( modbusRTU_getID( (uint8_t *)DTU_Buf) != Dtu_config.rtu_addr)
						break;
					lszie = modbusRTU_data( (uint8_t *)DTU_Buf, ret, (uint8_t *)DtuTempBuf, sizeof( DtuTempBuf));
					s485_Uart_write( DtuTempBuf, lszie);
					
					break;

				}
				for( i = 0; i < IPMUX_NUM; i ++)
				{
					SIM800->sendto_tcp( SIM800, i, DTU_Buf, ret);
					
				}		
				///发生了tcpip数据之后不能立即发送短信，所以延迟一会再发短信
				if(  Dtu_config.work_mode == MODE_SMS)	
				{
					osDelay(2000);
				}					
				if(  Dtu_config.work_mode == MODE_SMS)
				{
					for( i = 0; i < ADMIN_PHNOE_NUM; )
					{
						if( SIM800->send_text_sms( SIM800, Dtu_config.admin_Phone[i], DTU_Buf) == ERR_FAIL)
						{
							count ++;
							if( count > 3)	//重试3次
							{
								i ++;
								count = 0;
							}
							
						}
						else
						{
							i ++;
							count = 0;
						}
//						osDelay(2000);
					}
					
					
				}
				
				step++;
			case 5:
				for( i = 0; i < IPMUX_NUM; i ++)
				{
					if( Ringing(ALARM_GPRSLINK(i)) == ERR_OK)
					{
						set_alarmclock_s( ALARM_GPRSLINK(i), Dtu_config.hartbeat_timespan_s);
						
						SIM800->sendto_tcp( SIM800, i, Dtu_config.heatbeat_package, strlen( Dtu_config.heatbeat_package));
							
						
						
					}	
				}
				step++;
				break;
			case 6:
				if( Dtu_config.multiCent_mode == 0)
				{
					ret = SIM800->get_firstCnt_seq(SIM800);
					
					step = 0;
					if( ret >= 0)
					{
						step = 3;
					}
					else
					{
						
						strcpy(DTU_Buf, "None connnect, reconnect...");
						prnt_485( DTU_Buf);
					}
					break;
				}
				else 
				{
					
					ret = SIM800->get_firstDiscnt_seq(SIM800);
					if( ret >= 0)
					{
						sprintf(DTU_Buf, "cnnnect DC :%d,%s,%d,%s ...", ret,Dtu_config.DateCenter_ip[ ret],\
									Dtu_config.DateCenter_port[ret],Dtu_config.protocol[ret] );
						prnt_485( DTU_Buf);
						if( SIM800->tcpip_cnnt( SIM800, ret, Dtu_config.protocol[cnnt_seq], Dtu_config.DateCenter_ip[ret], Dtu_config.DateCenter_port[ret]) == ERR_OK)
						{
							prnt_485(" succeed !\n");
						
						}
						else
						{
							prnt_485(" failed !\n");
						}	
					}
					step = 3;
					
				}
			
				
				break;
			default:
				step = 0;
				break;
			  
	  }
	  
	  osThreadYield();                                           // suspend thread
	}
	
#endif
}

static void Config_ack( char *data, void *arg)
{
	int source = *(int *)arg;
	SIM800->send_text_sms( SIM800, Dtu_config.admin_Phone[source - 1], data);
}

