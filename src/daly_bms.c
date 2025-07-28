/**
 * @file daly_bms.c
 * @brief Daly Smart BMS UART Communication Implementation
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>

#include "daly_bms.h"
#include "logging.h"

/* Static function prototypes */
static uint8_t daly_checksum(const uint8_t *data, size_t len);
static int daly_build_request(uint8_t cmd, uint8_t *frame, const uint8_t *payload);
static int daly_read_exact(int fd, uint8_t *buf, size_t len, int timeout_ms);
static int daly_read_frame(int fd, uint8_t expected_cmd, uint8_t *data, int timeout_ms);
static int daly_request(int fd, uint8_t cmd, uint8_t *response, int timeout_ms, const uint8_t *payload);
static uint16_t daly_get_u16be(const uint8_t *data, int offset);

/* Data parsing functions */
static void daly_parse_0x90(const uint8_t *data, daly_pack_summary_t *pack);
static void daly_parse_0x91(const uint8_t *data, daly_extremes_t *extremes);
static void daly_parse_0x92(const uint8_t *data, daly_temps_t *temps);
static void daly_parse_0x93(const uint8_t *data, daly_mos_caps_t *mos);
static void daly_parse_0x94(const uint8_t *data, daly_status_t *status);
static void daly_parse_0x95_frames(const uint8_t **frames, int frame_count, int cell_count, int *cell_mv);
static void daly_parse_0x96_frames(const uint8_t **frames, int frame_count, int ntc_count, daly_temps_t *temps);
static void daly_parse_0x97(const uint8_t *data, int cell_count, bool *balance);
static void daly_parse_0x98(const uint8_t *data, char faults[][64], int *fault_count);

/* Fault descriptions by byte and bit position */
static const char * const daly_faults[8][8] = {
    /* Byte 0 */
    {
        "Cell volt high L1",
        "Cell volt high L2",
        "Cell volt low L1",
        "Cell volt low L2",
        "Sum volt high L1",
        "Sum volt high L2",
        "Sum volt low L1",
        "Sum volt low L2",
    },
    /* Byte 1 */
    {
        "Chg temp high L1",
        "Chg temp high L2",
        "Chg temp low L1",
        "Chg temp low L2",
        "Dischg temp high L1",
        "Dischg temp high L2",
        "Dischg temp low L1",
        "Dischg temp low L2",
    },
    /* Byte 2 */
    {
        "Chg OC L1",
        "Chg OC L2",
        "Dischg OC L1",
        "Dischg OC L2",
        "SOC high L1",
        "SOC high L2",
        "SOC low L1",
        "SOC low L2",
    },
    /* Byte 3 */
    {
        "Diff volt L1",
        "Diff volt L2",
        "Diff temp L1",
        "Diff temp L2",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved"
    },
    /* Byte 4 */
    {
        "Chg MOS temp high",
        "Dischg MOS temp high",
        "Chg MOS temp sensor err",
        "Dischg MOS temp sensor err",
        "Chg MOS adhesion err",
        "Dischg MOS adhesion err",
        "Chg MOS open circuit",
        "Dischg MOS open circuit",
    },
    /* Byte 5 */
    {
        "AFE collect chip err",
        "Voltage collect dropped",
        "Cell temp sensor err",
        "EEPROM err",
        "RTC err",
        "Precharge failure",
        "Communication failure",
        "Internal comm failure",
    },
    /* Byte 6 */
    {
        "Current module fault",
        "Sum voltage detect fault",
        "Short circuit protect fault",
        "Low volt forbid charge",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
    },
    /* Byte 7 */
    {
        "Fault code bit0",
        "bit1",
        "bit2",
        "bit3",
        "bit4",
        "bit5",
        "bit6",
        "bit7"
    }
};

/**
 * @brief Calculate checksum for Daly frames
 * 
 * The checksum is a simple sum of all bytes, truncated to 8 bits.
 */
static uint8_t daly_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

