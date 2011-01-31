#ifndef __UBUSMSG_H
#define __UBUSMSG_H

#include <stdint.h>
#include <libubox/blob.h>

#define __packetdata __attribute__((packed)) __attribute__((__aligned__(4)))

#define UBUS_MAX_MSGLEN	65535

struct ubus_msghdr {
	uint8_t version;
	uint8_t type;
	uint16_t seq;
	uint32_t peer;
	struct blob_attr data[];
} __packetdata;

enum ubus_msg_type {
	/* initial server message */
	UBUS_MSG_HELLO,

	/* generic command response */
	UBUS_MSG_STATUS,

	/* data message response */
	UBUS_MSG_DATA,

	/* ping request */
	UBUS_MSG_PING,

	/* look up one or more objects */
	UBUS_MSG_LOOKUP,

	/* invoke a method on a single object */
	UBUS_MSG_INVOKE,

	/* publish an object */
	UBUS_MSG_PUBLISH,

	/* must be last */
	__UBUS_MSG_LAST,
};

enum ubus_msg_attr {
	UBUS_ATTR_UNSPEC,

	UBUS_ATTR_STATUS,

	UBUS_ATTR_OBJPATH,
	UBUS_ATTR_OBJID,
	UBUS_ATTR_METHOD,

	UBUS_ATTR_OBJTYPE,
	UBUS_ATTR_SIGNATURE,

	UBUS_ATTR_DATA,

	/* must be last */
	UBUS_ATTR_MAX,
};

enum ubus_msg_status {
	UBUS_STATUS_OK,
	UBUS_STATUS_INVALID_COMMAND,
	UBUS_STATUS_INVALID_ARGUMENT,
	UBUS_STATUS_METHOD_NOT_FOUND,
	UBUS_STATUS_NOT_FOUND,
	UBUS_STATUS_NO_DATA,
	__UBUS_STATUS_LAST
};

#endif