/**
* @file 		sdh_filesys.c
* @brief		一个适用与裸机活rtos的简易文件系统，为了方便存储器的管理.
* @details		存储器的被分成三部分： 管理区，存储区，保留区.管理区保存文件描述结构，以及内存页的使用情况。存储区存储数据，保留区是文件系统不会去访问的部分。
*	不支持目录结构。所有存放在flash中的文件都是平级的。
*	扇区0保存文件信息。称为特殊扇区
*	紧挨着的扇区1，或者更多的扇区用来管理存储器的内存页的使用。一个Bit代表一个页，1表示该页可以被使用，0表示该页已经被分配掉了。
*	扇区0的文件信息由3部分组成：
*	1、扇区头信息：已经创建的文件数量及版本号
*	2、文件信息存储区
*	3、文件存储区间存储区
*	提供缓存操作机制，降低存储器的操作次数.
*	消耗资源：
*	掉电存储器：2 + n个扇区,如果内存页过多无法用一个扇区来保存就怎讲内存管理扇区
*	内存：		1个扇区的大小
* @author		author
* @date		date
* @version	A001
* @par Copyright (c): 
* 		XXX公司
* @par History:         
*	version: author, date, desc

*/

#include "list.h"
#include "sw_filesys.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "stdbool.h"
#include "sdhError.h"
#include "debug.h"


static List L_File_opened;
static uint8_t	*Flash_buf;
static storageInfo_t	StrgInfo;
static fs_area			Page_Zone;
static	uint8_t		Buf_chg_flag = 0;				//缓存内容被修改的标志,缓存被写入flash时标志清零
static uint16_t	Src_sector = INVALID_SECTOR;		//缓存中的内容的扇区
static char Flash_err_flag = 0;

static	int FsErr = 0;

static bool check_bit(uint8_t *data, int bit);
static void clear_bit(uint8_t *data, int bit);
static void set_bit(uint8_t *data, int bit);
static void get_area( int pg_offset, int size, area_t *out_area);
static int page_malloc( area_t *area, int len);
static int page_free( area_t *area, int area_num);
static int read_flash( uint16_t sector);
static int flush_flash( uint16_t sector);
static file_info_t	*searchfile( uint8_t* flash_data, char *name);
static int mach_file(const void *key, const void *data)
{
	char *name = ( char *)key;
	sdhFile	*fd = (sdhFile *)data;
	return strcmp( name, fd->name);
	
	
	
}

int filesys_init(void)
{
	int		rese_pagenum;
	int		data_pagenum;			///需要被管理的数据区的页数
	int		capacity_pagenum = 0;		///可以纳入管理的页数
	int		pageuseinfo_sector = 1;		///数据页面使用信息的存储扇区

	if( STORAGE_INIT() != ERR_OK)
	{
		
		Flash_err_flag = 1;
		return ERR_FLASH_UNAVAILABLE;
		
	}
	else
		Flash_err_flag = 0;
	
	SYS_ARCH_INIT();
	STORAGE_INFO( &StrgInfo);
	StrgInfo.sector_size = StrgInfo.sector_pagenum * StrgInfo.page_size;
	StrgInfo.sector_number = StrgInfo.total_pagenum / StrgInfo.sector_pagenum;
	StrgInfo.block_size = StrgInfo.block_pagenum * StrgInfo.page_size;
	StrgInfo.block_number = StrgInfo.total_pagenum / StrgInfo.block_pagenum;
	Flash_buf = malloc( StrgInfo.sector_size);
	if( Flash_buf == NULL)
		return ERR_FLASH_UNAVAILABLE;
		
	//todo : 后继改进可以根据文件的最大数量来调整
	Page_Zone.fileinfo_sector_begin = 0;
	Page_Zone.fileinfo_sector_end = 1;
	Page_Zone.pguseinfo_sector_begin = Page_Zone.fileinfo_sector_end;
	
	rese_pagenum = RESE_STOREAGE_SIZE_KB * 1024 / StrgInfo.page_size;
	
	
	///第1个扇区用来存储文件信�
	data_pagenum = StrgInfo.total_pagenum - rese_pagenum - StrgInfo.sector_pagenum;		
	
	//找到管理内存需要几个扇区
	while(1)
	{
		capacity_pagenum = pageuseinfo_sector * StrgInfo.sector_pagenum * StrgInfo.page_size * 8;			
		data_pagenum -= pageuseinfo_sector * StrgInfo.sector_pagenum;								///
		if( capacity_pagenum < data_pagenum)
			pageuseinfo_sector ++;
		else
			break;
	}
	
	Page_Zone.pguseinfo_sector_end =  Page_Zone.pguseinfo_sector_begin + pageuseinfo_sector;
	Page_Zone.data_sector_begin = Page_Zone.pguseinfo_sector_end;
	Page_Zone.data_sector_end = Page_Zone.data_sector_begin + (data_pagenum - rese_pagenum)/StrgInfo.sector_pagenum;
	list_init( &L_File_opened, free, mach_file);
	return ERR_OK;
	
}

