#include "ssj.h"
#include "dvalue.h"

#include "sockets.h"

struct dvalue
{
	enum dvalue_tag tag;
	union {
		double   float_value;
		int      int_value;
		dukptr_t ptr_value;
		struct {
			void*  data;
			size_t size;
		} buffer;
	};
};

static void
print_duktape_ptr(dukptr_t ptr)
{
	if (ptr.size == 8)  // x64 pointer
		printf("%016"PRIx64"h", (uint64_t)ptr.addr);
	else if (ptr.size == 4)  // x86 pointer
		printf("%08"PRIx32"h", (uint32_t)ptr.addr);
}

dvalue_t*
dvalue_new(dvalue_tag_t tag)
{
	dvalue_t* obj;

	obj = calloc(1, sizeof(dvalue_t));
	obj->tag = tag;
	return obj;
}

dvalue_t*
dvalue_new_float(double value)
{
	dvalue_t* obj;

	obj = calloc(1, sizeof(dvalue_t));
	obj->tag = DVALUE_FLOAT;
	obj->float_value = value;
	return obj;
}

dvalue_t*
dvalue_new_heapptr(dukptr_t value)
{
	dvalue_t* obj;

	obj = calloc(1, sizeof(dvalue_t));
	obj->tag = DVALUE_HEAPPTR;
	obj->ptr_value.addr = value.addr;
	obj->ptr_value.size = value.size;
	return obj;
}

dvalue_t*
dvalue_new_int(int value)
{
	dvalue_t* obj;

	obj = calloc(1, sizeof(dvalue_t));
	obj->tag = DVALUE_INT;
	obj->int_value = value;
	return obj;
}

dvalue_t*
dvalue_new_string(const char* value)
{
	dvalue_t* obj;

	obj = calloc(1, sizeof(dvalue_t));
	obj->tag = DVALUE_STRING;
	obj->buffer.data = strdup(value);
	obj->buffer.size = strlen(value);
	return obj;
}

dvalue_t*
dvalue_dup(const dvalue_t* src)
{
	dvalue_t* obj;

	obj = calloc(1, sizeof(dvalue_t));
	memcpy(obj, src, sizeof(dvalue_t));
	return obj;
}

void
dvalue_free(dvalue_t* obj)
{
	if (obj->tag == DVALUE_STRING || obj->tag == DVALUE_BUF)
		free(obj->buffer.data);
	free(obj);
}

dvalue_tag_t
dvalue_tag(const dvalue_t* obj)
{
	return obj->tag;
}

const char*
dvalue_as_cstr(const dvalue_t* obj)
{
	return obj->tag == DVALUE_STRING ? obj->buffer.data : NULL;
}

double
dvalue_as_float(const dvalue_t* obj)
{
	return obj->tag == DVALUE_FLOAT ? obj->float_value : 0.0;
}

dukptr_t
dvalue_as_ptr(const dvalue_t* obj)
{
	dukptr_t retval;

	memset(&retval, 0, sizeof(dukptr_t));
	if (obj->tag == DVALUE_PTR || obj->tag == DVALUE_HEAPPTR || obj->tag == DVALUE_OBJ || obj->tag == DVALUE_LIGHTFUNC) {
		retval.addr = obj->ptr_value.addr;
		retval.size = obj->ptr_value.size;
	}
	return retval;
}

int
dvalue_as_int(const dvalue_t* obj)
{
	return obj->tag == DVALUE_INT ? obj->int_value : 0;
}

