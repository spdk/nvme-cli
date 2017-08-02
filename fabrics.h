#ifndef _DISCOVER_H
#define _DISCOVER_H

struct config {
	char *nqn;
	char *transport;
	char *traddr;
	char *trsvcid;
	char *host_traddr;
	char *hostnqn;
	char *hostid;
	char *nr_io_queues;
	char *queue_size;
	char *keep_alive_tmo;
	char *reconnect_delay;
	char *raw;
	char *device;
};

extern int fdiscover(const char *desc, int argc, char **argv, bool connect);
extern int fconnect(const char *desc, int argc, char **argv);
extern int fdisconnect(const char *desc, int argc, char **argv);

#endif
