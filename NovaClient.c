#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>

#include "cJSON.h"
#include "thriftgeneric.h"
#include "nova.h"
#include "binarydata.h"

#define RECV_BUF_SIZE 8192

static const char *usage =
    "\nUsage: nova -h<HOST> -p<PORT> -m<METHOD> -a<JSON_ARGUMENTS> [-e<JSON_ATTACHMENT='{}'> -t<TIMEOUT_SEC=5>]\n"
    "   nova -h127.0.0.1 -p8050 -m=com.youzan.material.general.service.TokenService.getToken -a='{\"xxxId\":1,\"scope\":\"\"}'\n"
    "   nova -h127.0.0.1 -p8050 -m=com.youzan.material.general.service.TokenService.getToken -a='{\"xxxId\":1,\"scope\":\"\"}' -e='{\"xxxId\":1}'\n"
    "   nova -h127.0.0.1 -p8050 -m=com.youzan.material.general.service.MediaService.getMediaList -a='{\"query\":{\"categoryId\":2,\"xxxId\":1,\"pageNo\":1,\"pageSize\":5}}'\n"
    "   nova -hqabb-dev-scrm-test0 -p8100 -mcom.youzan.scrm.customer.service.customerService.getByYzUid -a '{\"xxxId\":1, \"yzUid\": 1}'\n";

struct globalArgs_t
{
    int debug;
    const char *host;
    int port;
    const char *service;
    const char *method;
    const char *args;   /* JSON */
    const char *attach; /* JSON */
    struct timeval timeout;
} globalArgs;

static const char *optString = "h:p:m:a:e:t:?!";

