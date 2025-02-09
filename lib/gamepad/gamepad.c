#define _GNU_SOURCE

#include "gamepad.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "command.h"
#include "input.h"
#include "video.h"

#include "../pipe/def.h"
#include "status.h"
#include "util.h"

static const uint32_t STOP_CODE = 0xCAFEBABE;
static uint32_t SERVER_ADDRESS = 0;

uint16_t PORT_MSG;
uint16_t PORT_VID;
uint16_t PORT_AUD;
uint16_t PORT_HID;
uint16_t PORT_CMD;

#define EVENT_BUFFER_SIZE 65536
#define EVENT_BUFFER_ARENA_SIZE VANILLA_MAX_EVENT_COUNT * 2
uint8_t *EVENT_BUFFER_ARENA[EVENT_BUFFER_ARENA_SIZE] = {0};
pthread_mutex_t event_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

void send_to_console(int fd, const void *data, size_t data_size, int port)
{
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = SERVER_ADDRESS;
    address.sin_port = htons((uint16_t) (port - 100));

    ssize_t sent = sendto(fd, data, data_size, 0, (const struct sockaddr *) &address, sizeof(address));
    if (sent == -1) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, ip, INET_ADDRSTRLEN);
        print_info("Failed to send to Wii U socket: address: %s, fd: %d, port: %d, error: %i", ip, fd, port - 100, vanilla_socket_error());
    }
}

int create_socket(socket_t *socket_out, uint16_t port)
{
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    socket_t skt = socket(AF_INET, SOCK_DGRAM, 0);
    if (skt == -1) {
        print_info("FAILED TO CREATE SOCKET FOR PORT %i", port);
        return 0;
    }

    (*socket_out) = skt;
    
    if (bind((*socket_out), (const struct sockaddr *) &address, sizeof(address)) == -1) {
        print_info("FAILED TO BIND PORT %u: %i", port, vanilla_socket_error());
        return 0;
    }

    print_info("SUCCESSFULLY BOUND SOCKET %i on PORT %i", skt, port);
    return 1;
}

void send_stop_code(socket_t from_socket, in_port_t port)
{
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    address.sin_port = htons(port);
    // uint32_t* --explicit--> void* --implicit--> char*
    sendto(from_socket, (void*)&STOP_CODE, sizeof(STOP_CODE), 0, (struct sockaddr *)&address, sizeof(address));
}

int send_pipe_cc(socket_t skt, uint32_t cc, int wait_for_reply)
{
    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = SERVER_ADDRESS;
    addr.sin_port = htons(VANILLA_PIPE_CMD_SERVER_PORT);

    ssize_t read_size;
    uint32_t send_cc = htonl(cc);
    uint32_t recv_cc;

    do {
        sendto(skt, (void*)&send_cc, sizeof(send_cc), 0, (struct sockaddr *) &addr, sizeof(addr));

        if (!wait_for_reply) {
            return 1;
        }
        
        read_size = recv(skt, (void*)&recv_cc, sizeof(recv_cc), 0);
        if (read_size == sizeof(recv_cc) && ntohl(recv_cc) == VANILLA_PIPE_CC_BIND_ACK) {
            return 1;
        }

        vanilla_sleep(1000);
    } while (!is_interrupted());
    
    return 0;
}

