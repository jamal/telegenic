#ifndef __TELEGENIC_RTMP_H__
#define __TELEGENIC_RTMP_H__

#include "conn.h"

#define RTMP_VERSION 3

int rtmp_read(struct conn_client *client, char *data, size_t len);

#endif