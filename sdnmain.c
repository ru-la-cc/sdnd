#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <signal.h>
#include "sdnd.h"


static char _s_moduledir[PATH_MAX+1];
/* static char _s_conffile[PATH_MAX+1]; */
static char _s_logfile[PATH_MAX+1];
static char _s_modulename[PATH_MAX+1];

int main(int argc, char* argv[])
{
	sdnconf sdnc;
	/* int selffd; */
	char *p;
	struct sockaddr_in addr;
	int s;
	
	if(sdn_daemon_start(2,1,_s_moduledir,(int)sizeof(_s_moduledir)-1) < 0) puts("daemon error!!");
	
	for(p=argv[0]+strlen(argv[0]); p>=argv[0]; --p){
		if(*p == '/') break;
	}
	sdn_joinpath(_s_moduledir, p+1, _s_modulename, sizeof(_s_modulename));
	
	sdn_loadconf(argc > 1 ? argv[1] : SDN_DEF_CONFFILE, &sdnc);
	
	sdn_init_mutex();
	
	if(sdnc.logfile[0] != '/') sdn_joinpath(_s_moduledir, sdnc.logfile,_s_logfile,sizeof(_s_logfile));
	else strcpy(_s_logfile, sdnc.logfile);
	
	sdn_logopen(_s_logfile, sdnc.mode);
	sdn_putlog("sdnd version %s start", SDN_VERSION);
	
	signal(SIGPIPE, SIG_IGN);
	
	if((s=sdn_svsocket(sdnc.port, &addr)) < 0){
		sdn_putlog("server start error!!");
	}else{
		sdn_putlog("listen port = %u", sdnc.port);
		sdn_mainloop(s);
	}
	sdn_putlog("sdnd version %s end", SDN_VERSION);
	
	sdn_logclose();
	
	sdn_destroy_mutex();
	
	return 0;
}