#define INVALID_OPT(reason, ...)                                     \
    fprintf(stderr, "\x1B[1;31m" reason "\x1B[0m\n", ##__VA_ARGS__); \
    display_usage();
static void display_usage()
{
    puts(usage);
    exit(1);
}

static void error(char *msg)
{
    perror(msg);
    exit(0);
}

#define DUMP_STRUCT(sp) bin2hex((const char *)(sp), sizeof(*(sp)))
#define DUMP_MEM(vp, n) bin2hex((const char *)(vp), (size_t)(n))
static void bin2hex(const char *vp, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
    {
        printf("%02x", (unsigned char)vp[i]);
    }
    putchar('\n');
}

// remove prefix = sapce and remove suffix space
static char *trim_opt(char *opt)
{
    char *end;
    if (opt == 0) {
        return 0;
    }
    
    while (isspace((int)*opt) || *opt == '=') opt++;

    if (*opt == 0) {
        return opt;
    }

    end = opt + strlen(opt) - 1;
    while(end > opt && isspace((int)*end)) end--;

    *(end + 1) = 0;

    return opt;
}

static void nova_invoke()
{
    int sockfd;
    struct sockaddr_in sin = {0};
    struct in_addr tmp;
    struct hostent *host_entry;

    swNova_Header *nova_hdr;
    char *thrift_buf;

    char *nova_buf;
    ssize_t send_n;
    int32_t nova_pkt_len;

    char *recv_buf;
    ssize_t recv_msg_size;
    int recv_n;
    int recv_left;

    char *resp_json;

    char *tmp_buf;

    int buf_len = thrift_generic_pack(0,
                                      globalArgs.service, strlen(globalArgs.service),
                                      globalArgs.method, strlen(globalArgs.method),
                                      globalArgs.args, strlen(globalArgs.args), &thrift_buf);

    if (buf_len == 0)
    {
        fprintf(stderr, "ERROR, fail to pack thrift\n");
        exit(1);
    }

    nova_hdr = createNovaHeader();
    if (nova_hdr == NULL)
    {
        fprintf(stderr, "ERROR, fail to create nova header\n");
        exit(1);
    }

    nova_hdr->magic = NOVA_MAGIC;
    nova_hdr->version = 1;
    nova_hdr->ip = 0;
    nova_hdr->port = 0;

    nova_hdr->service_len = GENERIC_SERVICE_LEN;
    nova_hdr->method_len = GENERIC_METHOD_LEN;
    nova_hdr->attach_len = strlen(globalArgs.attach);
    int headLen = NOVA_HEADER_COMMON_LEN + nova_hdr->service_len + nova_hdr->method_len + nova_hdr->attach_len;
    if (headLen > 0x7fff)
    {
        fprintf(stderr, "ERROR, too large nova header as %d\n", headLen);
        exit(1);
    }
    nova_hdr->head_size = (int16_t)headLen;
    nova_hdr->service_name = malloc(nova_hdr->service_len + 1);
    memcpy(nova_hdr->service_name, GENERIC_SERVICE, nova_hdr->service_len);
    nova_hdr->service_name[nova_hdr->service_len] = 0;

    nova_hdr->method_name = malloc(nova_hdr->method_len + 1);
    memcpy(nova_hdr->method_name, GENERIC_METHOD, nova_hdr->method_len);
    nova_hdr->method_name[nova_hdr->method_len] = 0;
    nova_hdr->seq_no = 1;

    nova_hdr->attach = malloc(nova_hdr->attach_len + 1);
    memcpy(nova_hdr->attach, globalArgs.attach, nova_hdr->attach_len);
    nova_hdr->attach[nova_hdr->attach_len] = 0;

    nova_buf = NULL;
    if (!swNova_pack(nova_hdr, thrift_buf, buf_len, &nova_buf, &nova_pkt_len))
    {
        fprintf(stderr, "ERROR, fail to pack nova\n");
        exit(1);
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short int)globalArgs.port);
    if (inet_aton(globalArgs.host, &tmp))
    {
        sin.sin_addr.s_addr = tmp.s_addr;
    }
    else
    {
        if (!(host_entry = gethostbyname(globalArgs.host)))
        {
            fprintf(stderr, "ERROR, no such host as %s\n", globalArgs.host);
        }
        memcpy(&(sin.sin_addr.s_addr), host_entry->h_addr_list[0], host_entry->h_length);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        error("ERROR opening socket");
    }

    if (connect(sockfd, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) < 0)
    {
        error("ERROR connecting");
    }

    if (globalArgs.debug)
    {
        DUMP_MEM(nova_buf, nova_pkt_len);
    }

    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&globalArgs.timeout, sizeof(struct timeval));
    tmp_buf = nova_buf;
    while (nova_pkt_len > 0)
    {
        send_n = send(sockfd, tmp_buf, nova_pkt_len, 0);
        if (send_n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            error("ERROR sending");
        }
        tmp_buf += send_n;
        nova_pkt_len -= send_n;
    }

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&globalArgs.timeout, sizeof(struct timeval));
    recv_buf = (char *)malloc(RECV_BUF_SIZE);
    tmp_buf = recv_buf;
    recv_n = recv(sockfd, tmp_buf, RECV_BUF_SIZE, 0);
    if (recv_n < 4)
    {
        error("ERROR receiving");
    }
    swReadI32((const uchar *)tmp_buf, (int32_t *)&recv_msg_size);
    recv_left = recv_msg_size - recv_n;
    tmp_buf += recv_n;
    while (recv_left > 0)
    {
        recv_n = recv(sockfd, tmp_buf, RECV_BUF_SIZE, 0);
        if (recv_n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            error("ERROR receiving");
        }
        tmp_buf += recv_n;
        recv_left -= recv_n;
    }

    if (globalArgs.debug)
    {
        DUMP_MEM(recv_buf, recv_msg_size);
    }

    if (!swNova_IsNovaPack(recv_buf, recv_msg_size))
    {
        DUMP_MEM(recv_buf, recv_msg_size);
        fprintf(stderr, "ERROR, invalid nova packet\n");
        exit(1);
    }
    if (!swNova_unpack(recv_buf, recv_msg_size, nova_hdr))
    {
        deleteNovaHeader(nova_hdr);
        DUMP_MEM(recv_buf, recv_msg_size);
        fprintf(stderr, "ERROR, fail to unpcak nova packet header\n");
        exit(1);
    }

    if (!thrift_generic_unpack(recv_buf + nova_hdr->head_size, nova_hdr->msg_size - nova_hdr->head_size, &resp_json))
    {
        DUMP_MEM(recv_buf, recv_msg_size);
        fprintf(stderr, "ERROR, fail to unpack thrift packet\n");
        exit(1);
    }

    printf("%s", resp_json);

    deleteNovaHeader(nova_hdr);
    close(sockfd);
    free(nova_buf);
    free(thrift_buf);
    free(recv_buf);
}

