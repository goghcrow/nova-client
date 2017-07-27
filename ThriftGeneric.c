#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "binarydata.h"
#include "thriftgeneric.h"

#define BUF_OFS (uchar_t *)buf + off
#define C_BUF_OFS (const uchar *)buf + off

int thrift_generic_pack(int seq,
         const char *serv, int serv_len,
         const char *method, int method_len,
         const char *json_args, int args_len, char **out_buf)
{
    int off = 0;
    char *buf = malloc(GENERIC_COMMON_LEN + serv_len + method_len + args_len);
    if (buf == NULL)
    {
        fprintf(stderr, "malloc failed");
        return 0;
    }

    swWriteU32(BUF_OFS, VER1 | T_CALL);
    off += 4;

    swWriteU32(BUF_OFS, GENERIC_METHOD_LEN);
    off += 4;

    swWriteBytes(BUF_OFS, GENERIC_METHOD, GENERIC_METHOD_LEN);
    off += GENERIC_METHOD_LEN;

    swWriteU32(BUF_OFS, seq);
    off += 4;

    { // pack args
        uint16_t field_id = 1;
        char field_type = TYPE_STRUCT; // uchar

        swWriteByte(BUF_OFS, field_type);
        off += 1;

        swWriteU16(BUF_OFS, field_id);
        off += 2;
    }

    { // pack struct \Com\Youzan\Nova\Framework\Generic\Service\GenericRequest
        {
            uint16_t field_id = 1;
            char field_type = TYPE_STRING; // uchar

            swWriteByte(BUF_OFS, field_type);
            off += 1;

            swWriteU16(BUF_OFS, field_id);
            off += 2;

            swWriteU32(BUF_OFS, serv_len);
            off += 4;

            swWriteBytes(BUF_OFS, serv, serv_len);
            off += serv_len;
        }

        {
            uint16_t field_id = 2;
            char field_type = TYPE_STRING; // uchar

            swWriteByte(BUF_OFS, field_type);
            off += 1;

            swWriteU16(BUF_OFS, field_id);
            off += 2;

            swWriteU32(BUF_OFS, method_len);
            off += 4;

            swWriteBytes(BUF_OFS, method, method_len);
            off += method_len;
        }

        {
            uint16_t field_id = 3;
            char field_type = TYPE_STRING; // uchar

            swWriteByte(BUF_OFS, field_type);
            off += 1;

            swWriteU16(BUF_OFS, field_id);
            off += 2;

            swWriteU32(BUF_OFS, args_len);
            off += 4;

            swWriteBytes(BUF_OFS, json_args, args_len);
            off += args_len;
        }

        swWriteByte(BUF_OFS, FIELD_STOP);
        off += 1;
    }

    swWriteByte(BUF_OFS, FIELD_STOP);
    off += 1;

    *out_buf = buf;
    return off;
}

int thrift_generic_unpack(const char *buf, int buf_len, char **out_json_resp)
{
    int off = 0;
    uint32_t ver1;
    int type;
    uint32_t tmp_len;
    char *tmp_str;
    uint32_t seq;
    uchar_t field_type;
    uint16_t field_id;

    swReadU32(C_BUF_OFS, &ver1);
    off += 4;

    if (ver1 > 0x7fffffff)
    {
        ver1 = 0 - ((ver1 - 1) ^ 0xffffffff);
    }
    ver1 = ver1 & VER_MASK;
    assert(ver1 == VER1);

    type = ver1 & 0x000000ff;
    if (type == T_EX)
    {
        fprintf(stderr, "unexpected thrift exception response\n");
        return 0;
    }

    swReadU32(C_BUF_OFS, &tmp_len);
    off += 4;
    tmp_str = NULL;
    if (!swReadBytes(C_BUF_OFS, buf_len - off, &tmp_str, (int)tmp_len))
    {
        fprintf(stderr, "fail to read generic method\n");
        return 0;
    }
    off += tmp_len;
    assert(strncmp(tmp_str, GENERIC_METHOD, GENERIC_COMMON_LEN) == 0);
    free(tmp_str);

    swReadU32(C_BUF_OFS, &seq);
    off += 4;

    swReadByte(C_BUF_OFS, (char *)&field_type);
    off += 1;
    assert(field_type == TYPE_STRING);

    swReadU16(C_BUF_OFS, &field_id);
    off += 2;
    assert(field_id == 0);

    swReadU32(C_BUF_OFS, &tmp_len);
    off += 4;

    tmp_str = NULL;
    if (!swReadBytes(C_BUF_OFS, buf_len - off, &tmp_str, tmp_len))
    {
        fprintf(stderr, "fail to read json resp\n");
        return 0;
    }
    off += tmp_len;
    *out_json_resp = tmp_str;

    swReadByte(C_BUF_OFS, (char *)&field_type);
    assert(field_type == FIELD_STOP);
    off += 1;

    assert(off == buf_len);

    return tmp_len;
}