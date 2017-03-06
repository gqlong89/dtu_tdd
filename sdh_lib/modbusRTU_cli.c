/**
* @file 		modbusRTU_cli.c
* @brief		实现modbusRTU从站功能.
* @details	由寄存器的访问部分和协议解析两部分组成.
* @author		author
* @date		date
* @version	A001
* @par Copyright (c): 
* 		XXX??
* @par History:         
*	version: author, date, desc\n
*	V0.1:sundh,17-01-15,创建实现0和3号命令
*/
#include "modbusRTU_cli.h"
#include "sdhError.h"
#include "stdint.h"
#include "string.h"
#include "sdhError.h"
#include "string.h"
//uint16_t coil_buf[COIL_SIZE/16];
//static uint16_t state_buf[STATE_SIZE/16];			//2区数据内存
uint16_t input_buf[INPUT_SIZE];
uint16_t hold_buf[HOLD_SIZE];

//uint16_t *COIL_ADDRESS=coil_buf;
//uint16_t *STATE_ADDRESS=state_buf;
uint16_t *INPUT_ADDRESS=input_buf;
uint16_t *HOLD_ADDRESS=hold_buf;

static Reg3_write_cb	g_rg3_wr_cb = NULL;

#ifdef CPU_LITTLE_END
static void Little_end_to_Big_end( uint16_t *p_val16)
{
	uint8_t u16_h = ( *p_val16 ) >> 8;
	uint8_t u16_l =  *p_val16 & 0xff;
	
	*p_val16 = 0;
	*p_val16 = ( u16_l << 8) | u16_h;
}

static void Big_end_to_Little_end( uint16_t *p_val16)
{
	uint8_t u16_h = ( *p_val16 ) ;
	uint8_t u16_l =  *p_val16 >> 8;
	
	*p_val16 = 0;
	*p_val16 = ( u16_h << 8) | u16_l;
}
#endif

/* 寄存器访问接口			*/
#if 0
uint16_t regTyppe2_read(uint16_t addr, uint16_t reg_type)
{
	uint16_t i,j;

	if(reg_type == REG_MODBUS)
	{
		if( addr > 10000)
			addr-=10001;
	}
	 
	i = addr/16;
	j = addr%16;
    
	i = *(STATE_ADDRESS+i) & (1<<j);
	
	return (i!=0);
	
}

int regType2_write(uint16_t addr, uint16_t reg_type, int val)
{
	return ERR_OK;
}


#endif

int Regist_reg3_wrcb( Reg3_write_cb cb)
{
	g_rg3_wr_cb = cb;
	return ERR_OK;
}
uint16_t regType3_read(uint16_t hold_address, uint16_t reg_type)
{
	uint16_t tmp;
	if(reg_type==REG_MODBUS)
	{
		if( hold_address > 40000)
			hold_address-=40001;
		tmp = *(HOLD_ADDRESS + hold_address);
#ifdef CPU_LITTLE_END
		Little_end_to_Big_end( &tmp);
#endif	
	}
	else
	{
		tmp = *(HOLD_ADDRESS + hold_address);
	}

	return tmp;
}

uint16_t regType3_write(uint16_t hold_address, uint16_t reg_type, uint16_t val)
{
	char chn_flag = 0;
	uint16_t tmp = val;
	if(reg_type==REG_MODBUS)
	{
		if( hold_address > 40000)
			hold_address-=40001;
#ifdef CPU_LITTLE_END
		Big_end_to_Little_end( &tmp);
#endif
		
		if( *(HOLD_ADDRESS + hold_address) != tmp)
			chn_flag = 1;
	}
	else
	{

	}
	
	

	*(HOLD_ADDRESS + hold_address) = tmp;
	if( g_rg3_wr_cb && chn_flag )
		g_rg3_wr_cb();
	return ERR_OK;
}


uint16_t regType4_read(uint16_t input_address, uint16_t reg_type)
{
	
	uint16_t tmp;
	if(reg_type==REG_MODBUS)
	{
		if( input_address > 30000)
			input_address-=30001;
		
		tmp = *(INPUT_ADDRESS + input_address);
#ifdef CPU_LITTLE_END
		Little_end_to_Big_end( &tmp);
#endif	
	}
	else
	{
		tmp = *(INPUT_ADDRESS + input_address);
		
	}
	  
	return tmp;
}

