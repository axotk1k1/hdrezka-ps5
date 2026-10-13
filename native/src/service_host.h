/*
 * HdRezka - in-process HTTP UI server (no elfldr :9021 required).
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/* Starts http_server_run on a background thread. */
int hdrezka_start_embedded_server(int port);

/* After start (or failure): path used for www, or reason. */
const char *hdrezka_www_path(void);

#ifdef __cplusplus
}
#endif