void
dvalue_print(const dvalue_t* obj, bool is_verbose)
{
	switch (dvalue_tag(obj)) {
	case DVALUE_UNDEF: printf("undefined"); break;
	case DVALUE_UNUSED: printf("unused"); break;
	case DVALUE_NULL: printf("null"); break;
	case DVALUE_TRUE: printf("true"); break;
	case DVALUE_FALSE: printf("false"); break;
	case DVALUE_FLOAT: printf("%g", obj->float_value); break;
	case DVALUE_INT: printf("%d", obj->int_value); break;
	case DVALUE_STRING: printf("\"%s\"", (char*)obj->buffer.data); break;
	case DVALUE_BUF: printf("buf:%zd-bytes", obj->buffer.size); break;
	case DVALUE_HEAPPTR:
		printf("{ heap:\"");
		print_duktape_ptr(dvalue_as_ptr(obj));
		printf("\" }");
		break;
	case DVALUE_LIGHTFUNC:
		printf("{ lightfunc:\"");
		print_duktape_ptr(dvalue_as_ptr(obj));
		printf("\" }");
		break;
	case DVALUE_OBJ:
		if (!is_verbose)
			printf("{...}");
		else {
			printf("{ obj:\"");
			print_duktape_ptr(dvalue_as_ptr(obj));
			printf("\" }");
		}
		break;
	case DVALUE_PTR:
		printf("{ ptr:\"");
		print_duktape_ptr(dvalue_as_ptr(obj));
		printf("\" }");
		break;
	default:
		printf("*munch*");
	}
}