/**
 * @brief Build a Daly BMS request frame
 * 
 * @param cmd Command byte
 * @param frame Output buffer for the complete frame (must be DALY_FRAME_LEN bytes)
 * @param payload Optional 8-byte payload (null for default zeros)
 * @return int Number of bytes in the frame
 */
static int daly_build_request(uint8_t cmd, uint8_t *frame, const uint8_t *payload) {
    uint8_t default_payload[8] = {0};
    
    if (!payload) {
        payload = default_payload;
    }
    
    frame[0] = DALY_START_BYTE;
    frame[1] = DALY_HOST_ADDR;
    frame[2] = cmd;
    frame[3] = DALY_LEN_FIXED;
    
    /* Copy payload */
    memcpy(frame + 4, payload, 8);
    
    /* Calculate and append checksum */
    frame[12] = daly_checksum(frame, 12);
    
    return DALY_FRAME_LEN;
}

/**
 * @brief Read exactly len bytes with timeout
 * 
 * @param fd File descriptor
 * @param buf Buffer to store data
 * @param len Number of bytes to read
 * @param timeout_ms Timeout in milliseconds
 * @return int Number of bytes read, or -1 on error
 */
static int daly_read_exact(int fd, uint8_t *buf, size_t len, int timeout_ms) {
    size_t total_read = 0;
    struct timeval tv;
    fd_set readfds;
    
    /* Calculate end time */
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    end_time.tv_sec += timeout_ms / 1000;
    end_time.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (end_time.tv_nsec >= 1000000000) {
        end_time.tv_sec++;
        end_time.tv_nsec -= 1000000000;
    }
    
    while (total_read < len) {
        /* Calculate remaining timeout */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        if ((now.tv_sec > end_time.tv_sec) || 
            (now.tv_sec == end_time.tv_sec && now.tv_nsec >= end_time.tv_nsec)) {
            /* Timeout */
            return total_read;
        }
        
        /* Calculate timeout for select */
        tv.tv_sec = end_time.tv_sec - now.tv_sec;
        tv.tv_usec = (end_time.tv_nsec - now.tv_nsec) / 1000;
        if (tv.tv_usec < 0) {
            tv.tv_sec--;
            tv.tv_usec += 1000000;
        }
        
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        int select_result = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (select_result < 0) {
            if (errno == EINTR) {
                continue;  /* Signal interrupted select, try again */
            }
            OLOG_ERROR("select() failed: %s", strerror(errno));
            return -1;
        } else if (select_result == 0) {
            /* Timeout */
            return total_read;
        }
        
        /* Read available data */
        ssize_t n = read(fd, buf + total_read, len - total_read);
        if (n < 0) {
            if (errno == EINTR) {
                continue;  /* Signal interrupted read, try again */
            }
            OLOG_ERROR("read() failed: %s", strerror(errno));
            return -1;
        } else if (n == 0) {
            /* End of file */
            return total_read;
        }
        
        total_read += n;
    }
    
    return total_read;
}

/**
 * @brief Read a Daly BMS frame
 * 
 * @param fd File descriptor
 * @param expected_cmd Expected command byte, or 0 to accept any command
 * @param data Buffer to store frame data (8 bytes)
 * @param timeout_ms Timeout in milliseconds
 * @return int Command byte on success, -1 on error
 */
