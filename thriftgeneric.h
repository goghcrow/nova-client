#ifndef _THRIFT_GENERIC_H_
#define _THRIFT_GENERIC_H_

#define GENERIC_SERVICE "com.youzan.nova.framework.generic.service.GenericService"
#define GENERIC_SERVICE_LEN 56
#define GENERIC_METHOD "invoke"
#define GENERIC_METHOD_LEN 6
#define GENERIC_COMMON_LEN 44

#define VER_MASK 0xffff0000
#define VER1 0x80010000

#define T_CALL 1
#define T_REPLY 2
#define T_EX 3

#define TYPE_STRUCT 12
#define TYPE_STRING 11

#define FIELD_STOP 0

int thrift_generic_pack(int seq,
         const char *service_name, int service_name_len,
         const char *method_name, int method_name_len,
         const char *json_args, int json_args_len,
         char **out_buf);

int thrift_generic_unpack(const char *buf, int buf_len, char **out_json_resp);

#endif