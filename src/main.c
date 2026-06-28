#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

/*File IO Settings*/
#define READ_FILE_BUF_SIZE   4096      // Maximum Reading Buffer Size From File (4096 is optimal, cause usually sectors on Hard Drive/SSD have 4k size)
#define DWORD_BITS_NUM       32
#define BYTE_BITS_NUM        8

/*Byte Parsing Settings*/
#define BYTE_CHANNEL_MASK   0x03       // Mask To get 0 and 1 channels from byte stream


/*Bit Stream Settings*/
#define SYNC_MARKER_VAL    0x1ACFFC1D
#define LOGIC_0_VAL        0            // Logic 0 val
#define LOGIC_1_VAL        1            // Logic 1 val

/*BITSTREAM */
typedef enum info_bitstream_type_t
{
    INFO_BITSTREAM_ERR     = 0, // bit0 - 0, bit1 - 0 (0x00). Unknown Situation
    INFO_BITSTREAM_LOGIC_0 = 1, // bit0 - 1, bit1 - 0 (0x01). Logic 0
    INFO_BITSTREAM_LOGIC_1 = 2, // bit0 - 0, bit1 - 1 (0x10). Logic 1
    INFO_BITSTREAM_SYNC    = 3, // bit0 - 1, bit1 - 1 (0x11). Sync Pulses
    INFO_BITSTREAM_INIT    = 4
} info_bitstream_type_t;

typedef enum ret_t
{
    RET_OK     = 0, // Ok Return
    RET_FAIL   = 1, // Fail Return
} ret_t;

typedef enum fsm_state_t
{
    FSM_STATE_FIRST_SYNC_SEARCH,
    FSM_STATE_NEXT_SYNC_SEARCH
} fsm_state_t;

static fsm_state_t fsm_state = FSM_STATE_FIRST_SYNC_SEARCH;

/*
This function implements bit stream parsing process

Input:
    const uint8_t* data_in: Input data pointer
    const size_t len_data_in: size of input data
    int fd_write: File descriptor for writing

Output: Return Status of parsing. RET_OK - if parsing was successful, RET_FAIL - otherwise
*/
static ret_t bit_stream_process(const uint8_t* data_in, const size_t len_data_in, int fd_write)
{
    static uint32_t decode_dword_val = 0;      // Use double word to simplify store data(decoded bits) and finding the sync marker
    static size_t decoded_bit_read_cnt = 0;    // Decoded Bit Cnt
    static size_t sync_marker_cnt = 0;         // Sync Market Cnt (Number of sync markers found)
    static size_t frame_byte_written = 0;      // Number of bytes written/found at current frame
    static info_bitstream_type_t last_bitstream_type = INFO_BITSTREAM_INIT;
    ret_t ret = RET_FAIL;

    for (size_t i = 0; i < len_data_in; i++)
    {
        const info_bitstream_type_t cur_bitstream_type = data_in[i] & BYTE_CHANNEL_MASK;   // Получаем значения 0-го и 1-го каналов
        if (last_bitstream_type != cur_bitstream_type)
        {
            switch (cur_bitstream_type)
            {
                case INFO_BITSTREAM_LOGIC_0:
                case INFO_BITSTREAM_LOGIC_1:
                    if (last_bitstream_type == INFO_BITSTREAM_SYNC)        // Checking if before payload, was sync pulses
                    {
                        uint8_t decoded_bit_val = (cur_bitstream_type == INFO_BITSTREAM_LOGIC_0) ? LOGIC_0_VAL : LOGIC_1_VAL;   // Decode BitStream
                        decode_dword_val = (decode_dword_val << 1) | decoded_bit_val;   // Get Current Frame
                        if (fsm_state == FSM_STATE_FIRST_SYNC_SEARCH)       // During First Sync Search, Just Find the Sync Marker w/o writing
                        {
                            if (SYNC_MARKER_VAL == decode_dword_val)
                            {
                                printf("%s", "Found First sync marker\n");
                                int len_bytes_written = write(fd_write, &decode_dword_val, sizeof(decode_dword_val));
                                if (len_bytes_written == sizeof(decode_dword_val))
                                {
                                    frame_byte_written = len_bytes_written;
                                    decode_dword_val = 0;
                                    decoded_bit_read_cnt = 0;
                                    fsm_state = FSM_STATE_NEXT_SYNC_SEARCH;
                                    ret = RET_OK;
                                }
                            }
                        }
                        else if (fsm_state == FSM_STATE_NEXT_SYNC_SEARCH)
                        {
                            if (SYNC_MARKER_VAL == decode_dword_val)
                            {
                                printf("Frame Done Num[%ld] Size[%ld]\n", sync_marker_cnt, frame_byte_written);
                                int len_bytes_written = write(fd_write, &decode_dword_val, sizeof(decode_dword_val));
                                if (len_bytes_written == sizeof(decode_dword_val))
                                {
                                    frame_byte_written = len_bytes_written;
                                    decoded_bit_read_cnt = 0;
                                    sync_marker_cnt++;
                                    decode_dword_val = 0;
                                    ret = RET_OK;
                                }
                            }
                            else if (decoded_bit_read_cnt / (DWORD_BITS_NUM - 1) > 0)            // If we Read More than 1 Dword (32 Bits), write it in file
                            {
                                int len_bytes_written = write(fd_write, &decode_dword_val, sizeof(decode_dword_val));
                                if (len_bytes_written == sizeof(decode_dword_val))
                                {
                                    frame_byte_written += len_bytes_written;
                                    decoded_bit_read_cnt = 0;
                                    decode_dword_val = 0;
                                    ret = RET_OK;
                                }
                            }
                            else
                            {
                                ret = RET_OK;
                                decoded_bit_read_cnt++;
                            }
                        }
                        last_bitstream_type = cur_bitstream_type;
                    }
                    break;
                case INFO_BITSTREAM_SYNC:
                    if (last_bitstream_type != cur_bitstream_type)
                    {
                        last_bitstream_type = cur_bitstream_type;
                    }
                    ret = RET_OK;
                    break;
                default:
                    break;
            }
        }
    }
    if (len_data_in < READ_FILE_BUF_SIZE && decoded_bit_read_cnt > 0)  // If we still have some data in buffer and input data has ended, so just write it in file
    {
        int len_bytes_to_write = (decoded_bit_read_cnt / BYTE_BITS_NUM) + 1;
        int len_bytes_written = write(fd_write, &decode_dword_val, len_bytes_to_write);
        if (len_bytes_written == len_bytes_to_write)
        {
            frame_byte_written += len_bytes_written;
            printf("Frame Done Num[%ld] Size[%ld]\n", sync_marker_cnt, frame_byte_written);
            ret = RET_OK;
        }
    }
    return ret;
}


int main()
{
    uint8_t byte[READ_FILE_BUF_SIZE] = {0};

    int fd_read = open("udk_dump.bin", O_RDONLY);
    int fd_write = open("out.bin", (O_CREAT | O_WRONLY), (S_IWUSR | S_IRUSR));

    if (fd_read == -1)
    {
        printf("%s", "Error Open Read File\n");
        return -1;
    }
    if (fd_write == -1)
    {
        printf("%s", "Error Open Write File\n");
        close(fd_read);
        return -1;
    }

    int read_bytes = read(fd_read, byte, READ_FILE_BUF_SIZE);
    ret_t res = RET_OK;
    while (read_bytes != 0 && res == RET_OK)
    {
        res = bit_stream_process(byte, read_bytes, fd_write);
        read_bytes = read(fd_read, byte, READ_FILE_BUF_SIZE);
    }

    close(fd_read);
    close(fd_write);
}