static int daly_read_frame(int fd, uint8_t expected_cmd, uint8_t *data, int timeout_ms) {
    struct timespec start_time, now;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    int elapsed_ms;
    
    while (1) {
        /* Check for timeout */
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (now.tv_sec - start_time.tv_sec) * 1000 + 
                     (now.tv_nsec - start_time.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) {
            /* Timeout */
            return -1;
        }
        
        /* Try to read start byte */
        uint8_t byte;
        int n = daly_read_exact(fd, &byte, 1, timeout_ms - elapsed_ms);
        if (n != 1) {
            return -1;
        }
        
        if (byte != DALY_START_BYTE) {
            /* Not a start byte, keep hunting */
            continue;
        }
        
        /* Read the rest of the frame */
        uint8_t frame[DALY_FRAME_LEN - 1];
        n = daly_read_exact(fd, frame, DALY_FRAME_LEN - 1, timeout_ms - elapsed_ms);
        if (n != DALY_FRAME_LEN - 1) {
            /* Incomplete frame */
            continue;
        }
        
        /* Check address, command, and length */
        uint8_t addr = frame[0];
        uint8_t cmd = frame[1];
        uint8_t len = frame[2];
        
        if (addr != DALY_BMS_ADDR || len != DALY_LEN_FIXED) {
            /* Invalid frame */
            continue;
        }
        
        if (expected_cmd != 0 && cmd != expected_cmd) {
            /* Unexpected command */
            continue;
        }
        
        /* Verify checksum */
        uint8_t full_frame[DALY_FRAME_LEN];
        full_frame[0] = DALY_START_BYTE;
        memcpy(full_frame + 1, frame, DALY_FRAME_LEN - 1);
        
        uint8_t calc_checksum = daly_checksum(full_frame, DALY_FRAME_LEN - 1);
        if (calc_checksum != frame[DALY_FRAME_LEN - 2]) {
            /* Bad checksum */
            continue;
        }
        
        /* Frame is valid, copy data */
        memcpy(data, frame + 3, 8);
        
        return cmd;
    }
}

/**
 * @brief Send a request and read response
 * 
 * @param fd File descriptor
 * @param cmd Command byte
 * @param response Buffer to store response data (8 bytes)
 * @param timeout_ms Timeout in milliseconds
 * @param payload Optional 8-byte payload (null for default zeros)
 * @return int 0 on success, -1 on error
 */