int filesys_close(void)
{
	
	STORAGE_CLOSE();
	
	return ERR_OK;
	
}

//从文件记录扇区0中找到指定名字的文件记录信息
sdhFile * fs_open(char *name)
{
	int ret = 0;
	file_info_t	*file_in_storage;
	sdhFile *pfd;
	storage_area_t	*src_area;
	area_t					*dest_area;	
	sup_sector_head_t	*sup_head;
	ListElmt			*ele;
	short 				j;
	short		end = 0;
	short step = 0;
	if( Flash_err_flag )
	{
		FsErr = ERR_FLASH_UNAVAILABLE;
		return (NULL);
	}
	while(1)
	{
		switch( step)
		{
			case 0:
				ele = list_get_elmt( &L_File_opened,name);		//先从已经打开的文件中查找是否已经被其他任务打开过
				if(  ele!= NULL)
				{
					pfd = list_data(ele);
					pfd->reference_count ++;
					pfd->rd_pstn[SYS_GETTID()] = 0;
					pfd->wr_pstn[SYS_GETTID()] = 0;
					return pfd;
					
				}
				step ++;
				break;
			case 1:
				ret = read_flash( Page_Zone.fileinfo_sector_begin);
				if( ret == ERR_OK)
					step ++;
				else
				{
					FsErr =  ERR_DRI_OPTFAIL;
					return NULL;
				}
			case 2:
				sup_head = ( sup_sector_head_t *)Flash_buf;
				if( strcmp( sup_head->ver, FILESYS_VER) != 0x00 )	//文件系统版本号不一致
				{
					FsErr =  ERR_FILESYS_ERROR;
					return NULL;
					
				}	
				
				file_in_storage = searchfile( Flash_buf, name);

				
				if( file_in_storage )	
				{
					//找到了文件
					pfd = (sdhFile *)malloc(sizeof( sdhFile));
					dest_area = malloc( file_in_storage->area_total * sizeof( area_t));
				
					pfd->area = dest_area;
					//把文件的存储区间赋值给文件描述符
					src_area = ( storage_area_t *)( Flash_buf + sizeof(sup_sector_head_t) + FILE_NUMBER_MAX * sizeof(file_info_t));
					end = sizeof(sup_sector_head_t) + FILE_NUMBER_MAX * sizeof(file_info_t);
					for( j = 0; j < file_in_storage->area_total ;)
					{
						if( src_area->file_id == file_in_storage->file_id)		
						{
							
							pfd->area[src_area->seq].start_pg = src_area->area.start_pg;
							pfd->area[src_area->seq].pg_number = src_area->area.pg_number;
							j ++;
						}
						

						end += sizeof(file_info_t);
						if( end > StrgInfo.sector_size)
						{
							
							//当查询的位置超过扇区的大小，还没有找到文件的存储区就认为文件是错误的
							
							free( dest_area);
							free( pfd);
							
							FsErr =  ERR_FILE_ERROR;
							return NULL;
		
						}
						src_area ++;	
						
					}				
					
					strcpy( pfd->name, name);
					pfd->reference_count = 1;
					memset( pfd->rd_pstn, 0, sizeof( pfd->rd_pstn));
					memset( pfd->wr_pstn, 0, sizeof( pfd->wr_pstn));
					step = 0;
					list_ins_next( &L_File_opened, L_File_opened.tail, pfd);
					FsErr = ERR_OK;
					return pfd;
				}
					
				FsErr =  ERR_NON_EXISTENT;
				return NULL;
		}		//switch
		
	}		//while(1)

}
int fs_get_error(void)
{
	
	return FsErr;
}
//格式化文件系统，删除所有的内容（保留区除外）
int fs_format(void)
{
	sup_sector_head_t	*sup_head;
	int ret;
	int sector = 0;
	
	//擦除整个文件系统
	while(1)
	{
		ret = flash_erase_sector(sector);
		if( ret == ERR_OK)
		{
			if( 	sector < Page_Zone.data_sector_end )
			{
				sector ++;
				
				
			}
			else
				break;
			
		}
		else
			return ERR_DRI_OPTFAIL;
//		ret = w25q_Erase_chip_60();
//		if( ret == ERR_OK)
//			break;

		
		
	}
	//读取扇区Page_Zone.fileinfo_sector_begin
	//因为刚擦除过，所有的flash内容都是0xff，也就不必去真的读取了
	memset( Flash_buf, 0xff, SECTOR_SIZE);		
	//设置上次读取的扇区为Page_Zone.fileinfo_sector_begin
	Src_sector = Page_Zone.fileinfo_sector_begin;
	
	sup_head = ( sup_sector_head_t *)Flash_buf;
	
	sup_head->file_count = 0;
	
	strcpy( sup_head->ver, FILESYS_VER);
	Buf_chg_flag = 1;
	
	return ERR_OK;
	
}


