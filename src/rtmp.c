#include "rtmp.h"
#include "log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RTMP_SIG_SIZE 1536
#define RTMP_MAX_HEADER_SIZE 18

#define RTMP_TYPE_CHUNK_SIZE        0x01
#define RTMP_TYPE_PING              0x04
#define RTMP_TYPE_SERVER_BANDWIDTH  0x05
#define RTMP_TYPE_CLIENT_BANDWIDTH  0x06
#define RTMP_TYPE_AUDIO_PACKET      0x08
#define RTMP_TYPE_VIDEO_PACKET      0x09
#define RTMP_TYPE_AMF3_COMMAND      0x11
#define RTMP_TYPE_INVOKE_COMMAND    0x12
#define RTMP_TYPE_AMF0_COMMAND      0x14

enum rtmp_state {
	rtmp_state_uninitialized,
	rtmp_state_handshake_version,
	rtmp_state_handshake_ack,
	rtmp_state_handshake_done
};

struct rtmp_info {
	enum rtmp_state state;
	int client_version;
	uint32_t max_chunk_size;
};

void dump(char *str, size_t len)
{
    char *p = str;
    for (int n = 0; n < len; ++n)
    {
        printf("%2.2x ", *p);
        ++p;
    }

    printf("\n");
}

static struct rtmp_info *
rtmp_alloc_info()
{
	struct rtmp_info *info = malloc(sizeof(struct rtmp_info));
	info->max_chunk_size = 128;
	return info;
}

static uint8_t
rtmp_read_uint8(char *ptr) {
	return ptr[0];
}

static uint32_t
rtmp_read_uint24(char *ptr) {
	return (ptr[0] << 16) + (ptr[1] << 8) + ptr[2];
}

static uint32_t
rtmp_read_uint32(char *ptr) {
	uint32_t v = (ptr[0] << 16) + (ptr[1] << 8) + ptr[2];
	return ntohl(v);
}

static void
rtmp_chunk(struct rtmp_info *info, void *data, size_t len)
{
	char *ptr = data;
	uint8_t fmt, msg_type_id;
	uint16_t csid;
	uint32_t timestamp, msg_len, msg_stream_id;
	char buf[info->max_chunk_size];
	bzero(buf, info->max_chunk_size);

	dump(data, len);

	memcpy(&fmt, data, 1);
	csid = fmt & 0x3F;
	fmt = (fmt & 0xC0) >> 6;
	switch (csid) {
		case 0:
			memcpy(&csid, &data[1], 1);
			csid = csid + 64;
			ptr += 2;
			break;

		case 1:
			memcpy(&csid, &data[1], 2);
			csid = csid + 64;
			ptr += 3;
			break;

		default:
			ptr++;
			break;
	}

	log_debug("Chunk fmt: %d csid: %d", fmt, csid);

	if (fmt < 3) {
		timestamp = rtmp_read_uint24(ptr);
		msg_len = rtmp_read_uint24(&ptr[3]);
		msg_type_id = rtmp_read_uint8(&ptr[6]);
		msg_stream_id = rtmp_read_uint32(&ptr[7]);

		// Some weird shit about channel 03
		if (csid == 0x03) {
			msg_len = 256 - RTMP_MAX_HEADER_SIZE;
		}

		log_debug("Chunk 0 data timestamp: %d len: %d type: %d stream: %d", timestamp, msg_len, msg_type_id, msg_stream_id);

		memcpy(buf, &ptr[11], msg_len);
		log_debug("Message buf: ");
		dump(buf, msg_len);

		switch (msg_type_id) {
			case RTMP_TYPE_CHUNK_SIZE:
				memcpy(&info->max_chunk_size, buf, 4);
				info->max_chunk_size = htonl(info->max_chunk_size);
				log_debug("Set max chunk size: %d", info->max_chunk_size);

				break;
		}
	}
}

int
rtmp_read(struct conn_client *client, char *data, size_t len)
{
	char sbuf[RTMP_SIG_SIZE + 1], *psbuf = sbuf;
	uint32_t stime;

	if (client->proto_data == NULL) {
		client->proto_data = rtmp_alloc_info();
	}

	struct rtmp_info *info = client->proto_data;

	switch (info->state)
	{
		case rtmp_state_uninitialized:
			log_debug("Parsing C0");

			// Check client version
			info->client_version = data[0];
			log_debug("Client version: %d", info->client_version);

			sprintf(sbuf, "%d", RTMP_VERSION);
			conn_buffer_write(client, sbuf, 1);

			info->state = rtmp_state_handshake_version;

			if (len == 1) {
				break;
			}

			// The client send C1 as well
			data = data + 1;
			psbuf = sbuf + 1;

		case rtmp_state_handshake_version:
			log_debug("Parsing C1");

			// Zero Fucks Given about the client Handshake
			stime = 0;
			memcpy(psbuf, &stime, 4);
			memset(&psbuf[4], 0, 4);
			memcpy(&psbuf[8], &data[8], RTMP_SIG_SIZE - 8);

			conn_buffer_write(client, psbuf, RTMP_SIG_SIZE);
			info->state = rtmp_state_handshake_ack;

			break;

		case rtmp_state_handshake_ack:
			log_debug("Parsing C2");

			stime = 0;
			memcpy(psbuf, &stime, 4);
			memset(&psbuf[4], 0, 4);
			memcpy(&psbuf[8], &data[8], RTMP_SIG_SIZE - 8);

			conn_buffer_write(client, psbuf, RTMP_SIG_SIZE);
			info->state = rtmp_state_handshake_done;

			break;

		case rtmp_state_handshake_done:
			rtmp_chunk(info, data, len);

			break;
	}

	return 1;
}