static int daly_request(int fd, uint8_t cmd, uint8_t *response, int timeout_ms, const uint8_t *payload) {
    uint8_t frame[DALY_FRAME_LEN];
    
    /* Build request frame */
    daly_build_request(cmd, frame, payload);
    
    /* Flush input buffer */
    tcflush(fd, TCIFLUSH);
    
    /* Send request */
    if (write(fd, frame, DALY_FRAME_LEN) != DALY_FRAME_LEN) {
        OLOG_ERROR("Failed to write request frame: %s", strerror(errno));
        return -1;
    }
    
    /* Read response */
    int result = daly_read_frame(fd, cmd, response, timeout_ms);
    if (result < 0) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Get 16-bit big-endian value from byte array
 */
static uint16_t daly_get_u16be(const uint8_t *data, int offset) {
    return (uint16_t)((data[offset] << 8) | data[offset + 1]);
}

/**
 * @brief Parse basic pack info from 0x90 command response
 */
static void daly_parse_0x90(const uint8_t *data, daly_pack_summary_t *pack) {
    /* Daly places instantaneous pack voltage in bytes 0..1 (deci-volts). Some variants mirror in 2..3. */
    float v0 = daly_get_u16be(data, 0) / 10.0f;
    float v2 = daly_get_u16be(data, 2) / 10.0f;
    
    pack->v_total_v = v0 > 0 ? v0 : v2;
    pack->v_total_cumulative_v = v2;
    pack->current_a = (daly_get_u16be(data, 4) - 30000) / 10.0f;  /* Offset encoding for bidirectional */
    pack->soc_pct = daly_get_u16be(data, 6) / 10.0f;
}

/**
 * @brief Parse cell voltage extremes from 0x91 command response
 */
static void daly_parse_0x91(const uint8_t *data, daly_extremes_t *extremes) {
    extremes->vmax_v = daly_get_u16be(data, 0) / 1000.0f;
    extremes->vmax_cell = data[2];
    extremes->vmin_v = daly_get_u16be(data, 3) / 1000.0f;
    extremes->vmin_cell = data[5];
}

/**
 * @brief Parse temperature extremes from 0x92 command response
 */
static void daly_parse_0x92(const uint8_t *data, daly_temps_t *temps) {
    temps->tmax_c = data[0] - 40;
    temps->tmax_idx = data[1];
    temps->tmin_c = data[2] - 40;
    temps->tmin_idx = data[3];
}

/**
 * @brief Parse MOS status from 0x93 command response
 */
static void daly_parse_0x93(const uint8_t *data, daly_mos_caps_t *mos) {
    mos->state = data[0];
    mos->charge_mos = data[1] != 0;
    mos->discharge_mos = data[2] != 0;
    mos->life_cycles = data[3];
    mos->remain_capacity_mah = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
}

/**
 * @brief Parse status from 0x94 command response
 */
static void daly_parse_0x94(const uint8_t *data, daly_status_t *status) {
    status->cell_count = data[0];
    status->ntc_count = data[1];
    status->charger_present = data[2] != 0;
    status->load_present = data[3] != 0;
    status->dio_bits = data[4];
}

/**
 * @brief Parse cell voltages from multiple 0x95 frames
 */
static void daly_parse_0x95_frames(const uint8_t **frames, int frame_count, int cell_count, int *cell_mv) {
    /* Initialize cell voltages to zero */
    memset(cell_mv, 0, cell_count * sizeof(int));
    
    for (int i = 0; i < frame_count; i++) {
        const uint8_t *frame = frames[i];
        if (!frame) continue;
        
        /* Each frame contains voltage for 3 cells */
        uint8_t frame_no = frame[0];
        if (frame_no == 0 || frame_no == 0xFF) continue;
        
        int base_idx = (frame_no - 1) * 3;  /* 1-based frame index */
        
        /* Extract the three voltage values */
        uint16_t mv1 = daly_get_u16be(frame, 1);
        uint16_t mv2 = daly_get_u16be(frame, 3);
        uint16_t mv3 = daly_get_u16be(frame, 5);
        
        /* Store values if within range */
        if (base_idx < cell_count) cell_mv[base_idx] = mv1;
        if (base_idx + 1 < cell_count) cell_mv[base_idx + 1] = mv2;
        if (base_idx + 2 < cell_count) cell_mv[base_idx + 2] = mv3;
    }
}

/**
 * @brief Parse temperature sensors from multiple 0x96 frames
 */
static void daly_parse_0x96_frames(const uint8_t **frames, int frame_count, int ntc_count, daly_temps_t *temps) {
    /* Initialize temperatures to zero */
    memset(temps->sensors_c, 0, sizeof(temps->sensors_c));
    
    for (int i = 0; i < frame_count; i++) {
        const uint8_t *frame = frames[i];
        if (!frame) continue;
        
        /* Each frame contains 7 temperature values */
        uint8_t frame_no = frame[0];
        if (frame_no == 0) continue;
        
        int base_idx = (frame_no - 1) * 7;  /* 1-based frame index */
        
        /* Extract temperature values (offset by +40) */
        for (int j = 1; j < 8; j++) {
            int idx = base_idx + (j - 1);
            if (idx < ntc_count && idx < DALY_MAX_TEMPS) {
                temps->sensors_c[idx] = frame[j] - 40;
            }
        }
    }
}

/**
 * @brief Parse balance status from 0x97 command response
 */
static void daly_parse_0x97(const uint8_t *data, int cell_count, bool *balance) {
    /* Combine all bytes into a bit field */
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
        bits |= (uint64_t)data[i] << (8 * i);
    }
    
    /* Extract balance status for each cell */
    for (int i = 0; i < cell_count && i < DALY_MAX_CELLS; i++) {
        balance[i] = (bits >> i) & 1;
    }
}

/**
 * @brief Parse fault flags from 0x98 command response
 */