int main(int argc, char **argv)
{
    int opt = 0;

    char *ret = NULL;
    size_t method_len = 0;
    size_t service_len = 0;

    // 默认attach
    globalArgs.attach = "{}";
    globalArgs.debug = 0;
    globalArgs.timeout.tv_sec = 5;
    globalArgs.timeout.tv_usec = 0;

    opt = getopt(argc, argv, optString);
    optarg = trim_opt(optarg);
    while (opt != -1)
    {
        switch (opt)
        {
        case 'h':
            globalArgs.host = optarg;
            break;
        case 'p':
            globalArgs.port = atoi(optarg);
            break;
        case 'm':
            ret = strrchr(optarg, '.');
            if (ret == NULL)
            {
                INVALID_OPT("Invalid method %s", optarg);
            }

            service_len = ret - optarg;
            method_len = strlen(optarg) - service_len - 1;
            *ret = 0;
            globalArgs.service = malloc(service_len + 1);
            globalArgs.method = malloc(method_len + 1);
            memcpy((void *)globalArgs.service, optarg, service_len + 1);
            memcpy((void *)globalArgs.method, ret + 1, method_len + 1);

            break;
        case 'a':
            globalArgs.args = optarg;
            break;
        case 'e':
            globalArgs.attach = optarg;
            break;
        case 't':
            globalArgs.timeout.tv_sec = atoi(optarg) > 0 ? atoi(optarg) : 5;
            break;
        case '?':
            display_usage();
            break;
        case '!':
            globalArgs.debug = 1;
            break;
        default:
            break;
        }
        opt = getopt(argc, argv, optString);
        optarg = trim_opt(optarg);
    }

    if (globalArgs.host == NULL)
    {
        INVALID_OPT("Missing Host");
    }

    if (globalArgs.port <= 0)
    {
        INVALID_OPT("Missing Port");
    }

    if (globalArgs.service == NULL)
    {
        INVALID_OPT("Missing Service");
    }

    if (globalArgs.method == NULL)
    {
        INVALID_OPT("Missing Method");
    }

    if (globalArgs.args == NULL)
    {
        INVALID_OPT("Missing Arguments");
    }
    else
    {
        cJSON *root = cJSON_Parse(globalArgs.args);
        if (root == NULL)
        {
            INVALID_OPT("Invalid Arguments JSON Format as %s", globalArgs.args);
        }
        else if (!cJSON_IsObject(root))
        {
            INVALID_OPT("Invalid Arguments JSON Format as %s", globalArgs.args);
        }
        else
        {
            // 泛化调用参数为扁平KV结构, 非标量参数要二次打包
            cJSON *cur = root->child;
            while (cur)
            {
                if (cJSON_IsArray(cur) || cJSON_IsObject(cur))
                {
                    cJSON_ReplaceItemInObject(root, cur->string, cJSON_CreateString(cJSON_PrintUnformatted(cur)));
                }

                cur = cur->next;
            }

            globalArgs.args = cJSON_PrintUnformatted(root);
        }
    }

    if (globalArgs.attach != NULL)
    {
        cJSON *root = cJSON_Parse(globalArgs.attach);
        if (root == NULL)
        {
            INVALID_OPT("Invalid Attach JSON Format as %s", globalArgs.attach);
        }
        else if (!cJSON_IsObject(root))
        {
            INVALID_OPT("Invalid Attach JSON Format as %s", globalArgs.attach);
        }
    }

    if (globalArgs.debug)
    {
        fprintf(stderr, "invoking nova://%s:%d/%s.%s\n",
                globalArgs.host,
                globalArgs.port,
                globalArgs.service,
                globalArgs.method);

        fprintf(stderr, "args=%s&attach=%s\n\n",
                globalArgs.args,
                globalArgs.attach);
    }

    nova_invoke();

    return 0;
}