dvalue_t*
dvalue_recv(socket_t* socket)
{
	dvalue_t* obj;
	uint8_t data[32];
	uint8_t ib;

	ptrdiff_t i, j;

	obj = calloc(1, sizeof(dvalue_t));
	socket_recv(socket, &ib, 1);
	obj->tag = (enum dvalue_tag)ib;
	switch (ib) {
	case DVALUE_INT:
		socket_recv(socket, data, 4);
		obj->tag = DVALUE_INT;
		obj->int_value = (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
		break;
	case DVALUE_STRING:
		socket_recv(socket, data, 4);
		obj->buffer.size = (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
		obj->buffer.data = calloc(1, obj->buffer.size + 1);
		socket_recv(socket, obj->buffer.data, obj->buffer.size);
		obj->tag = DVALUE_STRING;
		break;
	case DVALUE_STRING16:
		socket_recv(socket, data, 2);
		obj->buffer.size = (data[0] << 8) + data[1];
		obj->buffer.data = calloc(1, obj->buffer.size + 1);
		socket_recv(socket, obj->buffer.data, obj->buffer.size);
		obj->tag = DVALUE_STRING;
		break;
	case DVALUE_BUF:
		socket_recv(socket, data, 4);
		obj->buffer.size = (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + data[3];
		obj->buffer.data = calloc(1, obj->buffer.size + 1);
		socket_recv(socket, obj->buffer.data, obj->buffer.size);
		obj->tag = DVALUE_BUF;
		break;
	case DVALUE_BUF16:
		socket_recv(socket, data, 2);
		obj->buffer.size = (data[0] << 8) + data[1];
		obj->buffer.data = calloc(1, obj->buffer.size + 1);
		socket_recv(socket, obj->buffer.data, obj->buffer.size);
		obj->tag = DVALUE_BUF;
		break;
	case DVALUE_FLOAT:
		socket_recv(socket, data, 8);
		((uint8_t*)&obj->float_value)[0] = data[7];
		((uint8_t*)&obj->float_value)[1] = data[6];
		((uint8_t*)&obj->float_value)[2] = data[5];
		((uint8_t*)&obj->float_value)[3] = data[4];
		((uint8_t*)&obj->float_value)[4] = data[3];
		((uint8_t*)&obj->float_value)[5] = data[2];
		((uint8_t*)&obj->float_value)[6] = data[1];
		((uint8_t*)&obj->float_value)[7] = data[0];
		obj->tag = DVALUE_FLOAT;
		break;
	case DVALUE_OBJ:
		socket_recv(socket, data, 1);
		socket_recv(socket, &obj->ptr_value.size, 1);
		socket_recv(socket, data, obj->ptr_value.size);
		obj->tag = DVALUE_OBJ;
		for (i = 0, j = obj->ptr_value.size - 1; j >= 0; ++i, --j)
			((uint8_t*)&obj->ptr_value.addr)[i] = data[j];
		break;
	case DVALUE_PTR:
		socket_recv(socket, &obj->ptr_value.size, 1);
		socket_recv(socket, data, obj->ptr_value.size);
		obj->tag = DVALUE_PTR;
		for (i = 0, j = obj->ptr_value.size - 1; j >= 0; ++i, --j)
			((uint8_t*)&obj->ptr_value.addr)[i] = data[j];
		break;
	case DVALUE_LIGHTFUNC:
		socket_recv(socket, data, 2);
		socket_recv(socket, &obj->ptr_value.size, 1);
		socket_recv(socket, data, obj->ptr_value.size);
		obj->tag = DVALUE_LIGHTFUNC;
		for (i = 0, j = obj->ptr_value.size - 1; j >= 0; ++i, --j)
			((uint8_t*)&obj->ptr_value.addr)[i] = data[j];
		break;
	case DVALUE_HEAPPTR:
		socket_recv(socket, &obj->ptr_value.size, 1);
		socket_recv(socket, data, obj->ptr_value.size);
		obj->tag = DVALUE_HEAPPTR;
		for (i = 0, j = obj->ptr_value.size - 1; j >= 0; ++i, --j)
			((uint8_t*)&obj->ptr_value.addr)[i] = data[j];
		break;
	default:
		if (ib >= 0x60 && ib <= 0x7F) {
			obj->tag = DVALUE_STRING;
			obj->buffer.size = ib - 0x60;
			obj->buffer.data = calloc(1, obj->buffer.size + 1);
			socket_recv(socket, obj->buffer.data, obj->buffer.size);
		}
		else if (ib >= 0x80 && ib <= 0xBF) {
			obj->tag = DVALUE_INT;
			obj->int_value = ib - 0x80;
		}
		else if (ib >= 0xC0) {
			socket_recv(socket, data, 1);
			obj->tag = DVALUE_INT;
			obj->int_value = ((ib - 0xC0) << 8) + data[0];
		}
	}
	return obj;
}

void
dvalue_send(const dvalue_t* obj, socket_t* socket)
{
	uint8_t  data[32];
	uint32_t str_length;

	ptrdiff_t i, j;

	data[0] = (uint8_t)obj->tag;
	socket_send(socket, data, 1);
	switch (obj->tag) {
	case DVALUE_INT:
		data[0] = (uint8_t)(obj->int_value >> 24 & 0xFF);
		data[1] = (uint8_t)(obj->int_value >> 16 & 0xFF);
		data[2] = (uint8_t)(obj->int_value >> 8 & 0xFF);
		data[3] = (uint8_t)(obj->int_value & 0xFF);
		socket_send(socket, data, 4);
		break;
	case DVALUE_STRING:
		str_length = (uint32_t)strlen(obj->buffer.data);
		data[0] = (uint8_t)(str_length >> 24 & 0xFF);
		data[1] = (uint8_t)(str_length >> 16 & 0xFF);
		data[2] = (uint8_t)(str_length >> 8 & 0xFF);
		data[3] = (uint8_t)(str_length & 0xFF);
		socket_send(socket, data, 4);
		socket_send(socket, obj->buffer.data, (int)str_length);
		break;
	case DVALUE_FLOAT:
		data[0] = ((uint8_t*)&obj->float_value)[7];
		data[1] = ((uint8_t*)&obj->float_value)[6];
		data[2] = ((uint8_t*)&obj->float_value)[5];
		data[3] = ((uint8_t*)&obj->float_value)[4];
		data[4] = ((uint8_t*)&obj->float_value)[3];
		data[5] = ((uint8_t*)&obj->float_value)[2];
		data[6] = ((uint8_t*)&obj->float_value)[1];
		data[7] = ((uint8_t*)&obj->float_value)[0];
		socket_send(socket, data, 8);
		break;
	case DVALUE_HEAPPTR:
		for (i = 0, j = obj->ptr_value.size - 1; j >= 0; ++i, --j)
			data[i] = ((uint8_t*)&obj->ptr_value.addr)[j];
		socket_send(socket, &obj->ptr_value.size, 1);
		socket_send(socket, data, obj->ptr_value.size);
		break;
	}
}