//文件的存储信息结构体都是4字节对齐的，所以不必考虑字节对齐问题
//前置条件:在创建文件的时候,特殊块是不能够存在空洞的,所以在删除操作的时候,要去整理特殊扇区中的内存
//长度是可选参数

/**
 * @brief 创建文件.
 *
 * @details 文件的大小的范围是4k - .
 * 
 * @param[in]	inArgName input argument description.
 * @param[out]	outArgName output argument description. 
 * @retval	OK	成功
 * @retval	ERROR	错误 
 * @par 标识符
 * 		保留
 * @par 其它
 * 		无
 * @par 修改日志
 * 		XXX于201X-XX-XX创建
 */
sdhFile * fs_creator(char *name,  int len)
{	
	int i = 0;
	int ret = 0;
	int end = 0;
	area_t		*tmp_area;
	sup_sector_head_t	*sup_head;
	file_info_t	*file_in_storage;
	file_info_t	*creator_file;
	storage_area_t	*target_area;
	sdhFile *p_fd;
	
	//存储区不正确就不处理直接返回
	if( Flash_err_flag )
		return NULL;
	if (len == 0)
		len = 1;
	tmp_area = malloc( sizeof( storage_area_t));
	if( tmp_area == NULL)
	{
		FsErr = ERR_MEM_UNAVAILABLE;
		goto err1;
	}
	ret = page_malloc( tmp_area, len);
	if( ret < 0) {
		
		FsErr =  ERR_NO_FLASH_SPACE;
		goto err2;
	}
	
	ret = read_flash( Page_Zone.fileinfo_sector_begin);
	if( ret != ERR_OK)
	{
		FsErr = ERR_STORAGE_FAIL;
		goto err2;
	}
	
	sup_head = (sup_sector_head_t *)Flash_buf;
	creator_file = ( file_info_t *)( Flash_buf+ sizeof( sup_sector_head_t));

	if( sup_head->file_count > FILE_NUMBER_MAX)
	{
		FsErr =  ERR_FILESYS_OVER_FILENUM;
		goto err2;
	}
	
	//检查文件是否已经存在
	file_in_storage = searchfile( Flash_buf, name);

	if( file_in_storage )	
	{	
		FsErr =  ERR_FILESYS_FILE_EXIST;
		goto err2;
	}
	//找到第一个没有被使用的文件信息存储区
	i = 0;
	for( i = 0; i < FILE_NUMBER_MAX; i ++)
	{
		if( creator_file->file_id == 0xff)
			break;
		creator_file ++;
		
	}
	if ( i == 	FILE_NUMBER_MAX)
	{
		FsErr = ERR_NO_SUPSECTOR_SPACE;
		goto err2;	
	}
	target_area = ( storage_area_t *)( Flash_buf+ sizeof( sup_sector_head_t) + FILE_NUMBER_MAX * sizeof(file_info_t));
	end = sizeof( sup_sector_head_t) + FILE_NUMBER_MAX * sizeof(file_info_t);
	i = 0;
	while(1)
	{
		if( target_area[i].file_id == 0xff)
		{
			target_area[i].file_id = sup_head->file_count;
			target_area[i].seq = 0;
			target_area[i].area.start_pg = tmp_area->start_pg;
			target_area[i].area.pg_number = tmp_area->pg_number;
			break;
		}
		i ++;
		end += sizeof(file_info_t);
		if( end > StrgInfo.sector_size)
		{
			FsErr = ERR_NO_SUPSECTOR_SPACE;
			goto err2;	
			
		}
		
		
		
	}

			
	strcpy( creator_file->name, name);
	creator_file->file_id = sup_head->file_count;
	creator_file->area_total = 1;
	sup_head->file_count ++;
		
	Buf_chg_flag = 1;
		
	p_fd = malloc( sizeof( sdhFile));
	if( p_fd == NULL) {
		FsErr = ERR_MEM_UNAVAILABLE;
		goto err2;	
	}
		
	strcpy( p_fd->name, name);
	memset( p_fd->rd_pstn, 0 , sizeof( p_fd->rd_pstn));
	memset( p_fd->wr_pstn, 0 , sizeof( p_fd->wr_pstn));
	p_fd->wr_size = 0;
	p_fd->reference_count = 1;
	p_fd->area = malloc( sizeof( area_t));
		
	//装载存储区的地址区间到文件描述符中的存储区间链表中去
	p_fd->area->start_pg = tmp_area->start_pg;
	p_fd->area->pg_number = tmp_area->pg_number;
	p_fd->area_total = 1;
	list_ins_next( &L_File_opened, L_File_opened.tail, p_fd);
	free( tmp_area);
	return p_fd;
	

err2:
	page_free( tmp_area, 1);
err1:
	free( tmp_area);
	return NULL;

}

