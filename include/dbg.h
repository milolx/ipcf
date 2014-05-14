#ifndef __DBG_H__
#define __DBG_H__

#ifdef __DEBUG__
#define DBG(fmt, args...)	\
	printf("[%s:%d] "fmt, __FILE__, __LINE__, ##args)
#else
#define DBG(fmt, args...)	\
	do {}while(0)
#endif

#endif /* __DBG_H__ */
