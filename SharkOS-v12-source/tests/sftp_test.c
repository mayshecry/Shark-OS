#include "kernel.h"
#include "sftp.h"
#include <stdio.h>
#include <stdlib.h>

struct fs_node node_pool[MAX_NODES];
int pool_index;
struct fs_node* root;
struct fs_node* current_dir;
static sftp_session_t session;
static uint8_t response[9000];
static uint32_t response_length;

void* kmalloc(size_t size) {
    return malloc(size);
}

void int_to_string(uint32_t value, char* output) {
    char digits[16];
    uint32_t count = 0;
    uint32_t number = (uint32_t)value;
    if (!number) {
        output[0] = '0';
        output[1] = 0;
        return;
    }
    while (number) {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    }
    for (uint32_t i = 0; i < count; i++) output[i] = digits[count - i - 1];
    output[count] = 0;
}

static int send_capture(void* context, const uint8_t* data, uint32_t length) {
    (void)context;
    if (length > sizeof(response)) return -1;
    memcpy(response, data, length);
    response_length = length;
    return 0;
}

static void put_u32(uint8_t* data, uint32_t* length, uint32_t value) {
    data[(*length)++] = (uint8_t)(value >> 24);
    data[(*length)++] = (uint8_t)(value >> 16);
    data[(*length)++] = (uint8_t)(value >> 8);
    data[(*length)++] = (uint8_t)value;
}

static void put_u64(uint8_t* data, uint32_t* length, uint64_t value) {
    put_u32(data, length, (uint32_t)(value >> 32));
    put_u32(data, length, (uint32_t)value);
}

static void put_string(uint8_t* data, uint32_t* length, const uint8_t* value, uint32_t size) {
    put_u32(data, length, size);
    if (size) memcpy(data + *length, value, size);
    *length += size;
}

static int feed_request(const uint8_t* body, uint32_t body_length) {
    uint8_t packet[65540];
    uint32_t packet_length = body_length;
    packet[0] = (uint8_t)(packet_length >> 24);
    packet[1] = (uint8_t)(packet_length >> 16);
    packet[2] = (uint8_t)(packet_length >> 8);
    packet[3] = (uint8_t)packet_length;
    memcpy(packet + 4, body, body_length);
    response_length = 0;
    return sftp_session_feed(&session, packet, packet_length + 4, send_capture, NULL);
}

static uint8_t pattern_byte(uint32_t offset) {
    return (uint8_t)((offset * 131u + (offset >> 8) + 17u) & 255u);
}

int main(void) {
    root = create_node("/", FS_DIRECTORY, NULL);
    if (!root) return 1;
    current_dir = root;
    sftp_session_init(&session, root);

    uint8_t init_body[5] = {1, 0, 0, 0, 3};
    if (feed_request(init_body, sizeof(init_body)) != 0 || response_length < 9 || response[4] != 2) return 2;

    uint8_t request[65540];
    uint32_t n = 0;
    request[n++] = 3;
    put_u32(request, &n, 1);
    put_string(request, &n, (const uint8_t*)"/large.bin", 10);
    put_u32(request, &n, 27);
    put_u32(request, &n, 1);
    put_u64(request, &n, 6u * 1024u * 1024u + 123u);
    if (feed_request(request, n) != 0 || response_length < 17 || response[4] != 102) return 3;
    uint8_t handle[4];
    memcpy(handle, response + 13, sizeof(handle));

    uint32_t total = 6u * 1024u * 1024u + 123u;
    uint32_t offset = 0;
    uint8_t data[32768];
    while (offset < total) {
        uint32_t count = total - offset;
        if (count > sizeof(data)) count = sizeof(data);
        for (uint32_t i = 0; i < count; i++) data[i] = pattern_byte(offset + i);
        n = 0;
        request[n++] = 6;
        put_u32(request, &n, offset / sizeof(data) + 2);
        put_string(request, &n, handle, sizeof(handle));
        put_u64(request, &n, offset);
        put_string(request, &n, data, count);
        if (feed_request(request, n) != 0 || response_length < 13 || response[4] != 101 || response[9] || response[10] || response[11] || response[12]) return 4;
        offset += count;
    }

    struct fs_node* file = find_node(root, "large.bin");
    if (!file || file->content_len != (int)total) return 5;
    for (uint32_t i = 0; i < total; i++) if ((uint8_t)file->content[i] != pattern_byte(i)) return 6;

    offset = 0;
    uint32_t request_id = 1000;
    while (offset < total) {
        n = 0;
        request[n++] = 5;
        put_u32(request, &n, request_id++);
        put_string(request, &n, handle, sizeof(handle));
        put_u64(request, &n, offset);
        put_u32(request, &n, 32768);
        if (feed_request(request, n) != 0 || response_length < 13 || response[4] != 103) return 7;
        uint32_t count = ((uint32_t)response[9] << 24) | ((uint32_t)response[10] << 16) | ((uint32_t)response[11] << 8) | response[12];
        if (!count || count > total - offset || response_length != count + 13) return 8;
        for (uint32_t i = 0; i < count; i++) if (response[13 + i] != pattern_byte(offset + i)) return 9;
        offset += count;
    }

    printf("SFTP binary read/write, embedded NUL bytes, and 6 MiB transfer passed\n");
    return 0;
}