//定位当前位置在存储区间的起始页,并返回区间
static area_t *locate_page( sdhFile *fd, uint32_t pstn, uint16_t *pg)
{
	short i = 0;
	uint32_t offset = pstn;
	int	lct_page = 0;

	
	
	for( i = 0; i < fd->area_total; i ++)
	{
		//位置位于本区间内部
		if( fd->area[i].pg_number *PAGE_SIZE > offset)
		{
			
			lct_page = fd->area[i].start_pg + offset/PAGE_SIZE ;
			*pg = lct_page;
			return &fd->area[i];
		}
		//不知本区间范围内，去下一个区间查找
		offset -= fd->area[i].pg_number * PAGE_SIZE;
		
		
	}
	

	
	return NULL;		//无法定位，这种情况出现的话，应该新分配flash空间
	
	
}

//
int fs_write( sdhFile *fd, uint8_t *data, int len)
{
	
	
	int 			ret;
	int 				i, limit;
	int  				myid = SYS_GETTID();
	area_t		*wr_area;
	uint16_t	wr_page = 0, wr_sector = 0;

	while( 1)
	{
		
		wr_area = locate_page( fd, fd->wr_pstn[myid], &wr_page);
		if( wr_area == NULL)
		{
			return (ERR_FILE_FULL);
		}
		wr_sector = 	wr_page/SECTOR_HAS_PAGES;
		
		if( wr_sector < Page_Zone.data_sector_begin)
		{
			return (ERR_FILE_ERROR);
			
		}
		ret = read_flash(wr_sector);
		if( ret != ERR_OK)
			return ret;
		
	
		i = ( wr_page % SECTOR_HAS_PAGES)*PAGE_SIZE + fd->wr_pstn[myid] % PAGE_SIZE;
		if( wr_area->pg_number + wr_area->start_pg > ( wr_sector + 1 ) *  SECTOR_HAS_PAGES )				//文件的结束位置超出本扇区，以扇区的大小为上限
				limit  = SECTOR_SIZE;
		else		//文件的结束位置在本扇区内部，以文件结束位置在本扇区中的相对位置为上限
				limit = ( wr_area->start_pg +  wr_area->pg_number - wr_sector *  SECTOR_HAS_PAGES) * PAGE_SIZE;
		while( len)
		{
			if( i >= SECTOR_SIZE || i > limit)		 //超出了当前的扇区或者区间范围
			{
				break;
			}
			

			if( Flash_buf[i] != *data)
			{
				Flash_buf[i] = *data;
				Buf_chg_flag = 1;	
			}
			i ++;
			fd->wr_pstn[myid] ++;
			len --;
			data++;
			
		}
		fd->wr_size =  fd->wr_pstn[myid];
		if( len == 0)			
		{
			break;
		}
	}
	
	return ERR_OK;
	
}

