#include "enc_base.h"
#include "log.h"
int enc_base::disp_setup_frame_header(uint8_t * msg, int x, int y, int right, int bottom, uint8_t op_flg ,uint32_t total)
{
	udisp_frame_header_t * pfh;
	static int fid=0;

	pfh = (udisp_frame_header_t *)msg;
	pfh->type = op_flg;
	pfh->crc16 = 0;
	pfh->x = cpu_to_le16(x);
	pfh->y = cpu_to_le16(y);
	pfh->y = cpu_to_le16(y);
	pfh->width = cpu_to_le16(right + 1 - x);
	pfh->height = cpu_to_le16(bottom + 1 - y);
	pfh->payload_total = total;
	pfh->frame_id=fid++;

	LOGD("fid:%d enc:%d %d\n",pfh->frame_id, pfh->type,pfh->payload_total);

	return sizeof(udisp_frame_header_t);
}

