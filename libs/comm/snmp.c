#include <config/defaults.h>

#include "snmp.h"

#include <libs/daemon/console.h>
#include <libs/daemon/daemon-poll.h>

#include <errno.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#define OID_RSSI "1.3.6.1.4.1.40297.1.2.1.2.10.0"

static oid oid_rssi[MAX_OID_LEN];
static size_t oid_rssi_length = 0;
static double last_rssi = 0;
struct snmp_session *active_session = NULL;

static int snmp_get_rssi_cb(int operation, struct snmp_session *sp, int reqid, struct snmp_pdu *pdu, void *magic) {
	//struct session *host = (struct session *)magic;
	char value[15] = {0,};
	char *endptr = NULL;
	double value_double;

	if (sp != active_session)
		return 1;

	if (operation == NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE) {
		if (pdu->errstat == SNMP_ERR_NOERROR) {
			snprint_value(value, sizeof(value), oid_rssi, oid_rssi_length, pdu->variables);
			errno = 0;
			value_double = strtod(value+9, &endptr); // +9: cutting "INTEGER: " text returned by snprint_value().
			if (*endptr != 0 || errno != 0)
				console_log(LOGLEVEL_DEBUG "snmp: invalid rssi value received: %s\n", value);
			else {
				last_rssi = value_double;
				console_log("snmp: got rssi value %lf\n", last_rssi);
				// TODO: store timestamp
			}
		} else
			console_log(LOGLEVEL_DEBUG "snmp: rssi read error\n");
    } else
    	console_log(LOGLEVEL_DEBUG "snmp: rssi read timeout\n");

	active_session = NULL;

	return 0;
}

void snmp_start_read_rssi(char *host) {
	struct snmp_pdu *pdu;
	struct snmp_session session;
	const char *community = "public";

	if (oid_rssi_length == 0)
		return;

	snmp_close(active_session);

	snmp_sess_init(&session);
	session.version = SNMP_VERSION_1;
	session.peername = strdup(host);
	session.community = (unsigned char *)strdup(community);
	session.community_len = strlen(community);
	session.callback = snmp_get_rssi_cb;
	session.callback_magic = host;
	if (!(active_session = snmp_open(&session))) {
		console_log("snmp error: error opening session to host %s\n", host);
		return;
	}

	pdu = snmp_pdu_create(SNMP_MSG_GET);
	snmp_add_null_var(pdu, oid_rssi, oid_rssi_length);
	if (!snmp_send(active_session, pdu))
		console_log("snmp error: error sending request to host %s\n", host);
	snmp_free_pdu(pdu);
	free(session.peername);
	free(session.community);
}

void snmp_process(void) {
    int nfds = 0;
    int block = 1;
    fd_set fdset;
    struct timeval timeout;

	if (active_session == NULL)
		return;

	FD_ZERO(&fdset);
	snmp_select_info(&nfds, &fdset, &timeout, &block);
	// Timeout is handled by daemon-poll.
	daemon_poll_setmaxtimeout(timeout.tv_sec*1000+timeout.tv_usec/1000);
	// As timeout is handled by daemon-poll, we want select() to return immediately here.
	timeout.tv_sec = timeout.tv_usec = 0;
	nfds = select(nfds, &fdset, NULL, NULL, &timeout);
	if (nfds > 0)
		snmp_read(&fdset);
	else
		snmp_timeout();
}

void snmp_init(void) {
	init_snmp(APPNAME);
	oid_rssi_length = MAX_OID_LEN;
	if (!read_objid(OID_RSSI, oid_rssi, &oid_rssi_length))
		console_log("snmp error: can't parse rssi oid (%s)\n", OID_RSSI);
}