int fs_read( sdhFile *fd, uint8_t *data, int len)
{
	
	int 			ret;
	int 				i, limit;
	int  				myid = SYS_GETTID();
	area_t			*rd_area;
	uint16_t	rd_page = 0, rd_sector = 0;

	
	while(1)
	{
		rd_area = locate_page( fd, fd->rd_pstn[myid], &rd_page);
		if( rd_area == NULL)
		{
			return (ERR_FILE_EMPTY);
		}
		
		rd_sector = 	rd_page/SECTOR_HAS_PAGES;
		ret = read_flash(rd_sector);
		if( ret != ERR_OK)
			return ret;
		
		//读写的时候，要考虑文件的结尾不在本扇区的情况
		i = ( rd_page % SECTOR_HAS_PAGES)*PAGE_SIZE + fd->rd_pstn[myid] % PAGE_SIZE;
			
		if( rd_area->start_pg +  rd_area->pg_number > ( rd_sector + 1) *  SECTOR_HAS_PAGES)	//结尾位于本扇区外
			limit = SECTOR_SIZE;
		else	//结尾位于本扇区内
			limit = ( rd_area->start_pg +  rd_area->pg_number - rd_sector *  SECTOR_HAS_PAGES) * PAGE_SIZE;
			
		while( len)
		{
			if( i >= SECTOR_SIZE || i > limit)		 //超出了当前的扇区或者区间范围
			{
				break;
			}
			*data++ = Flash_buf[i];
			i ++;
			fd->rd_pstn[myid] ++;
			len --;
				
		}
			
		if( len == 0)			//还有数据没读完
		{
			
			break;
			
		}
			
	}
	
	return ERR_OK;
		
	
}

int fs_lseek( sdhFile *fd, int offset, int whence)
{
	char myid = SYS_GETTID();

	switch( whence)
	{
		case WR_SEEK_SET:
			fd->wr_pstn[ myid] = offset;
			break;
		case WR_SEEK_CUR:
			fd->wr_pstn[ myid] += offset;
			break;
		
		
		case WR_SEEK_END:			//连续5个0xff作为结尾

		
			break;
		
		case RD_SEEK_SET:
			fd->rd_pstn[ myid] = offset;
			break;
		case RD_SEEK_CUR:
			fd->rd_pstn[ myid] += offset;
			break;
		
		
		case RD_SEEK_END:
			
		
			break;
		case GET_WR_END:
			return fd->wr_pstn[ myid];
		
		default:
			break;
		
	}
	
	return ERR_OK;
	
	
}

int fs_close( sdhFile *fd)
{
	char myid = SYS_GETTID();
	void 							*data = NULL;
	ListElmt           *elmt;
	int  ret;
	file_info_t	*file_in_storage;

	fd->reference_count --;			
	fd->rd_pstn[ myid] = 0;
	fd->wr_pstn[ myid] = 0;
	if( fd->reference_count > 0)
		return ERR_OK;
	ret = read_flash( Page_Zone.fileinfo_sector_begin);
	if( ret != ERR_OK)
		return ret;
	
	file_in_storage = searchfile( Flash_buf, fd->name);

	if( file_in_storage)
	{
		Buf_chg_flag = 1;		//文件被关闭，那么就要尽快区刷新flash缓存了
	}
	
	elmt = list_get_elmt( &L_File_opened,fd->name);
	list_rem_next( &L_File_opened, elmt, &data);
	free( fd->area);
	
	free(fd);
	return ERR_OK;
}
///返回文件占用的存储区空间
int fs_du( sdhFile *fd)
{
	int i = 0;
	int size = 0;
	for( i = 0; i < fd->area_total; i ++)
	{
		size += fd->area[i].pg_number * StrgInfo.page_size;
	}
	return size;
	
}

