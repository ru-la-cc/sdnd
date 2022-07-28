#ifndef __INC_SDND_H__
#define __INC_SDND_H__

#include <limits.h>
#include <netinet/in.h>

#define SDN_CONFFILE		"sdnd.conf"
#define SDN_DEF_CONFFILE	"/etc/sdn/sdnd.conf"
#define SDN_DEF_PORT		50801
#define SDN_DEF_LOG			"/var/log/sdnd.log"
#define SDN_PHI_MKR			1825
#define SDN_PHI_VER			2
#define SDN_PHI_GF			0x0001
#define SDN_PHI_OF			0x0100
#define SDN_PHI_EF			0x0200
#define SDN_PHI_END			0xffff
#define SDN_PHI_SRBUF		1024
#define SDN_PHI_TIMEOUT		5
#define SDN_THEND_WAIT		5
#define SDN_VERSION			"0.2.3.1"

typedef struct _tag_phi{
	unsigned short			mkr;
	unsigned short			ver;
	unsigned short			fcd;
	unsigned short			rcd;
	unsigned int			size;
} phi;

typedef struct _tag_thpara{
	int s;
	unsigned short port;
	char ipaddr[64];
	int cpend;
} thpara;

typedef struct _tag_sdnconf{
	unsigned short port;
	char logfile[PATH_MAX+1];
	short mode;
	short fresult;
} sdnconf;

void sdn_init_mutex(void);
void sdn_destroy_mutex(void);
int sdn_logopen(const char *logfile, int mode);
void sdn_logclose();
int sdn_putlog(const char* str, ...);
int sdn_daemon_start(int allclose, int ch_root, char *bcwd, int len);
void sdn_joinpath(const char* dirpath, const char* filename, char* fullfile, int len);
int sdn_loadconf(const char* conffile, sdnconf* sdnc);
void sdn_ph_hton(phi* h);
void sdn_ph_ntoh(phi* h);
unsigned long long sdn_htonll(unsigned long long hostll);
unsigned long long sdn_ntohll(unsigned long long netll);
void sdn_ph_clear(phi* h);
int sdn_ph_check(phi* h);
int sdn_svsocket(u_short port, struct sockaddr_in* addr);
int sdn_send(int s, const char* buf, int len, int flags);
int sdn_recv(int s, char* buf, int len, int flags);
int sdn_mainloop(int s);
int sdn_diskfree(const char* path, unsigned long long *ava, unsigned long long *total);
void* sdn_req_thread(void* param);

#endif