static void daly_parse_0x98(const uint8_t *data, char faults[][64], int *fault_count) {
    *fault_count = 0;
    
    for (int byte_idx = 0; byte_idx < 8; byte_idx++) {
        uint8_t mask = data[byte_idx];
        if (!mask) continue;
        
        for (int bit = 0; bit < 8; bit++) {
            if (mask & (1 << bit)) {
                if (*fault_count < DALY_MAX_FAULTS) {
                    /* Copy fault description */
                    strncpy(faults[*fault_count], daly_faults[byte_idx][bit], 63);
                    faults[*fault_count][63] = '\0';
                    (*fault_count)++;
                }
            }
        }
    }
}

/**
 * @brief Initialize the Daly BMS device
 */
int daly_bms_init(daly_device_t *dev, const char *port, int baud, int timeout_ms) {
    struct termios tty;
    speed_t baud_const;
    
    if (!dev || !port) {
        return -1;
    }
    
    /* Clear device structure */
    memset(dev, 0, sizeof(daly_device_t));
    
    /* Store configuration */
    strncpy(dev->port, port, sizeof(dev->port) - 1);
    dev->baud = baud;
    dev->timeout_ms = timeout_ms;
    
    /* Open serial port */
    dev->fd = open(port, O_RDWR | O_NOCTTY);
    if (dev->fd < 0) {
        OLOG_ERROR("Failed to open serial port %s: %s", port, strerror(errno));
        return -1;
    }
    
    /* Get current port settings */
    if (tcgetattr(dev->fd, &tty) != 0) {
        OLOG_ERROR("Failed to get port attributes: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }
    
    /* Clear parity bit, disabling parity (most common) */
    tty.c_cflag &= ~PARENB;
    /* Set one stop bit */
    tty.c_cflag &= ~CSTOPB;
    /* 8 bits per byte */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    /* Disable RTS/CTS hardware flow control */
    tty.c_cflag &= ~CRTSCTS;
    /* Turn on READ & ignore ctrl lines */
    tty.c_cflag |= CREAD | CLOCAL;
    
    /* Disable canonical mode */
    tty.c_lflag &= ~ICANON;
    /* Disable echo */
    tty.c_lflag &= ~ECHO;
    /* Disable erasure */
    tty.c_lflag &= ~ECHOE;
    /* Disable new-line echo */
    tty.c_lflag &= ~ECHONL;
    /* Disable interpretation of INTR, QUIT and SUSP */
    tty.c_lflag &= ~ISIG;
    
    /* Disable software flow control */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    /* Disable special handling of received bytes */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    /* Disable special handling of output bytes */
    tty.c_oflag &= ~OPOST;
    /* Disable newline expansion */
    tty.c_oflag &= ~ONLCR;
    
    /* Configure blocking behavior */
    tty.c_cc[VTIME] = 1;  /* 100ms timeout (1 decisecond) */
    tty.c_cc[VMIN] = 0;   /* No blocking, return immediately with what is available */
    
    /* Set baud rate */
    switch (baud) {
        case 9600:   baud_const = B9600;   break;
        case 19200:  baud_const = B19200;  break;
        case 38400:  baud_const = B38400;  break;
        case 57600:  baud_const = B57600;  break;
        case 115200: baud_const = B115200; break;
        default:
            OLOG_ERROR("Unsupported baud rate: %d", baud);
            close(dev->fd);
            dev->fd = -1;
            return -1;
    }
    
    cfsetispeed(&tty, baud_const);
    cfsetospeed(&tty, baud_const);
    
    /* Apply settings */
    if (tcsetattr(dev->fd, TCSANOW, &tty) != 0) {
        OLOG_ERROR("Failed to set port attributes: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }
    
    /* Flush any existing data */
    tcflush(dev->fd, TCIOFLUSH);
    
    dev->initialized = true;
    OLOG_INFO("Daly BMS initialized on %s at %d baud", port, baud);
    
    return 0;
}

/**
 * @brief Close the Daly BMS device
 */
void daly_bms_close(daly_device_t *dev) {
    if (dev && dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
        dev->initialized = false;
        OLOG_INFO("Daly BMS connection closed");
    }
}

/**
 * @brief Poll all data from the Daly BMS
 */
int daly_bms_poll(daly_device_t *dev) {
    if (!dev || !dev->initialized) {
        return -1;
    }
    
    daly_data_t *data = &dev->data;
    uint8_t response[8];
    int result;
    
    /* Clear previous error */
    data->last_err[0] = '\0';
    
    /* Request basic pack info (0x90) */
    result = daly_request(dev->fd, DALY_CMD_PACK_INFO, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x90(response, &data->pack);
    } else {
        snprintf(data->last_err, sizeof(data->last_err), "Failed to read pack info (0x90)");
        return -1;
    }
    
    /* Request cell voltage extremes (0x91) */
    result = daly_request(dev->fd, DALY_CMD_CELL_VOLTAGE, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x91(response, &data->extremes);
    } else {
        snprintf(data->last_err, sizeof(data->last_err), "Failed to read cell voltage extremes (0x91)");
        return -1;
    }
    
    /* Request temperature extremes (0x92) */
    result = daly_request(dev->fd, DALY_CMD_TEMPERATURE, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x92(response, &data->temps);
    } else {
        snprintf(data->last_err, sizeof(data->last_err), "Failed to read temperature extremes (0x92)");
        return -1;
    }
    
    /* Request MOS status (0x93) */
    result = daly_request(dev->fd, DALY_CMD_MOS_STATUS, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x93(response, &data->mos);
    } else {
        snprintf(data->last_err, sizeof(data->last_err), "Failed to read MOS status (0x93)");
        return -1;
    }
    
    /* Request system status (0x94) */
    result = daly_request(dev->fd, DALY_CMD_STATUS, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x94(response, &data->status);
        data->temps.ntc_count = data->status.ntc_count;
    } else {
        snprintf(data->last_err, sizeof(data->last_err), "Failed to read system status (0x94)");
        return -1;
    }
    
    /* Request cell voltages (0x95) - Multiple frames */
    int cell_count = data->status.cell_count;
    if (cell_count > 0) {
        int frames_needed = (cell_count + 2) / 3;  /* Ceiling division */
        const uint8_t *frames[16] = {0};
        int frame_count = 0;
        
        for (int i = 0; i < 32 && frame_count < frames_needed; i++) {
            result = daly_request(dev->fd, DALY_CMD_CELL_VOLTAGES, response, dev->timeout_ms, NULL);
            if (result == 0) {
                /* Check frame number */
                uint8_t frame_no = response[0];
                if (frame_no != 0 && frame_no != 0xFF && frame_no <= frames_needed) {
                    /* Allocate memory for this frame */
                    uint8_t *frame_data = malloc(8);
                    if (frame_data) {
                        memcpy(frame_data, response, 8);
                        frames[frame_count++] = frame_data;
                    }
                }
            }
        }
        
        /* Parse cell voltages */
        if (frame_count > 0) {
            daly_parse_0x95_frames(frames, frame_count, cell_count, data->cell_mv);
        }
        
        /* Free frame data */
        for (int i = 0; i < frame_count; i++) {
            free((void *)frames[i]);
        }
    }
    
    /* Request temperature sensors (0x96) - Multiple frames */
    int ntc_count = data->status.ntc_count;
    if (ntc_count > 0) {
        int frames_needed = (ntc_count + 6) / 7;  /* Ceiling division */
        const uint8_t *frames[8] = {0};
        int frame_count = 0;
        
        for (int i = 0; i < 16 && frame_count < frames_needed; i++) {
            result = daly_request(dev->fd, DALY_CMD_TEMPERATURES, response, dev->timeout_ms, NULL);
            if (result == 0) {
                /* Check frame number */
                uint8_t frame_no = response[0];
                if (frame_no != 0 && frame_no <= frames_needed) {
                    /* Allocate memory for this frame */
                    uint8_t *frame_data = malloc(8);
                    if (frame_data) {
                        memcpy(frame_data, response, 8);
                        frames[frame_count++] = frame_data;
                    }
                }
            }
        }
        
        /* Parse temperature sensors */
        if (frame_count > 0) {
            daly_parse_0x96_frames(frames, frame_count, ntc_count, &data->temps);
        }
        
        /* Free frame data */
        for (int i = 0; i < frame_count; i++) {
            free((void *)frames[i]);
        }
    }
    
    /* Request balance status (0x97) */
    result = daly_request(dev->fd, DALY_CMD_BALANCE_STATUS, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x97(response, cell_count, data->balance);
    }
    
    /* Request fault flags (0x98) */
    result = daly_request(dev->fd, DALY_CMD_FAULTS, response, dev->timeout_ms, NULL);
    if (result == 0) {
        daly_parse_0x98(response, data->faults, &data->fault_count);
    }
    
    /* Mark data as valid and update timestamp */
    data->last_ok = time(NULL);
    data->valid = true;
    
    return 0;
}

/**
 * @brief Read rated capacity from the Daly BMS
 */
int daly_bms_read_capacity(daly_device_t *dev, daly_capacity_t *capacity) {
    if (!dev || !dev->initialized || !capacity) {
        return -1;
    }
    
    uint8_t response[8];
    
    int result = daly_request(dev->fd, DALY_CMD_READ_CAPACITY, response, dev->timeout_ms, NULL);
    if (result != 0) {
        OLOG_ERROR("Failed to read rated capacity");
        return -1;
    }
    
    /* Parse response: data[0..3] = rated mAh (BE), data[6..7] = nominal cell mV (BE) */
    capacity->rated_capacity_mah = (response[0] << 24) | (response[1] << 16) | (response[2] << 8) | response[3];
    capacity->nominal_cell_mv = (response[6] << 8) | response[7];
    
    return 0;
}

/**
 * @brief Write rated capacity to the Daly BMS
 */
int daly_bms_write_capacity(daly_device_t *dev, int capacity_mah, int nominal_cell_mv) {
    if (!dev || !dev->initialized) {
        return -1;
    }
    
    uint8_t payload[8];
    uint8_t response[8];
    
    /* Prepare payload */
    payload[0] = (capacity_mah >> 24) & 0xFF;
    payload[1] = (capacity_mah >> 16) & 0xFF;
    payload[2] = (capacity_mah >> 8) & 0xFF;
    payload[3] = capacity_mah & 0xFF;
    payload[4] = 0;
    payload[5] = 0;
    payload[6] = (nominal_cell_mv >> 8) & 0xFF;
    payload[7] = nominal_cell_mv & 0xFF;
    
    int result = daly_request(dev->fd, DALY_CMD_WRITE_CAPACITY, response, 600, payload);
    if (result != 0) {
        OLOG_ERROR("Failed to write rated capacity");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Write SOC to the Daly BMS
 */
int daly_bms_write_soc(daly_device_t *dev, float soc_percent) {
    if (!dev || !dev->initialized) {
        return -1;
    }
    
    uint8_t payload[8];
    uint8_t response[8];
    time_t now;
    struct tm *tm_info;
    
    /* Clamp SOC to valid range */
    if (soc_percent < 0.0f) soc_percent = 0.0f;
    if (soc_percent > 100.0f) soc_percent = 100.0f;
    
    /* Convert SOC to tenths of a percent */
    int soc_tenths = (int)roundf(soc_percent * 10.0f);
    
    /* Get current time */
    time(&now);
    tm_info = localtime(&now);
    
    /* Prepare payload: [YY MM DD HH mm SS] [SOC_hi SOC_lo] */
    payload[0] = tm_info->tm_year % 100;
    payload[1] = tm_info->tm_mon + 1;
    payload[2] = tm_info->tm_mday;
    payload[3] = tm_info->tm_hour;
    payload[4] = tm_info->tm_min;
    payload[5] = tm_info->tm_sec;
    payload[6] = (soc_tenths >> 8) & 0xFF;
    payload[7] = soc_tenths & 0xFF;
    
    int result = daly_request(dev->fd, DALY_CMD_WRITE_SOC, response, 600, payload);
    if (result != 0) {
        OLOG_ERROR("Failed to write SOC");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Get the inferred state of the BMS
 */
int daly_bms_infer_state(float current_a, bool chg_mos, bool dsg_mos, float threshold) {
    if (current_a > threshold && chg_mos) {
        return DALY_STATE_CHARGE;
    } else if (current_a < -threshold && dsg_mos) {
        return DALY_STATE_DISCHARGE;
    } else {
        return DALY_STATE_IDLE;
    }
}

/**
 * @brief Check if charger is present based on current
 */
bool daly_bms_infer_charger(float current_a, bool chg_mos, float threshold) {
    return (current_a > threshold && chg_mos);
}

/**
 * @brief Check if load is present based on current
 */
bool daly_bms_infer_load(float current_a, bool dsg_mos, float threshold) {
    return (current_a < -threshold && dsg_mos);
}

/**
 * @brief Print BMS data in human-readable format
 */
void daly_bms_print_data(const daly_device_t *dev) {
    if (!dev || !dev->initialized || !dev->data.valid) {
        printf("Daly BMS: No valid data\n");
        return;
    }
    
    const daly_data_t *data = &dev->data;
    
    printf("Daly BMS Status:\n");
    printf("  Last update: %s\n", ctime(&data->last_ok));
    
    /* Pack information */
    printf("Pack:\n");
    printf("  Voltage: %.2f V\n", data->pack.v_total_v);
    printf("  Current: %.2f A\n", data->pack.current_a);
    printf("  Power: %.2f W\n", data->pack.v_total_v * data->pack.current_a);
    printf("  SOC: %.1f%%\n", data->pack.soc_pct);
    
    /* MOS status */
    printf("FETs:\n");
    printf("  Charge: %s\n", data->mos.charge_mos ? "Enabled" : "Disabled");
    printf("  Discharge: %s\n", data->mos.discharge_mos ? "Enabled" : "Disabled");
    
    /* Derived state */
    int state = daly_bms_infer_state(data->pack.current_a, data->mos.charge_mos, 
                                     data->mos.discharge_mos, DALY_CURRENT_DEADBAND);
    printf("  State: %s\n", state == DALY_STATE_CHARGE ? "Charging" : 
                            state == DALY_STATE_DISCHARGE ? "Discharging" : "Idle");
    
    /* Cell information */
    printf("Cells (%d):\n", data->status.cell_count);
    printf("  Vmax: %.3f V (Cell %d)\n", data->extremes.vmax_v, data->extremes.vmax_cell);
    printf("  Vmin: %.3f V (Cell %d)\n", data->extremes.vmin_v, data->extremes.vmin_cell);
    printf("  Delta: %.3f V\n", data->extremes.vmax_v - data->extremes.vmin_v);
    
    /* Individual cell voltages */
    for (int i = 0; i < data->status.cell_count && i < DALY_MAX_CELLS; i++) {
        printf("  Cell %2d: %.3f V%s\n", i + 1, data->cell_mv[i] / 1000.0f, 
               data->balance[i] ? " (Balancing)" : "");
    }
    
    /* Temperature information */
    printf("Temperature:\n");
    printf("  Tmax: %.1f °C (Sensor %d)\n", data->temps.tmax_c, data->temps.tmax_idx);
    printf("  Tmin: %.1f °C (Sensor %d)\n", data->temps.tmin_c, data->temps.tmin_idx);
    
    /* Individual temperature sensors */
    for (int i = 0; i < data->temps.ntc_count && i < DALY_MAX_TEMPS; i++) {
        printf("  Sensor %d: %.1f °C\n", i + 1, data->temps.sensors_c[i]);
    }
    
    /* Faults */
    printf("Faults (%d):\n", data->fault_count);
    if (data->fault_count == 0) {
        printf("  None\n");
    } else {
        for (int i = 0; i < data->fault_count; i++) {
            printf("  %s\n", data->faults[i]);
        }
    }
}