int fs_delete( sdhFile *fd)
{
	int ret = 0;
	file_info_t	*file_in_storage;
	storage_area_t	*src_area;
	sup_sector_head_t	*sup_head;
	short  j, k;
	short end = 0;

	
	fd->reference_count --;
	if( fd->reference_count > 0)
		return ERR_FILE_OCCUPY;
	ret = read_flash(Page_Zone.fileinfo_sector_begin);
	if( ret != ERR_OK)
		return ret;
	sup_head = ( sup_sector_head_t *)Flash_buf;
	k = 0;
	file_in_storage = searchfile( Flash_buf, fd->name);
	//存储器的管理区找到该文件的管理内容然后删除掉
	
	if( file_in_storage )
	{
		
		
		src_area = ( storage_area_t *)( Flash_buf + sizeof(sup_sector_head_t) + FILE_NUMBER_MAX * sizeof(file_info_t) );
		end = sizeof(sup_sector_head_t) + FILE_NUMBER_MAX * sizeof(file_info_t);
		j = 0;
		while(1)
		{
			if( src_area[j].file_id == file_in_storage->file_id)
			{
				memset( &src_area[j], 0xff, sizeof(storage_area_t));
				k ++;
				
			}
			if( k == file_in_storage->area_total)
				break;
			end += sizeof(file_info_t);
			if( end > StrgInfo.sector_size)
				break;
			
		}
		
		Buf_chg_flag = 1;
		memset( file_in_storage, 0xff, sizeof(file_info_t));
	}
		
		
	sup_head->file_count --;
	if( fd->area != NULL)
	{
		page_free( fd->area, fd->area_total);
		free( fd->area);
	}
	
	free(fd);
	fd = NULL;
			
	return ERR_OK;
	
}

int fs_flush( void)
{
	int ret;
	//存储区不正确就不处理直接返回
	if( Flash_err_flag )
		return ERR_FLASH_UNAVAILABLE;

		
	if( Buf_chg_flag == 0)
		return ERR_OK;
	ret = flush_flash( Src_sector);
	if( ret == ERR_OK)
	{
		Buf_chg_flag = 0;
		
		return ERR_OK;
		
	}
	return ret;
		
	
	
}








static int read_flash( uint16_t sector)
{
	int ret = 0;
	if( Src_sector == sector )		
	{
		//本次读取的扇区与上次读取的一致时候，可以不必再去读取
		//如果缓存修改标志置起，说明缓存中的内容比flash中的内容更新
		
		return ERR_OK;
	}
	
	//读取另外一个扇区，则根据缓存修改标志来决定是否将缓存内容写日flash

	if( Buf_chg_flag)
	{
		ret = flush_flash( Src_sector);
		if( ret != ERR_OK)
		{
			return ret;
			
		}
//		Src_sector = INVALID_SECTOR;
	}
	SYS_ARCH_PROTECT();
	ret = flash_read_sector(Flash_buf,sector);
	SYS_ARCH_UNPROTECT();
	if( ret == ERR_OK)
	{
		Buf_chg_flag = 0;
		Src_sector = sector;
		return ERR_OK;
	}
		
	return ret;

}

static int flush_flash( uint16_t sector)
{
	int ret = 0;
	
	SYS_ARCH_PROTECT();
	ret = flash_erase_sector(sector);
	
	if( ret != ERR_OK )
	{
		SYS_ARCH_UNPROTECT();
		return ret;
	}

	ret =  flash_write_sector( Flash_buf, sector);
	SYS_ARCH_UNPROTECT();
	return ret;
}

