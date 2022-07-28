#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
/* #include <arpa/inet.h> */
/* #include <netdb.h> */
#include "sdnd.h"

static pthread_mutex_t _s_logmtx, _s_tcmtx;
static FILE *_s_logf = NULL;
static int _s_logfd = -1;
static int _s_stopflg;
static int _s_threadcount = 0;
static int *_s_serversocket = NULL;
static sdnconf *_s_sdnc = NULL;

static void s_sdn_parsekeyval(char *line, char **key, char **value);
static void s_sdn_sigterm(int sig);

void sdn_init_mutex(void)
{
	pthread_mutex_init(&_s_tcmtx, NULL);
}

void sdn_destroy_mutex(void)
{
	pthread_mutex_destroy(&_s_tcmtx);
}

int sdn_logopen(const char *logfile, int mode)
{
	pthread_mutex_init(&_s_logmtx, NULL);
	if(mode){
		_s_logfd = open(logfile, O_CREAT|(mode==1?O_TRUNC:O_APPEND)|O_WRONLY,0644);
		if(_s_logfd < 0) return 1;
	}
	return 0;
}

void sdn_logclose()
{
	if(_s_logfd >= 0
		&& _s_logfd != STDIN_FILENO
		&& _s_logfd != STDOUT_FILENO
		&& _s_logfd != STDERR_FILENO){
		close(_s_logfd);
	}
	pthread_mutex_destroy(&_s_logmtx);
}

