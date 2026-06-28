#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define READ_BUF_SIZE 4096
#define BIT_STREAM_MASK 0x03
#define MAX_FRAME_SIZE 2048
#define LOGIC_0_VAL 0
#define LOGIC_1_VAL 1
const uint32_t sync_marker = 0x1ACFFC1D;

//const uint8_t sync_marker[4] = {0x1A, 0xCF, 0xFC, 0x1D};

size_t cur_packet_size = 0;
uint8_t byte[240000001] ={0};

typedef enum info_bitstream_type_t
{
    INFO_BITSTREAM_ERR     = 0, // bit0 - 0, bit1 - 0 (0x00). Unknown Situation
    INFO_BITSTREAM_LOGIC_0 = 1, // bit0 - 1, bit1 - 0 (0x01). Logic 0
    INFO_BITSTREAM_LOGIC_1 = 2, // bit0 - 0, bit1 - 1 (0x10). Logic 1
    INFO_BITSTREAM_SYNC    = 3  // bit0 - 1, bit1 - 1 (0x11). Sync Pulses
} info_bitstream_type_t;

typedef enum fsm_state_t
{
    FSM_STATE_FIRST_SYNC_SEARCH,
    FSM_STATE_NEXT_SYNC_SEARCH
} fsm_state_t;

static fsm_state_t fsm_state = FSM_STATE_FIRST_SYNC_SEARCH;

static int bit_stream_process(const uint8_t* byte_stream, const size_t byte_stream_size, int fd_write)
{
    static uint32_t cur_frame_val = 0;
    static uint8_t last_bit_val = 255;
    static uint8_t frame_ptr = 0;
    static size_t cnt = 0;
    static size_t last_pos = 0;
    static size_t frame_cnt = 0;

    for (size_t i = 0; i < byte_stream_size; i++)
    {
        const uint8_t cur_bit_val = byte_stream[i] & BIT_STREAM_MASK;
        if (last_bit_val != cur_bit_val)
        {
            switch (cur_bit_val)
            {
                case INFO_BITSTREAM_ERR:
                    fsm_state = FSM_STATE_FIRST_SYNC_SEARCH;
                    break;
                case INFO_BITSTREAM_LOGIC_0:
                case INFO_BITSTREAM_LOGIC_1:
                    if (last_bit_val == INFO_BITSTREAM_SYNC)
                    {
                        uint8_t sample_val = (cur_bit_val == INFO_BITSTREAM_LOGIC_0) ? LOGIC_0_VAL : LOGIC_1_VAL;
                        cur_frame_val = (cur_frame_val << 1) | sample_val;
                        if (fsm_state == FSM_STATE_FIRST_SYNC_SEARCH)
                        {
                            if (cur_frame_val == sync_marker)
                            {
                                printf("Found First sync marker %ld. Start Frame %d\n", cnt - last_pos, frame_cnt);
                                last_pos = cnt;
                                int res_write = write(fd_write, &cur_frame_val, sizeof(cur_frame_val));
                                frame_ptr = 0;
                                fsm_state = FSM_STATE_NEXT_SYNC_SEARCH;
                                frame_cnt++;
                            }
                        }
                        else if (fsm_state == FSM_STATE_NEXT_SYNC_SEARCH)
                        {
                            if (cur_frame_val == sync_marker)
                            {
                                printf("Found Next sync marker. Frame Num[%ld] Size [%ld]\n", frame_cnt, cnt - last_pos);
                                last_pos = cnt;
                                fsm_state = FSM_STATE_NEXT_SYNC_SEARCH;
                                int res_write = write(fd_write, &cur_frame_val, sizeof(cur_frame_val));
                                frame_ptr = 0;
                                frame_cnt++;
                            }
                            else if (frame_ptr / 32 > 0)            // Чтобы было удобно писать (по 4 байта)
                            {
                                int res_write = write(fd_write, &cur_frame_val, sizeof(cur_frame_val));
                                frame_ptr = 0;
                            }
                            else frame_ptr++;
                        }
                        last_bit_val = cur_bit_val;
                    }
                    break;
                case INFO_BITSTREAM_SYNC:
                    if (last_bit_val != cur_bit_val)
                    {
                        last_bit_val = cur_bit_val;
                    }
                    break;
                default:
                    break;
            }
        }
        cnt++;
    }
}


int main()
{
    uint8_t byte[READ_BUF_SIZE] = {0};

    int fd_read = open("udk_dump.bin", O_RDONLY);
    int fd_write = open("out.bin", O_CREAT | O_WRONLY, 0644);

    if (fd_read == -1)
    {
        printf("Error Open Read File\n");
        return -1;
    }
    if (fd_write == -1)
    {
        printf("Error Open Write File\n");
        return -1;
    }

    int read_bytes = read(fd_read, byte, READ_BUF_SIZE);
    while (read_bytes != 0)
    {
        bit_stream_process(byte, read_bytes, fd_write);
        read_bytes = read(fd_read, byte, READ_BUF_SIZE);
    }

    close(fd_read);
    close(fd_write);
}