static file_info_t	*searchfile( uint8_t	*flash_data, char *name)
{
	int i = 0;
	file_info_t	*file_in_storage;
	file_in_storage = ( file_info_t *)( flash_data + sizeof(sup_sector_head_t));
	
	//存储器的管理区找到该文件的管理内容然后删除掉
	for( i = 0; i < FILE_NUMBER_MAX; i ++)
	{
		if( strcmp(file_in_storage->name, name) == 0x00 )
		{
							
			return file_in_storage;
		}
		
		file_in_storage ++;
		
		
	}
	return NULL;
	
}

//返回的空间时当前能够找到的最合适的空间，不一定能够满足请求的空间，调用者来检查结果并决定是否再次调用来补足剩余的空间
static int page_malloc( area_t *area, int size)
{
	uint16_t 	j = 0, page_end,k;
	uint16_t	mem_manger_sector = 0;
	short		wr_addr = 0;
	uint16_t	offset ;
	int ret = 0;
	int		begin_pg = 0;
	
	
	mem_manger_sector = Page_Zone.pguseinfo_sector_begin;
	offset = Page_Zone.data_sector_begin * StrgInfo.sector_pagenum;
	while(1)
	{
		
		ret = read_flash(mem_manger_sector);
		if( ret != ERR_OK)
			return ret;
		//从数据存储区的空闲页中找到能够符合长度要求的起始页地址
		//todo : 这里有个bug，内存页管理扇区管理的内存页时互相独立的，不能组合成一个总体。类似于window的D盘不足时无法从E盘借用内存页
		//这个bug决定了一个文件最大的长度是一个扇区管理的内存页的总大小。超过它就无法分配内存页了。
		get_area( offset, size, area);
		if( area->start_pg > 0)	
		{
			area->start_pg += ( mem_manger_sector - Page_Zone.pguseinfo_sector_begin) * StrgInfo.sector_size * 8;	
			break;
		}
		
		mem_manger_sector ++;
		offset = 0;
		if( mem_manger_sector == Page_Zone.pguseinfo_sector_end)
			return ERR_NO_FLASH_SPACE;
		
		
	}
			
		
	//擦除是为了把0写成1，在标记已经被分配掉的页时是要把对应的bit从1->0
	//因此不会出现0->1的场景，为了节省操作就不进行擦除操作了。
	//只需要将被分配掉的页的标志清除即可
	
	
	begin_pg = area->start_pg & ( StrgInfo.sector_size * 8 - 1);  
	for( j = begin_pg; j <= area->pg_number; j ++)
		clear_bit( Flash_buf, j);
	
	//计算begin_pg这个起始位置在一个页中的起始位置
	j = begin_pg / 8 % StrgInfo.page_size;
	k = area->pg_number/8 + 1;  //计算要分配的长度所占用的字节长度
	
	page_end = (begin_pg + k) / StrgInfo.page_size + 1;
	wr_addr = 0;
	while(1)
	{
		offset = ( begin_pg/8/StrgInfo.page_size + wr_addr)* StrgInfo.page_size;
		ret = flash_write(Flash_buf + offset,  mem_manger_sector*SECTOR_SIZE + offset, PAGE_SIZE);
		if( ret == ERR_OK )
		{
			wr_addr ++;
			if( wr_addr >= page_end) 
			{
				return wr_addr;
				
			}
			
			
			
		}
		else
			return ret;
	}
}


//文件的内存要在同一个扇区管理的内存页区中才能使用改程序
static int page_free( area_t *area, int area_num)
{
	
	uint16_t i = 0;
	uint16_t j = 0, k = 0;
	uint16_t	mem_manger_sector = 0;
	uint16_t	sector_bit = StrgInfo.sector_size * 8;
	uint16_t	sector_offset = 0;
	int ret = 0;
	
		


	while( area_num)
	{


		if( area->start_pg + area->pg_number > StrgInfo.total_pagenum)
				return -1;
		
		sector_offset = area->start_pg / sector_bit;
		mem_manger_sector = Page_Zone.pguseinfo_sector_begin + sector_offset;	
		ret = read_flash(mem_manger_sector);
		if( ret != ERR_OK)
			return ret;	
		
		
		i = area[k].start_pg % ( sector_offset * sector_bit);
		for( j = 0; j < area[k].pg_number; j ++)
			set_bit( Flash_buf, i + j);
		
		Buf_chg_flag = 1;
		
		area_num --;
		if( area_num)
			area ++;
	}
	
		
	return ERR_OK;
		
	
			
			
			
			
			
	
	
}