int sdn_putlog(const char* str, ...)
{
	va_list list;
	struct tm *ptm;
	time_t tv;
	static char logstr[2048+1];
	int ret = 0;
	
	if(_s_logfd <= 0) return 1;
	
	time(&tv);
	if(pthread_mutex_lock(&_s_logmtx)) return 3;
	
	ptm = localtime(&tv);
	sprintf(logstr, "%04u/%02u/%02u %02u:%02u:%02u\t",
		ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
	if(write(_s_logfd,logstr,strlen(logstr)) < 0) ret = 2;
	if(!ret){
		va_start(list, str);
		vsnprintf(logstr, sizeof(logstr)-1, str, list);
		va_end(list);
		logstr[sizeof(logstr)-1] = '\0';
		if(write(_s_logfd,logstr,strlen(logstr)) < 0) ret = 2;
	}
	if(!ret && write(_s_logfd,"\n",1) < 0) ret = 2;
	pthread_mutex_unlock(&_s_logmtx);
	return ret;
}

int sdn_daemon_start(int allclose, int ch_root, char *bcwd, int len)
{
	int fdmax, i, fd;
	pid_t pid;
	
	if((pid = fork()) < 0) return -1;
	if(pid) _exit(0);
	
	if(setsid() < 0) return -1;
	
	if((pid = fork()) < 0) return -1;
	if(pid) _exit(0);
	
	/* ？ */
	umask(0);
	
	if(bcwd != NULL) getcwd(bcwd,len);
	
	if(ch_root) chdir("/");
	
	switch(allclose){
		case 1:
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
		break;
		case 2:
			fdmax = sysconf(_SC_OPEN_MAX);
			for(i=0; i<fdmax; ++i) close(i);
		break;
	}
	
	if((fd=open("/dev/null",O_RDWR)) >= 0){
		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		if(fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO) close(fd);
	}
	
	return 0;
}

void sdn_joinpath(const char* dirpath, const char* filename, char* fullfile, int len)
{
	const char *p1;
	
	if(filename == NULL || fullfile == NULL || len < 1) return;
	
	strncpy(fullfile, dirpath, len-1);
	fullfile[len-1] = '\0';
	p1 = fullfile + strlen(fullfile);
	if(p1 > fullfile) --p1;
	if(*p1 != '/'){
		if((len-strlen(fullfile)) > 1) strcat(fullfile, "/");
	}
	strncat(fullfile, filename, len-strlen(fullfile));
	fullfile[len-1] = '\0';
}

int sdn_loadconf(const char* conffile, sdnconf* sdnc)
{
	FILE *pf;
	int ret = 0;
	char line[1024], *p, *key, *value;
	
	sdnc->port = SDN_DEF_PORT;
	strncpy(sdnc->logfile, SDN_DEF_LOG, PATH_MAX);
	sdnc->logfile[PATH_MAX] = '\0';
	sdnc->mode = 1;
	sdnc->fresult = 0;
	
	if((pf=fopen(conffile,"r")) == NULL) return 1;
	while(fgets(line,sizeof(line),pf) != NULL){
		if(ferror(pf)){
			ret = 1;
			break;
		}
		for(p=line; *p&&(*p==' '||*p=='\t'); ++p);
		if(*p == '#') continue;
		
		s_sdn_parsekeyval(p, &key, &value);
		if(!strcmp(key,"port")){
			sdnc->port = (u_short)atoi(value);
		}else if(!strcmp(key,"log")){
			strncpy(sdnc->logfile, value, PATH_MAX);
			sdnc->logfile[PATH_MAX] = '\0';
		}else if(!strcmp(key,"mode")){
			sdnc->mode = (short)atoi(value);
		}else if(!strcmp(key,"fresult")){
			sdnc->fresult = (short)atoi(value);
		}
	}
	
	_s_sdnc = sdnc;
	
	fclose(pf);
	return ret;
}

void sdn_ph_hton(phi* h)
{
	h->mkr = htons(h->mkr);
	h->ver = htons(h->ver);
	h->fcd = htons(h->fcd);
	h->rcd = htons(h->rcd);
	h->size = htonl(h->size);
}

void sdn_ph_ntoh(phi* h)
{
	h->mkr = ntohs(h->mkr);
	h->ver = ntohs(h->ver);
	h->fcd = ntohs(h->fcd);
	h->rcd = ntohs(h->rcd);
	h->size = ntohl(h->size);
}

unsigned long long sdn_htonll(unsigned long long hostll)
{
	unsigned int* pl;
	unsigned int l;
	volatile unsigned short vus = 0x12ef;
	
	if(htons(vus) == vus) return hostll;
	pl = (unsigned int*)&hostll;
	l = htonl(*pl);
	*pl = htonl(*(pl+1));
	*(pl+1) = l;
	
	return hostll;
}

unsigned long long sdn_ntohll(unsigned long long netll)
{
	unsigned int* pl;
	unsigned int l;
	volatile unsigned short vus = 0x12ef;
	
	if(ntohs(vus) == vus) return netll;
	pl = (unsigned int*)&netll;
	l = ntohl(*pl);
	*pl = ntohl(*(pl+1));
	*(pl+1) = l;
	
	return netll;
}

void sdn_ph_clear(phi* h)
{
	memset(h, 0, sizeof(phi));
	h->mkr = SDN_PHI_MKR;
	h->ver = SDN_PHI_VER;
}

int sdn_ph_check(phi* h)
{
	if(h->mkr != SDN_PHI_MKR) return -1;
	if(h->ver > SDN_PHI_VER){
		sdn_putlog("ver=%u xxx", h->ver);
		return -1;
	}
	return 0;
}

int sdn_svsocket(u_short port, struct sockaddr_in* addr)
{
	int s, opt = 1;
	
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	if(s >= 0){
		memset(addr, 0, sizeof(struct sockaddr_in));
		addr->sin_family = AF_INET;
		addr->sin_port = htons(port);
		(addr->sin_addr).s_addr = INADDR_ANY;
		
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
		
		if(bind(s,(struct sockaddr*)addr,sizeof(struct sockaddr_in))){
			close(s);
			sdn_putlog("bind error!");
			return -1;
		}
		if(listen(s,5)){
			close(s);
			sdn_putlog("listen error!");
			return -1;
		}
	}
	
	return s;
}

int sdn_send(int s, const char* buf, int len, int flags)
{
	int sendlen, totalsend=0;
	fd_set fds;
	struct timeval tv;
	
	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = SDN_PHI_TIMEOUT;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(s, &fds);
	
	while(1){
		if(!select(s+1,NULL,&fds,NULL,&tv)){
			sdn_putlog("send timeout");
			return -1;
		}else{
			if(!FD_ISSET(s,&fds)){
				sdn_putlog("send error");
				return -1;
			}else{
				sendlen = send(s, buf, len, flags);
				if(sendlen <= 0) return -1;
				totalsend += sendlen;
				if(totalsend >= len) break;
			}
		}
	}
	return totalsend;
}

int sdn_recv(int s, char* buf, int len, int flags)
{
	int recvlen, totalrecv=0;
	fd_set fds;
	struct timeval tv;
	
	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = SDN_PHI_TIMEOUT;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(s, &fds);
	
	while(1){
		if(!select(s+1,&fds,NULL,NULL,&tv)){
			sdn_putlog("recv timeout");
			return -1;
		}else{
			if(!FD_ISSET(s,&fds)){
				sdn_putlog("recv error");
				return -1;
			}else{
				recvlen = recv(s, buf, len, flags);
				if(recvlen <= 0) return -1;
				totalrecv += recvlen;
				if(totalrecv >= len) break;
			}
		}
	}
	return totalrecv;
}

int sdn_mainloop(int s)
{
	int len;
	struct sockaddr_in claddr;
	thpara tp;
	pthread_t tid;
	
	signal(SIGTERM, s_sdn_sigterm);
	_s_stopflg = 0;
	_s_serversocket = &s;
	while(1){
		len = sizeof(claddr);
		tp.s = accept(s, (struct sockaddr*)&claddr, &len);
		if(_s_stopflg) break;
		if(tp.s < 0){
			sdn_putlog("client socket error");
			continue;
		}
		tp.port = ntohs(claddr.sin_port);
		sprintf(tp.ipaddr, "%u.%u.%u.%u",
				((unsigned char*)&claddr.sin_addr)[0],
				((unsigned char*)&claddr.sin_addr)[1],
				((unsigned char*)&claddr.sin_addr)[2],
				((unsigned char*)&claddr.sin_addr)[3]
				);
		tp.cpend = 0;
		sdn_putlog("accept : host=%s port=%u", tp.ipaddr, tp.port);
		if(pthread_create(&tid, NULL, sdn_req_thread, &tp)){
			sdn_putlog("pthread_create error!!");
			if(tp.s >= 0) close(tp.s);
			_s_serversocket = NULL;
			continue;
		}
		while(!tp.cpend) usleep(100000);
	}
	pthread_mutex_lock(&_s_tcmtx);
	if(_s_threadcount > 0){
		pthread_mutex_unlock(&_s_tcmtx);
		sdn_putlog("wait %dsec", SDN_THEND_WAIT);
		sleep(SDN_THEND_WAIT);
	}else  pthread_mutex_unlock(&_s_tcmtx);
	
	pthread_mutex_lock(&_s_tcmtx);
	sdn_putlog("thread ... %d", _s_threadcount);
	pthread_mutex_unlock(&_s_tcmtx);
	
	return 0;
}

int sdn_diskfree(const char* path, unsigned long long *ava, unsigned long long *total)
{
	struct statvfs sb;
	
	if(statvfs(path, &sb)) return -1;
	if(_s_sdnc->fresult) *ava = (unsigned long long)sb.f_frsize * sb.f_bfree;
	else *ava = (unsigned long long)sb.f_frsize * sb.f_bavail;
	*total = (unsigned long long)sb.f_frsize * sb.f_blocks;
	return 0;
}

void* sdn_req_thread(void* param)
{
	thpara tp;
	thpara* para;
	char buf[SDN_PHI_SRBUF+1];
	int state = 0, len;
	phi h, sh;
	unsigned long long fr, al;
	
	pthread_mutex_lock(&_s_tcmtx);
	++_s_threadcount;
	pthread_mutex_unlock(&_s_tcmtx);
	if(pthread_detach(pthread_self())) sdn_putlog("pthread_detach error!!");
	
	
	para = (thpara*)param;
	tp.s = para->s;
	tp.port = para->port;
	strcpy(tp.ipaddr, para->ipaddr);
	para->cpend = tp.cpend = 1;
	
	if(!state){
		if((len=sdn_recv(tp.s,(char*)&h,sizeof(h),0)) < 0) state = 1;
		else{
			sdn_ph_ntoh(&h);
			if(sdn_ph_check(&h)) state = 1;
		}
	}
	
	sdn_ph_clear(&sh);
	sh.rcd = SDN_PHI_EF;
	
	if(!state){
		if(h.fcd == SDN_PHI_GF){
			if((len=sdn_recv(tp.s,buf,h.size>SDN_PHI_SRBUF?SDN_PHI_SRBUF:h.size,0)) < 0){
				sdn_putlog("recv error!!");
				state = 1;
			}else{
				buf[len] = '\0';
				if(sdn_diskfree(buf, &fr, &al)){
					sdn_putlog("diskfree error path=%s", buf);
					state = 1;
				}else{
					sdn_putlog("%s free space=%llu all=%llu", buf, fr, al);
					sh.rcd = SDN_PHI_OF;
				}
			}
		}
	}
	
	if(!state){
		sh.size = sizeof(fr)+sizeof(al);
		sdn_ph_hton(&sh);
		fr = sdn_htonll(fr);
		al = sdn_htonll(al);
		memcpy(buf,&sh,sizeof(sh));
		memcpy(buf+sizeof(sh), &fr, sizeof(fr));
		memcpy(buf+sizeof(sh)+sizeof(fr), &al, sizeof(al));
		
		if(sdn_send(tp.s,buf,sizeof(sh)+sizeof(fr)+sizeof(al),0) < 0){
			state = 1;
			sdn_putlog("send error IP=%s port=%u", tp.ipaddr, tp.port);
		}
	}else{
		if(sdn_send(tp.s,(const char*)&sh,sizeof(sh),0) < 0){
			state = 1;
			sdn_putlog("send error IP=%s port=%u", tp.ipaddr, tp.port);
		}
	}
	close(tp.s);
	pthread_mutex_lock(&_s_tcmtx);
	if(_s_threadcount > 0) --_s_threadcount;
	pthread_mutex_unlock(&_s_tcmtx);
	
	return NULL;
}

static void s_sdn_parsekeyval(char *line, char **key, char **value)
{
	char *p1, *p2, *p3;
	
	/* key */
	for(p1=line; *p1 && (*p1==' ' || *p1=='\t'); ++p1);
	*key = p1;
	for(p2=p1; *p2 && (*p2!='='); ++p2);
	*p2 = '\0';
	for(p3=p2; p3>=*key; --p3){
		if(*p3 && *p3 != ' ' && *p3 != '\t') break;
	}
	*(p3+1) = '\0';
	
	for(p1=p2+1; *p1 && (*p1==' ' || *p1=='\t'); ++p1);
	*value = p1;
	if((p2=strchr(*value,'\n')) != NULL) *p2 = '\0';
}

static void s_sdn_sigterm(int sig)
{
	_s_stopflg = 1;
	if(_s_serversocket != NULL){
		close(*_s_serversocket);
		*_s_serversocket = -1;
	}
}
