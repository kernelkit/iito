#ifndef _IITO_IPC_H
#define _IITO_IPC_H

/*
 * iitod listens on a UNIX stream socket.  A client sends one JSON
 * request terminated by a newline, iitod sends back one JSON reply,
 * also newline terminated, and closes the connection.
 *
 *   request: { "method": "status" }
 *            { "method": "locate", "params": { "enable": true, "timeout": 60 } }
 *   reply:   { "result": { ... } }  or  { "error": "reason" }
 */
#define IPC_SOCKET      "/run/iitod.sock"
#define IPC_GROUP       "wheel"
#define IPC_MAX_MSG     4096

#endif	/* _IITO_IPC_H */
