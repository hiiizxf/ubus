#include <arpa/inet.h>
#include "ubusd.h"

static struct blob_buf b;
static struct ubus_msg_buf *retmsg;
static int *retmsg_data;

static struct blob_attr *attrbuf[UBUS_ATTR_MAX];

typedef int (*ubus_cmd_cb)(struct ubus_client *cl, struct ubus_msg_buf *ub);

static const struct blob_attr_info ubus_policy[UBUS_ATTR_MAX] = {
	[UBUS_ATTR_SIGNATURE] = { .type = BLOB_ATTR_NESTED },
	[UBUS_ATTR_OBJTYPE] = { .type = BLOB_ATTR_INT32 },
	[UBUS_ATTR_OBJPATH] = { .type = BLOB_ATTR_STRING },
};

struct blob_attr **ubus_parse_msg(struct blob_attr *msg)
{
	blob_parse(msg, attrbuf, ubus_policy, UBUS_ATTR_MAX);
	return attrbuf;
}

static void ubus_msg_init(struct ubus_msg_buf *ub, uint8_t type, uint16_t seq, uint32_t peer)
{
	ub->hdr.version = 0;
	ub->hdr.type = type;
	ub->hdr.seq = seq;
	ub->hdr.peer = peer;
}

static struct ubus_msg_buf *ubus_msg_from_blob(bool shared)
{
	return ubus_msg_new(b.head, blob_raw_len(b.head), shared);
}

static struct ubus_msg_buf *ubus_reply_from_blob(struct ubus_msg_buf *ub, bool shared)
{
	struct ubus_msg_buf *new;

	new = ubus_msg_new(b.head, blob_raw_len(b.head), shared);
	if (!new)
		return NULL;

	ubus_msg_init(new, UBUS_MSG_DATA, ub->hdr.seq, ub->hdr.peer);
	return new;
}

bool ubusd_send_hello(struct ubus_client *cl)
{
	struct ubus_msg_buf *ub;

	blob_buf_init(&b, 0);
	ub = ubus_msg_from_blob(true);
	if (!ub)
		return false;

	ubus_msg_init(ub, UBUS_MSG_HELLO, 0, cl->id.id);
	ubus_msg_send(cl, ub);
	return true;
}

static int ubusd_send_pong(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
	ub->hdr.type = UBUS_MSG_DATA;
	ubus_msg_send(cl, ubus_msg_ref(ub));
	return 0;
}

static int ubusd_handle_publish(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
	struct ubus_object *obj;
	struct blob_attr **attr;

	attr = ubus_parse_msg(ub->data);
	obj = ubusd_create_object(cl, attr);
	if (!obj)
		return UBUS_STATUS_INVALID_ARGUMENT;

	blob_buf_init(&b, 0);
	blob_put_int32(&b, UBUS_ATTR_OBJID, obj->id.id);
	if (attr[UBUS_ATTR_SIGNATURE])
		blob_put_int32(&b, UBUS_ATTR_OBJTYPE, obj->type->id.id);

	ub = ubus_reply_from_blob(ub, true);
	if (!ub)
		return UBUS_STATUS_NO_DATA;

	ubus_msg_send(cl, ub);
	return 0;
}

static void ubusd_send_obj(struct ubus_client *cl, struct ubus_msg_buf *ub, struct ubus_object *obj)
{
	struct ubus_method *m;
	void *s;

	blob_buf_init(&b, 0);

	if (obj->path.key)
		blob_put_string(&b, UBUS_ATTR_OBJPATH, obj->path.key);
	blob_put_int32(&b, UBUS_ATTR_OBJID, obj->id.id);

	s = blob_nest_start(&b, UBUS_ATTR_SIGNATURE);
	list_for_each_entry(m, &obj->type->methods, list)
		blob_put(&b, blob_id(m->data), blob_data(m->data), blob_len(m->data));
	blob_nest_end(&b, s);

	ub = ubus_reply_from_blob(ub, true);
	if (!ub)
		return;

	ubus_msg_send(cl, ub);
}

static int ubusd_handle_lookup(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
	struct ubus_object *obj;
	struct blob_attr **attr;
	char *objpath;
	bool wildcard = false;
	bool found = false;
	int len;

	attr = ubus_parse_msg(ub->data);
	if (!attr[UBUS_ATTR_OBJPATH]) {
		avl_for_each_element(&path, obj, path)
			ubusd_send_obj(cl, ub, obj);
		return 0;
	}

	objpath = blob_data(attr[UBUS_ATTR_OBJPATH]);
	len = strlen(objpath);
	if (objpath[len - 1] != '*') {
		obj = avl_find_element(&path, objpath, obj, path);
		if (!obj)
			return UBUS_STATUS_NOT_FOUND;

		ubusd_send_obj(cl, ub, obj);
		return 0;
	}

	objpath[--len] = 0;
	wildcard = true;

	obj = avl_find_ge_element(&path, objpath, obj, path);
	if (!obj)
		return UBUS_STATUS_NOT_FOUND;

	while (!strncmp(objpath, obj->path.key, len)) {
		found = true;
		ubusd_send_obj(cl, ub, obj);
		if (obj == avl_last_element(&path, obj, path))
			break;
		obj = avl_next_element(obj, path);
	}

	if (!found)
		return UBUS_STATUS_NOT_FOUND;

	return 0;
}

static int ubusd_handle_invoke(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
	return UBUS_STATUS_NOT_FOUND;
}

static const ubus_cmd_cb handlers[__UBUS_MSG_LAST] = {
	[UBUS_MSG_PING] = ubusd_send_pong,
	[UBUS_MSG_PUBLISH] = ubusd_handle_publish,
	[UBUS_MSG_LOOKUP] = ubusd_handle_lookup,
	[UBUS_MSG_INVOKE] = ubusd_handle_invoke,
};

void ubusd_receive_message(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
	ubus_cmd_cb cb = NULL;
	int ret;

	retmsg->hdr.seq = ub->hdr.seq;
	retmsg->hdr.peer = ub->hdr.peer;

	if (ub->hdr.type < __UBUS_MSG_LAST)
		cb = handlers[ub->hdr.type];

	if (cb)
		ret = cb(cl, ub);
	else
		ret = UBUS_STATUS_INVALID_COMMAND;

	ubus_msg_free(ub);

	*retmsg_data = htonl(ret);
	ubus_msg_send(cl, ubus_msg_ref(retmsg));
}

static void __init ubusd_proto_init(void)
{
	blob_buf_init(&b, 0);
	blob_put_int32(&b, UBUS_ATTR_STATUS, 0);

	retmsg = ubus_msg_from_blob(false);
	if (!retmsg)
		exit(1);

	retmsg->hdr.type = UBUS_MSG_STATUS;
	retmsg_data = blob_data(blob_data(retmsg->data));
}
