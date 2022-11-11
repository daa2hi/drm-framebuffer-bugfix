#include "pattern.h"


#define PATSIZE 64
#define PAT_MAGIC 0x07C007EC

static uint32_t pat[PATSIZE*PATSIZE+1];

static void gen_base_pattern();

void fill_pattern(uint32_t *buffer,uint16_t W,uint16_t H,uint16_t frameNo)
{
	if( pat[PATSIZE*PATSIZE] != PAT_MAGIC )
		gen_base_pattern();
	frameNo = frameNo%PATSIZE;
	for(uint16_t y=0;y<H;y++)
	{
		uint16_t x=0;
		uint32_t *wr;
		const uint32_t *pat_rd;
		wr = buffer + y*(uint32_t)W;
		pat_rd = pat + (y%PATSIZE)*PATSIZE;
		// first cut
		if(frameNo)
		{
			memcpy(wr,pat_rd+PATSIZE-frameNo,frameNo*4);
			wr+=frameNo;x+=frameNo;
		}
		// middle full circles
		while(x+PATSIZE<W)
		{
			memcpy(wr,pat_rd,PATSIZE*4);
			wr+=PATSIZE;x+=PATSIZE;
		}
		// last cut
		if(x<W)
			memcpy(wr,pat_rd,(W-x)*4);
	}
}

static void gen_base_pattern()
{
	uint32_t Ro2 = (PATSIZE-2)*(PATSIZE-2)*4;
	uint32_t Ri2 = (PATSIZE-6)*(PATSIZE-6)*4;
	for(uint32_t y=0;y<PATSIZE;y++)
	{
		int32_t y4 = (int32_t)(y<<2)+2-PATSIZE*2;
		for(uint32_t x=0;x<PATSIZE;x++)
		{
			int32_t x4 = (int32_t)(x<<2)+2-PATSIZE*2;
			uint32_t r2 = (uint32_t)(x4*x4+y4*y4);
			uint32_t color = 0xFF000000;
			// thresholds:
			if( r2 >= Ro2 )
			{
				//outside
			}else if( r2 >= Ri2 )
			{
				// ring
				color += 0x00EEEEEE;
			}else{
				// inside
				if((y^x)&4)
					color += 0x00FF0000;
				else
					color += 0x0000FF00;
			}
			pat[x+PATSIZE*y] = color;
		}
	}
	pat[PATSIZE*PATSIZE] = PAT_MAGIC;
}