static bool check_bit(uint8_t *data, int bit)
{
	int i, j ;
	i = bit/8;
	j = bit % 8;
	return ( data[i] & ( 1 << j));
	
	
}
static void clear_bit(uint8_t *data, int bit)
{
	int i, j ;
	i = bit/8;
	j = bit % 8;
	data[i] &= ~( 1 << j);
	
	
	
	
}

static void set_bit(uint8_t *data, int bit)
{
	int i, j ;
	i = bit/8;
	j = bit % 8;
	data[i] |=  1 << j;
	
	
	
	
}

///尽可能的返回足够大的空间
static void get_area( int pg_offset, int size, area_t *out_area)
{
	short i, j ;
	int pages = 0;
	area_t	max_area = {0,0};
	
	if( size > 0)
		size --;
	pages = size / PAGE_SIZE + 1;
	
	for( i = pg_offset; i < StrgInfo.sector_size * 8; i++)
	{
		
		if( check_bit( Flash_buf, i)) 		
		{
			
			
			for( j = 0; j < pages; j ++)	//检查长度是否达标
			{
				if( !check_bit( Flash_buf, i+j)) 
					break;
				
			}
			if( j == pages)
			{
				out_area->start_pg = i;
				out_area->pg_number = pages;
				return ;
			}
			
			if(max_area.pg_number < j  )
			{
				max_area.start_pg = i;
				max_area.pg_number = j ;
			}
			i+= j;		///跳过这个连续的区间，寻找下一个区间
			
		}
		
	}
	
	out_area->start_pg = max_area.start_pg;
	out_area->pg_number = max_area.pg_number;;
	
	
}

/**
 * @brief 文件系统的测试程序.
 *
 * @details 用尽可能的全覆盖代码路径的原则来编写这个测试程序.
 * 
 * @param[in]	inArgName input argument description.
 * @param[out]	outArgName output argument description. 
 * @retval	OK	??
 * @retval	ERROR	?? 
 */
int fs_test(void)
{
	int ret = 0;
	uint32_t	fsmax = 0;
	sdhFile	*ftest;
	uint8_t data;
	int i = 0;
	ret = filesys_init();
	if( ret != ERR_OK)
	{
		DPRINTF(" init filesystem fail, return val %d \n", ret);
		return ERR_FAIL;
		
	}
	DPRINTF(" init filesystem successed! \n");
	fsmax = StrgInfo.sector_size *(Page_Zone.data_sector_end - Page_Zone.data_sector_begin);
	ftest = fs_open("Max_file.test");
	if( fs_get_error() == ERR_FILESYS_ERROR)
	{
		ret = fs_format();
		if( ret != ERR_OK)
		{
			DPRINTF(" format filesystem fail, return val %d \n", ret);
			return ERR_FAIL;
			
		}
		DPRINTF(" format filesystem successed! \n");
	}

	if( fs_get_error() != ERR_OK)
	{
		DPRINTF("try create a max signal file, size %d KB  ", fsmax/1024);
		ftest = fs_creator("Max_file.test",  fsmax);
		if( ftest == NULL)
		{
			DPRINTF(" failed !\n");
			return ERR_FAIL;
			
		}
		DPRINTF(" succeed! \n");
	}
	for( i = 0; i < fsmax; i ++)
	{
		data = i;
		if( fs_write( ftest, &data, 1) != ERR_OK)
		{
			DPRINTF(" fs_write failed at the %d times\n", i);
			return ERR_FAIL;
		}
		
	}
	DPRINTF(" fs_write %d data succeed\n", i);
	
	for( i = 0; i < fsmax; i ++)
	{
		data = i;
		if( fs_read( ftest, &data, 1) != ERR_OK)
		{
			DPRINTF(" fs_read failed at the %d times\n", i);
			return ERR_FAIL;
		}
		if( data != ( i & 0xff))
		{
			DPRINTF(" read data is  %d  != %d, err\n", data, i&0xff);
			return ERR_FAIL;
		}
		
	}
	DPRINTF(" fs_read %d data succeed\n", i);
	return ERR_OK;
	
}