int connect_to_backend(socket_t *socket, uint32_t cc)
{
    // Try to bind with backend
    socket_t pipe_cc_skt;
    if (!create_socket(&pipe_cc_skt, VANILLA_PIPE_CMD_CLIENT_PORT)) {
        return VANILLA_ERROR;
    }

    #if defined(UNIX)
    struct timeval tv = {0};
    tv.tv_sec = 2;
    setsockopt(pipe_cc_skt, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    #elif defined(_WIN32)
    DWORD timeout = 2000;
    // Here I am explicitly casting to const char*, because this is Windows specific code.
    setsockopt(pipe_cc_skt, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    #endif

    if (!send_pipe_cc(pipe_cc_skt, cc, 1)) {
        print_info("FAILED TO BIND TO PIPE");
        vanilla_close_socket(pipe_cc_skt);
        return VANILLA_ERROR;
    }

    *socket = pipe_cc_skt;

    return VANILLA_SUCCESS;
}

int sync_internal(uint16_t code, uint32_t server_address)
{
    clear_interrupt();
    
    if (server_address == 0) {
        SERVER_ADDRESS = inet_addr("192.168.1.10");
    } else {
        SERVER_ADDRESS = htonl(server_address);
    }

    socket_t skt = 0;
    int ret = VANILLA_ERROR;
    if (server_address != 0) {
        ret = connect_to_backend(&skt, VANILLA_PIPE_SYNC_CODE(code));
        if (ret != VANILLA_SUCCESS) {
            goto exit;
        }
    }

    // Wait for sync result from pipe
    uint32_t recv_cc;
    while (1) {
        ssize_t read_size = recv(skt, (void*)&recv_cc, sizeof(recv_cc), 0);
        if (read_size == sizeof(recv_cc)) {
            recv_cc = ntohl(recv_cc);
            if (recv_cc >> 8 == VANILLA_PIPE_CC_SYNC_STATUS >> 8) {
                ret = recv_cc & 0xFF;
                break;
            }
        }

        if (is_interrupted()) {
            send_pipe_cc(skt, VANILLA_PIPE_CC_UNBIND, 0);
            break;
        }
    }

exit_pipe:
    if (skt)
        vanilla_close_socket(skt);

exit:
    return ret;
}

int connect_as_gamepad_internal(event_loop_t *event_loop, uint32_t server_address)
{
    clear_interrupt();

    PORT_MSG = 50110;
    PORT_VID = 50120;
    PORT_AUD = 50121;
    PORT_HID = 50122;
    PORT_CMD = 50123;

    if (server_address == 0) {
        SERVER_ADDRESS = inet_addr("192.168.1.10");
    } else {
        SERVER_ADDRESS = htonl(server_address);
        PORT_MSG += 200;
        PORT_VID += 200;
        PORT_AUD += 200;
        PORT_HID += 200;
        PORT_CMD += 200;
    }

    gamepad_context_t info;
    info.event_loop = event_loop;

    int ret = VANILLA_ERROR;

    socket_t pipe_cc_skt = 0;
    if (server_address != 0) {
        ret = connect_to_backend(&pipe_cc_skt, VANILLA_PIPE_CC_CONNECT);
        if (ret != VANILLA_SUCCESS) {
            goto exit;
        }
    }

    // Open all required sockets
    if (!create_socket(&info.socket_vid, PORT_VID)) goto exit_pipe;
    if (!create_socket(&info.socket_msg, PORT_MSG)) goto exit_vid;
    if (!create_socket(&info.socket_hid, PORT_HID)) goto exit_msg;
    if (!create_socket(&info.socket_aud, PORT_AUD)) goto exit_hid;
    if (!create_socket(&info.socket_cmd, PORT_CMD)) goto exit_aud;

    pthread_t video_thread, audio_thread, input_thread, msg_thread, cmd_thread;

    pthread_create(&video_thread, NULL, listen_video, &info);
    pthread_setname_np(video_thread, "vanilla-video");

    pthread_create(&audio_thread, NULL, listen_audio, &info);
    pthread_setname_np(audio_thread, "vanilla-audio");

    pthread_create(&input_thread, NULL, listen_input, &info);
    pthread_setname_np(input_thread, "vanilla-input");
    
    pthread_create(&cmd_thread, NULL, listen_command, &info);
    pthread_setname_np(cmd_thread, "vanilla-cmd");

    while (1) {
        vanilla_sleep(250);
        if (is_interrupted()) {
            // Wake up any threads that might be blocked on `recv`
            send_stop_code(info.socket_msg, PORT_VID);
            send_stop_code(info.socket_msg, PORT_AUD);
            send_stop_code(info.socket_msg, PORT_CMD);
            break;
        }
    }

    pthread_join(video_thread, NULL);
    pthread_join(audio_thread, NULL);
    pthread_join(input_thread, NULL);
    pthread_join(cmd_thread, NULL);

    if (server_address != 0) {
        send_pipe_cc(pipe_cc_skt, VANILLA_PIPE_CC_UNBIND, 0);
    }

    ret = VANILLA_SUCCESS;

exit_cmd:
    vanilla_close_socket(info.socket_cmd);

exit_aud:
    vanilla_close_socket(info.socket_aud);

exit_hid:
    vanilla_close_socket(info.socket_hid);

exit_msg:
    vanilla_close_socket(info.socket_msg);

exit_vid:
    vanilla_close_socket(info.socket_vid);

exit_pipe:
    if (pipe_cc_skt)
        vanilla_close_socket(pipe_cc_skt);

exit:
    return ret;
}

int is_stop_code(const char *data, size_t data_length)
{
    return (data_length == sizeof(STOP_CODE) && !memcmp(data, &STOP_CODE, sizeof(STOP_CODE)));
}

int push_event(event_loop_t *loop, int type, const void *data, size_t size)
{
    pthread_mutex_lock(&loop->mutex);

    vanilla_event_t *ev = &loop->events[loop->new_index % VANILLA_MAX_EVENT_COUNT];

    if (size <= EVENT_BUFFER_SIZE) {
        assert(!ev->data);

        ev->data = get_event_buffer();
        if (!ev->data) {
            print_info("OUT OF MEMORY FOR NEW EVENTS");
            return VANILLA_ERROR;
        }
        
        ev->type = type;
        memcpy(ev->data, data, size);
        ev->size = size;

        loop->new_index++;

        // Prevent rollover by skipping oldest event if necessary
        if (loop->new_index > loop->used_index + VANILLA_MAX_EVENT_COUNT) {
            vanilla_free_event(&loop->events[loop->used_index % VANILLA_MAX_EVENT_COUNT]);
            print_info("SKIPPED EVENT TO PREVENT ROLLOVER (%lu > %lu + %lu)", loop->new_index, loop->used_index, VANILLA_MAX_EVENT_COUNT);
            loop->used_index++;
        }

        pthread_cond_broadcast(&loop->waitcond);
    } else {
        print_info("FAILED TO PUSH EVENT: wanted %lu, only had %lu. This is a bug, please report to developers.\n", size, EVENT_BUFFER_SIZE);
    }

    pthread_mutex_unlock(&loop->mutex);
}

int get_event(event_loop_t *loop, vanilla_event_t *event, int wait)
{
    int ret = 0;

    pthread_mutex_lock(&loop->mutex);

    if (loop->active) {
        if (wait) {
            while (loop->active && loop->used_index == loop->new_index) {
                pthread_cond_wait(&loop->waitcond, &loop->mutex);
            }
        }

        if (loop->active && loop->used_index < loop->new_index) {
            // Output data to pointer
            vanilla_event_t *pull_event = &loop->events[loop->used_index % VANILLA_MAX_EVENT_COUNT];

            event->type = pull_event->type;
            event->data = pull_event->data;
            event->size = pull_event->size;

            pull_event->data = NULL;

            loop->used_index++;
            ret = 1;
        }
    }

    pthread_mutex_unlock(&loop->mutex);
    
    return ret;
}

void *get_event_buffer()
{
    void *buf = NULL;

    pthread_mutex_lock(&event_buffer_mutex);
    for (size_t i = 0; i < EVENT_BUFFER_ARENA_SIZE; i++) {
        if (EVENT_BUFFER_ARENA[i]) {
            buf = EVENT_BUFFER_ARENA[i];
            EVENT_BUFFER_ARENA[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&event_buffer_mutex);

    return buf;
}

void release_event_buffer(void *buffer)
{
    pthread_mutex_lock(&event_buffer_mutex);
    for (size_t i = 0; i < EVENT_BUFFER_ARENA_SIZE; i++) {
        if (!EVENT_BUFFER_ARENA[i]) {
            EVENT_BUFFER_ARENA[i] = buffer;
            break;
        }
    }
    pthread_mutex_unlock(&event_buffer_mutex);
}

void init_event_buffer_arena()
{
    for (size_t i = 0; i < EVENT_BUFFER_ARENA_SIZE; i++) {
        if (!EVENT_BUFFER_ARENA[i]) {
            EVENT_BUFFER_ARENA[i] = malloc(EVENT_BUFFER_SIZE);
        } else {
            print_info("CRITICAL: Buffer wasn't returned to the arena");
        }
    }
}

void free_event_buffer_arena()
{
    for (size_t i = 0; i < EVENT_BUFFER_ARENA_SIZE; i++) {
        if (EVENT_BUFFER_ARENA[i]) {
            free(EVENT_BUFFER_ARENA[i]);
            EVENT_BUFFER_ARENA[i] = NULL;
        } else {
            print_info("CRITICAL: Buffer wasn't returned to the arena");
        }
    }
}