uint16_t regType4_write(uint16_t input_address, uint16_t reg_type, uint16_t val)
{
	uint16_t tmp = val;
	if(reg_type==REG_MODBUS)
		return ERR_FAIL;

	*(INPUT_ADDRESS + input_address) = tmp;
	return ERR_OK;
}


/*	modbus 协议解析			*/
uint8_t 	modbusRTU_getID(uint8_t *command_buf)
{
	return command_buf[0];
}
uint16_t modbusRTU_data(uint8_t *command_buf, int cmd_len, uint8_t *ack_buf, int ackbuf_len)
{
	uint16_t i, data_start, data_num, data, ack_num;
	uint16_t crc16 = 0;
	uint16_t *p_playload = NULL;
	uint8_t  err = 0;
	for(i=0; i<6; i++) 
	{
		ack_buf[i] = command_buf[i];											//?????
	}	
	crc16 = CRC16( 	command_buf, 	( cmd_len - 2));
	//todo 协议上规定CRC是低字节在前，然而实际测试中却是高字节在前，需要再次确认（测试软件是ModbuScan）
	data = command_buf[ cmd_len - 1 ] + ( command_buf[ cmd_len - 2] << 8);		//read crc 
	if( crc16 != data)
	{
		err = MB_DATA_ERR;
	}
	else
	{
		data_start = command_buf[3] + (command_buf[2] << 8);						//??????
		data_num = command_buf[5] + (command_buf[4] << 8);							//???????
		switch(command_buf[1])
		{
			/*读取多Coil状态 读0区			-------------------------------------------*/
			case READ_COIL:		//无效操作码
				err=MB_CMD_ERR;
				break;
			/*读书如状态 读1区----------------------------------------------------------------------*/
			case READ_STATE:														
				
				break;

			/*读输入寄存器 3区------------------------------------------------------------------*/
			case READ_INPUT:	
																					//data_num=127;	  ??????
				if((data_start >= INPUT_SIZE) || (data_num >= 125) || ((data_start + data_num) > INPUT_SIZE) ) 
				{
					err= MB_ADDR_ERR;	
					break;
				}
							
				ack_buf[2] = data_num*2;
				ack_num = 3;
				for(i=0; i<data_num; i++)
				{
					data = regType4_read(data_start, REG_MODBUS);	
					data_start++;
						
					ack_buf[ack_num] = data; 			
					ack_num++; 
					ack_buf[ack_num] = data >>8;				
					ack_num++;	
				}
			break;
		/*读输入寄存器 4区，保持寄存器------------------------------------------------------------------*/
			case READ_HOLD:	
																					//data_num=127;	  ??????
				if((data_start >= HOLD_SIZE) || (data_num >= 125) || ((data_start + data_num) > HOLD_SIZE) ) 
				{
					err = MB_ADDR_ERR;	
					break;
				}
							
				ack_buf[2] = data_num*2;
				ack_num = 3;
				for(i=0; i<data_num; i++)
				{
					data = regType3_read(data_start, REG_MODBUS);	
					data_start++;
						
					ack_buf[ack_num] = data; 			
					ack_num++; 
					ack_buf[ack_num] = data >>8;				
					ack_num++;	
				}
			break;
			/*写输入寄存器 4区，保持寄存器------------------------------------------------------------------*/
			case WRITE_1_HOLD:	
																					//data_num=127;	  ??????
				if((data_start >= HOLD_SIZE) || (data_num >= 125) || ((data_start + data_num) > HOLD_SIZE) ) 
				{
					err = MB_ADDR_ERR;	
					break;
				}
							
				p_playload = (uint16_t *)(command_buf + 4);
				regType3_write(data_start, REG_MODBUS, *p_playload);
				data = regType3_read(data_start, REG_MODBUS);
				ack_num = 4;
				ack_buf[ack_num] = data; 			
				ack_num++; 
				ack_buf[ack_num] = data >>8 ;				
				ack_num++;	
				
				break;
				
			default:
				err = MB_CMD_ERR; 
			break;
		}
	}
	
	//todo:将crc校验码也要加在结尾处	
	if (err )
    {
		ack_buf[1] = ack_buf[1] | 0x80;											 //无效操作码
		ack_buf[2] = err;            
		ack_num = 3;
	}	
	
	
	crc16 = CRC16( 	ack_buf, 	ack_num);
	ack_buf[ack_num] = crc16 >>8; 	
	ack_num++;
	ack_buf[ack_num] = crc16 ; 	
	ack_num ++;	
	return ack_num;
	
}


