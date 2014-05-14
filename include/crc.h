#ifndef __CRC_H__
#define __CRC_H__

#include <linux/types.h>

u8 crc8(u8 crc, u8 const *buffer, size_t len);
u16 crc16(u16 crc, const u8 *buffer, size_t len);

#endif /* __CRC_H__ */