/*16?CRC???----------------------------------------------------------------------------*/
const uint8_t auchCRCHi[]={
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,

0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40
};
const uint8_t auchCRCLo[]={
0x00,0xc0,0xc1,0x01,0xc3,0x03,0x02,0xc2,0xc6,0x06,0x07,0xc7,0x05,0xc5,0xc4,0x04,
0xcc,0x0c,0x0d,0xcd,0x0f,0xcf,0xce,0x0e,0x0a,0xca,0xcb,0x0b,0xc9,0x09,0x08,0xc8,
0xd8,0x18,0x19,0xd9,0x1b,0xdb,0xda,0x1a,0x1e,0xde,0xdf,0x1f,0xdd,0x1d,0x1c,0xdc,
0x14,0xd4,0xd5,0x15,0xd7,0x17,0x16,0xd6,0xd2,0x12,0x13,0xd3,0x11,0xd1,0xd0,0x10,
0xf0,0x30,0x31,0xf1,0x33,0xf3,0xf2,0x32,0x36,0xf6,0xf7,0x37,0xf5,0x35,0x34,0xf4,
0x3c,0xfc,0xfd,0x3d,0xff,0x3f,0x3e,0xfe,0xfa,0x3a,0x3b,0xfb,0x39,0xf9,0xf8,0x38,
0x28,0xe8,0xe9,0x29,0xeb,0x2b,0x2a,0xea,0xee,0x2e,0x2f,0xef,0x2d,0xed,0xec,0x2c,
0xe4,0x24,0x25,0xe5,0x27,0xe7,0xe6,0x26,0x22,0xe2,0xe3,0x23,0xe1,0x21,0x20,0xe0,

0xa0,0x60,0x61,0xa1,0x63,0xa3,0xa2,0x62,0x66,0xa6,0xa7,0x67,0xa5,0x65,0x64,0xa4,
0x6c,0xac,0xad,0x6d,0xaf,0x6f,0x6e,0xae,0xaa,0x6a,0x6b,0xab,0x69,0xa9,0xa8,0x68,
0x78,0xb8,0xb9,0x79,0xbb,0x7b,0x7a,0xba,0xbe,0x7e,0x7f,0xbf,0x7d,0xbd,0xbc,0x7c,
0xb4,0x74,0x75,0xb5,0x77,0xb7,0xb6,0x76,0x72,0xb2,0xb3,0x73,0xb1,0x71,0x70,0xb0,
0x50,0x90,0x91,0x51,0x93,0x53,0x52,0x92,0x96,0x56,0x57,0x97,0x55,0x95,0x94,0x54,
0x9c,0x5c,0x5d,0x9d,0x5f,0x9f,0x9e,0x5e,0x5a,0x9a,0x9b,0x5b,0x99,0x59,0x58,0x98,
0x88,0x48,0x49,0x89,0x4b,0x8b,0x8a,0x4a,0x4e,0x8e,0x8f,0x4f,0x8d,0x4d,0x4c,0x8c,
0x44,0x84,0x85,0x45,0x87,0x47,0x46,0x86,0x82,0x42,0x43,0x83,0x41,0x81,0x80,0x40
};


/*-----------------------------------------------------------------------------------------*/
/*16?CRC?????														                   */
/*																						   */
/*-----------------------------------------------------------------------------------------*/
uint16_t CRC16(uint8_t* puchMsg, uint16_t usDataLen)
{
	uint8_t uchCRCHi=0xff;
	uint8_t uchCRCLo=0xff;
	uint16_t uIndex;

	while(usDataLen--)
	{
		uIndex=uchCRCHi^*(puchMsg++);
		uchCRCHi=uchCRCLo^auchCRCHi[uIndex];
		uchCRCLo=auchCRCLo[uIndex];
	}

	return uchCRCHi<<8|uchCRCLo;
}
