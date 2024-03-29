/*
 * Copyright © 2001-2011 Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 *
 * This library implements the Modbus protocol.
 * http://libmodbus.org/
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include <config.h>

#include "modbus.h"
#include "modbus-private.h"

#if ENABLE_MODBUS

/* Internal use */
#define MSG_LENGTH_UNDEFINED -1

/* Exported version */
const unsigned int libmodbus_version_major = LIBMODBUS_VERSION_MAJOR;
const unsigned int libmodbus_version_minor = LIBMODBUS_VERSION_MINOR;
const unsigned int libmodbus_version_micro = LIBMODBUS_VERSION_MICRO;

/* Max between RTU and TCP max adu length (so TCP) */
#define MAX_MESSAGE_LENGTH 260

const char *modbus_strerror(int errnum) {
    switch (errnum) {
    case EMBXILFUN:
        return "Illegal function";
    case EMBXILADD:
        return "Illegal data address";
    case EMBXILVAL:
        return "Illegal data value";
    case EMBXSFAIL:
        return "Slave device or server failure";
    case EMBXACK:
        return "Acknowledge";
    case EMBXSBUSY:
        return "Slave device or server is busy";
    case EMBXNACK:
        return "Negative acknowledge";
    case EMBXMEMPAR:
        return "Memory parity error";
    case EMBXGPATH:
        return "Gateway path unavailable";
    case EMBXGTAR:
        return "Target device failed to respond";
    case EMBBADCRC:
        return "Invalid CRC";
    case EMBBADDATA:
        return "Invalid data";
    case EMBBADEXC:
        return "Invalid exception code";
    case EMBMDATA:
        return "Too many data";
    case EMBBADSLAVE:
        return "Response not from requested slave";
    default:
        return strerror(errnum);
    }
}

void _error_print(modbus_t *ctx, const char *context)
{
    if (ctx->debug) {
        fprintf(stderr, "ERROR %s", modbus_strerror(errno));
        if (context != NULL) {
            fprintf(stderr, ": %s\n", context);
        } else {
            fprintf(stderr, "\n");
        }
    }
}

static void _sleep_response_timeout(modbus_t *ctx)
{
    /* Response timeout is always positive */
#ifdef _WIN32
    /* usleep doesn't exist on Windows */
    Sleep((ctx->response_timeout.tv_sec * 1000) +
          (ctx->response_timeout.tv_usec / 1000));
#else
    /* usleep source code */
    struct timespec request, remaining;
    request.tv_sec = ctx->response_timeout.tv_sec;
    request.tv_nsec = ((long int)ctx->response_timeout.tv_usec) * 1000;
    while (nanosleep(&request, &remaining) == -1 && errno == EINTR) {
        request = remaining;
    }
#endif
}

int modbus_flush(modbus_t *ctx)
{
    int rc;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    rc = ctx->backend->flush(ctx);
    if (rc != -1 && ctx->debug) {
        /* Not all backends are able to return the number of bytes flushed */
        printf("Bytes flushed (%d)\n", rc);
    }
    return rc;
}

/* Computes the length of the expected response */
static unsigned int compute_response_length_from_request(modbus_t *ctx, uint8_t *req)
{
    int length;
    const int offset = ctx->backend->header_length;

    switch (req[offset]) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS: {
        /* Header + nb values (code from write_bits) */
        int nb = (req[offset + 3] << 8) | req[offset + 4];
        length = 2 + (nb / 8) + ((nb % 8) ? 1 : 0);
    }
        break;
    case MODBUS_FC_WRITE_AND_READ_REGISTERS:
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS:
        /* Header + 2 * nb values */
        length = 2 + 2 * (req[offset + 3] << 8 | req[offset + 4]);
        break;
    case MODBUS_FC_READ_EXCEPTION_STATUS:
        length = 3;
        break;
    case MODBUS_FC_REPORT_SLAVE_ID:
        /* The response is device specific (the header provides the
           length) */
        return MSG_LENGTH_UNDEFINED;
    case MODBUS_FC_MASK_WRITE_REGISTER:
        length = 7;
        break;
    default:
        length = 5;
    }

    return offset + length + ctx->backend->checksum_length;
}

/* Sends a request/response */
int modbus_send_msg(modbus_t *ctx, uint8_t *msg, int msg_length)
{
    int rc;
    int i;

    msg_length = ctx->backend->send_msg_pre(msg, msg_length);

    if (ctx->debug) {
        for (i = 0; i < msg_length; i++)
            printf("[%.2X]", msg[i]);
        printf("\n");
    }

    /* In recovery mode, the write command will be issued until to be
       successful! Disabled by default. */
    do {
        rc = ctx->backend->send(ctx, msg, msg_length);
        if (rc == -1) {
            _error_print(ctx, NULL);
            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) {
                int saved_errno = errno;

                if ((errno == EBADF || errno == ECONNRESET || errno == EPIPE)) {
                    modbus_close(ctx);
                    _sleep_response_timeout(ctx);
                    modbus_connect(ctx);
                } else {
                    _sleep_response_timeout(ctx);
                    modbus_flush(ctx);
                }
                errno = saved_errno;
            }
        }
    } while ((ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) &&
             rc == -1);

    if (rc > 0 && rc != msg_length) {
        errno = EMBBADDATA;
        return -1;
    }

    return rc;
}

int modbus_send_raw_request(modbus_t *ctx, uint8_t *raw_req, int raw_req_length)
{
    sft_t sft;
    uint8_t req[MAX_MESSAGE_LENGTH];
    int req_length;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (raw_req_length < 2 || raw_req_length > (MODBUS_MAX_PDU_LENGTH + 1)) {
        /* The raw request must contain function and slave at least and
           must not be longer than the maximum pdu length plus the slave
           address. */
        errno = EINVAL;
        return -1;
    }

    sft.slave = raw_req[0];
    sft.function = raw_req[1];
    /* The t_id is left to zero */
    sft.t_id = 0;
    /* This response function only set the header so it's convenient here */
    req_length = modbus_build_response_basis(ctx, &sft, req);

    if (raw_req_length > 2) {
        /* Copy data after function code */
        memcpy(req + req_length, raw_req + 2, raw_req_length - 2);
        req_length += raw_req_length - 2;
    }

    return modbus_send_msg(ctx, req, req_length);
}

/*
 *  ---------- Request     Indication ----------
 *  | Client | ---------------------->| Server |
 *  ---------- Confirmation  Response ----------
 */

/* Computes the length to read after the function received */
static uint8_t compute_meta_length_after_function(int function,
                                                  msg_type_t msg_type)
{
    int length;

    if (msg_type == MSG_INDICATION) {
        if (function <= MODBUS_FC_WRITE_SINGLE_REGISTER) {
            length = 4;
        } else if (function == MODBUS_FC_WRITE_MULTIPLE_COILS ||
                   function == MODBUS_FC_WRITE_MULTIPLE_REGISTERS) {
            length = 5;
        } else if (function == MODBUS_FC_MASK_WRITE_REGISTER) {
            length = 6;
        } else if (function == MODBUS_FC_WRITE_AND_READ_REGISTERS) {
            length = 9;
        } else if (function == MODBUS_FC_READ_FILE_RECORD ||
                   function == MODBUS_FC_WRITE_FILE_RECORD) {
            length = 1; /* the length byte */
        } else {
            /* MODBUS_FC_READ_EXCEPTION_STATUS, MODBUS_FC_REPORT_SLAVE_ID */
            length = 0;
        }
    } else {
        /* MSG_CONFIRMATION */
        switch (function) {
        case MODBUS_FC_WRITE_SINGLE_COIL:
        case MODBUS_FC_WRITE_SINGLE_REGISTER:
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            length = 4;
            break;
        case MODBUS_FC_MASK_WRITE_REGISTER:
            length = 6;
            break;
        case MODBUS_FC_READ_FILE_RECORD:
        case MODBUS_FC_WRITE_FILE_RECORD:
        default:
            length = 1; /* a length byte immediately following the function code */
        }
    }

    return length;
}

/* Computes the length to read after the meta information (address, count, etc) */
static int compute_data_length_after_meta(modbus_t *ctx, uint8_t *msg,
                                          msg_type_t msg_type)
{
    int function = msg[ctx->backend->header_length];
    int length;

    if (msg_type == MSG_INDICATION) {
        switch (function) {
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            length = msg[ctx->backend->header_length + 5];
            break;
        case MODBUS_FC_WRITE_AND_READ_REGISTERS:
            length = msg[ctx->backend->header_length + 9];
            break;
        case MODBUS_FC_READ_FILE_RECORD:
        case MODBUS_FC_WRITE_FILE_RECORD:
            length = msg[ctx->backend->header_length + 1];
            break;
        default:
            length = 0;
        }
    } else {
        /* MSG_CONFIRMATION */
        if (function <= MODBUS_FC_READ_INPUT_REGISTERS ||
            function == MODBUS_FC_REPORT_SLAVE_ID ||
            function == MODBUS_FC_WRITE_AND_READ_REGISTERS ||
            function == MODBUS_FC_READ_FILE_RECORD ||
            function == MODBUS_FC_WRITE_FILE_RECORD) {
            length = msg[ctx->backend->header_length + 1]; /* length byte immediately follows the function code */
        } else {
            length = 0;
        }
    }

    length += ctx->backend->checksum_length;

    return length;
}


modbus_rcv_t* _modbus_receive_new(modbus_t *a_ctx, uint8_t *a_msg, msg_type_t a_msg_type)
{
    if (!a_ctx) return NULL;

    modbus_rcv_t* rcvctx = (modbus_rcv_t *)malloc(sizeof(modbus_rcv_t));
    if (!rcvctx) return NULL;

    rcvctx->ctx = a_ctx;
    rcvctx->msg = a_msg;
    rcvctx->msg_type = a_msg_type;
    rcvctx->msg_length = 0;

    if (rcvctx->ctx->debug) {
        if (rcvctx->msg_type == MSG_INDICATION) {
            printf("Waiting for a indication...\n");
        } else {
            printf("Waiting for a confirmation...\n");
        }
    }

    /* We need to analyse the message step by step.  At the first step, we want
     * to reach the function code because all packets contain this
     * information. */
    rcvctx->step = _STEP_FUNCTION;
    rcvctx->length_to_read = rcvctx->ctx->backend->header_length + 1;

    /* prepare response timeout - in case it is a indication, modbus_get_select_timeout() will return NULL */
    rcvctx->tv.tv_sec = rcvctx->ctx->response_timeout.tv_sec;
    rcvctx->tv.tv_usec = rcvctx->ctx->response_timeout.tv_usec;
    return rcvctx;
}


/* an external select must await this timeout */
struct timeval* modbus_get_select_timeout(modbus_rcv_t *rcvctx)
{
    if (rcvctx->msg_length==0 && rcvctx->msg_type == MSG_INDICATION) {
        /* Wait for first byte of an indication message, we don't know when the message will be received */
        return NULL;
    } else {
        return &rcvctx->tv;
    }
}


/* when an external select times out, it must await this timeout and then flush */
struct timeval* modbus_get_recovery_timeout(modbus_rcv_t *rcvctx)
{
  if (rcvctx->ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) {
    return &(rcvctx->ctx->response_timeout);
  }
  else {
    return NULL; /* no recovery, no timeout */
  }
}


/* returns rc and errno of backend's select, handles error recovery if any
 */
static int _modbus_receive_select(modbus_rcv_t *rcvctx)
{
    fd_set rset;
    int rc;

    /* Add a file descriptor to the set */
    FD_ZERO(&rset);
    FD_SET(rcvctx->ctx->s, &rset);

    rc = rcvctx->ctx->backend->select(rcvctx->ctx, &rset, modbus_get_select_timeout(rcvctx), rcvctx->length_to_read);
    if (rc == -1) {
        int saved_errno = errno;
        _error_print(rcvctx->ctx, "select");
        if (rcvctx->ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) {
            if (errno == ETIMEDOUT) {
                _sleep_response_timeout(rcvctx->ctx);
                modbus_flush(rcvctx->ctx);
            } else if (errno == EBADF) {
                modbus_close(rcvctx->ctx);
                modbus_connect(rcvctx->ctx);
            }
            errno = saved_errno;
        }
    }
    return rc;
}



/* must only be called when select (or epoll) indicates that we have data
 * returns:
 * - >=0 length of request received. Can be 0 when received request does not apply to us (e.g. wrong slaveID)
 * - -1 and errno==EAGAIN: must _modbus_receive_select (or functionally equivalent external routine) and then call again
 * - -1 and other errno: failed receiving
 */
int modbus_receive_step(modbus_rcv_t *rcvctx)
{
    int rc = 0;

    if (!rcvctx) return -1;

    if (rcvctx->length_to_read != 0) {

        rc = (int)rcvctx->ctx->backend->recv(rcvctx->ctx, rcvctx->msg + rcvctx->msg_length, rcvctx->length_to_read);
        if (rc == 0) {
            errno = ECONNRESET;
            rc = -1;
        }

        if (rc == -1) {
            int saved_errno = errno;
            _error_print(rcvctx->ctx, "read");
            if ((rcvctx->ctx->error_recovery & MODBUS_ERROR_RECOVERY_LINK) &&
                (saved_errno == ECONNRESET || saved_errno == ECONNREFUSED ||
                 saved_errno == EBADF))
            {
                modbus_close(rcvctx->ctx);
                modbus_connect(rcvctx->ctx);
            }
            errno = saved_errno;
            return -1;
        }

        /* Display the hex code of each character received */
        if (rcvctx->ctx->debug) {
            int i;
            for (i=0; i < rc; i++)
                printf("<%.2X>", rcvctx->msg[rcvctx->msg_length + i]);
        }

        /* Sums bytes received */
        rcvctx->msg_length += rc;
        /* Computes remaining bytes */
        rcvctx->length_to_read -= rc;

        if (rcvctx->length_to_read == 0) {
            switch (rcvctx->step) {
                case _STEP_FUNCTION:
                    /* Function code position */
                    rcvctx->length_to_read = compute_meta_length_after_function(
                        rcvctx->msg[rcvctx->ctx->backend->header_length],
                        rcvctx->msg_type
                    );
                    if (rcvctx->length_to_read != 0) {
                        rcvctx->step = _STEP_META;
                        break;
                    } /* else switches straight to the next step */
                case _STEP_META:
                    rcvctx->length_to_read = compute_data_length_after_meta(rcvctx->ctx, rcvctx->msg, rcvctx->msg_type);
                    if ((rcvctx->msg_length + rcvctx->length_to_read) > (int)rcvctx->ctx->backend->max_adu_length) {
                        _error_print(rcvctx->ctx, "too many data");
                        errno = EMBBADDATA;
                        return -1;
                    }
                    rcvctx->step = _STEP_DATA;
                    break;
                default:
                    break;
            }
        }

        if (rcvctx->length_to_read > 0) {
            /* not yet complete */
            errno = EAGAIN;
            rc = -1;
        }

        if (rcvctx->length_to_read > 0 &&
            (rcvctx->ctx->byte_timeout.tv_sec > 0 || rcvctx->ctx->byte_timeout.tv_usec > 0)) {
            /* If there is no character in the buffer, the allowed timeout
             interval between two consecutive bytes is defined by
             byte_timeout */
            rcvctx->tv.tv_sec = rcvctx->ctx->byte_timeout.tv_sec;
            rcvctx->tv.tv_usec = rcvctx->ctx->byte_timeout.tv_usec;
        }
        /* else timeout isn't set again, the full response must be read before
         expiration of response timeout (for CONFIRMATION only) */
    }

    if (rc>=0) {
        if (rcvctx->ctx->debug)
            printf("\n");
        rc = rcvctx->ctx->backend->check_integrity(rcvctx->ctx, rcvctx->msg, rcvctx->msg_length);
        if (rc==0) rcvctx->msg_length = 0; // ignored because slave ID does not match
    }

    return rc;
}

static int _modbus_receive_dosteps(modbus_rcv_t* rcvctx)
{
    int rc;
    do {
        rc = _modbus_receive_select(rcvctx);
        if (rc == -1) return -1;
        rc = modbus_receive_step(rcvctx);
    } while (rc==-1 && errno==EAGAIN);
    modbus_receive_free(rcvctx);
    return rc;
}


/* Waits a response from a modbus server or a request from a modbus client.
   This function blocks if there is no replies (3 timeouts).

   The function shall return the number of received characters and the received
   message in an array of uint8_t if successful. Otherwise it shall return -1
   and errno is set to one of the values defined below:
   - ECONNRESET
   - EMBBADDATA
   - EMBUNKEXC
   - ETIMEDOUT
   - read() or recv() error codes
*/

int modbus_receive_msg(modbus_t *ctx, uint8_t *msg, msg_type_t msg_type)
{
    if (ctx->slave == MODBUS_BROADCAST_ADDRESS) {
        return 0; // do not wait for confirmation
    }
    modbus_rcv_t* rcvctx = _modbus_receive_new(ctx, msg, msg_type);
    if (!rcvctx) return -1;
    return _modbus_receive_dosteps(rcvctx);
}



/* Prepare receiving the request from a modbus master */
modbus_rcv_t* modbus_receive_new(modbus_t *ctx, uint8_t *req)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return ctx->backend->receive_new(ctx, req);
}


/* Finish receiving the request from a modbus master */
void modbus_receive_free(modbus_rcv_t *rcvctx)
{
    if (rcvctx) {
        rcvctx->ctx->backend->receive_finish(rcvctx);
        free(rcvctx);
    }
}


/* Receive the request from a modbus master */
int modbus_receive(modbus_t *ctx, uint8_t *req)
{
    int rc;

    modbus_rcv_t* rcvctx = modbus_receive_new(ctx, req);
    if (!rcvctx) {
        errno = EINVAL;
        return -1;
    }
    rc =  _modbus_receive_dosteps(rcvctx);
    modbus_receive_free(rcvctx);
    return rc;
}

/* Receives the confirmation.

   The function shall store the read response in rsp and return the number of
   values (bits or words). Otherwise, its shall return -1 and errno is set.

   The function doesn't check the confirmation is the expected response to the
   initial request.
*/
int modbus_receive_confirmation(modbus_t *ctx, uint8_t *rsp)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
}


/* returns offset of function code, or -1 on error */
int modbus_pre_check_confirmation(modbus_t *ctx, uint8_t *req,
                                  uint8_t *rsp, int rsp_length)
{
    int offset = ctx->backend->header_length;
    const int function = rsp[offset];

    if (ctx->backend->pre_check_confirmation) {
        int rc = ctx->backend->pre_check_confirmation(ctx, req, rsp, rsp_length);
        if (rc == -1) {
            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL) {
                _sleep_response_timeout(ctx);
                modbus_flush(ctx);
            }
            // errno should be set by pre_check_confirmation()
            return -1;
        }
    }

    /* Exception code */
    if (function >= 0x80) {
        if (rsp_length == (offset + 2 + (int)ctx->backend->checksum_length) &&
            req[offset] == (rsp[offset] - 0x80)) {
            /* Valid exception code received */

            int exception_code = rsp[offset + 1];
            if (exception_code < MODBUS_EXCEPTION_MAX) {
                errno = MODBUS_ENOBASE + exception_code;
            } else {
                errno = EMBBADEXC;
            }
            _error_print(ctx, NULL);
            return -1;
        } else {
            errno = EMBBADEXC;
            _error_print(ctx, NULL);
            return -1;
        }
    }

    /* Check function code */
    if (function != req[offset]) {
        if (ctx->debug) {
            fprintf(stderr,
                    "Received function not corresponding to the request (0x%X != 0x%X)\n",
                    function, req[offset]);
        }
        if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL) {
            _sleep_response_timeout(ctx);
            modbus_flush(ctx);
        }
        errno = EMBBADDATA;
        return -1;
    }

    /* offset to function code field */
    return offset;
}


int check_confirmation(modbus_t *ctx, uint8_t *req,
                              uint8_t *rsp, int rsp_length)
{
    int rc;
    int rsp_length_computed;
    int function;

    if (rsp_length == 0 && ctx->slave == MODBUS_BROADCAST_ADDRESS) return 0; /* empty confirmation message (=none, in broadcast case) is ok */

    /* check basic confirmation format */
    int offset = modbus_pre_check_confirmation(ctx, req, rsp, rsp_length);
    if (offset<0) {
        return -1;
    }
    function = rsp[offset];
    rsp_length_computed = compute_response_length_from_request(ctx, req);

    /* Check length */
    if ((rsp_length == rsp_length_computed ||
         rsp_length_computed == MSG_LENGTH_UNDEFINED) &&
        function < 0x80) {
        int req_nb_value;
        int rsp_nb_value;

        /* Check the number of values is corresponding to the request */
        switch (function) {
        case MODBUS_FC_READ_COILS:
        case MODBUS_FC_READ_DISCRETE_INPUTS:
            /* Read functions, 8 values in a byte (nb
             * of values in the request and byte count in
             * the response. */
            req_nb_value = (req[offset + 3] << 8) + req[offset + 4];
            req_nb_value = (req_nb_value / 8) + ((req_nb_value % 8) ? 1 : 0);
            rsp_nb_value = rsp[offset + 1];
            break;
        case MODBUS_FC_WRITE_AND_READ_REGISTERS:
        case MODBUS_FC_READ_HOLDING_REGISTERS:
        case MODBUS_FC_READ_INPUT_REGISTERS:
            /* Read functions 1 value = 2 bytes */
            req_nb_value = (req[offset + 3] << 8) + req[offset + 4];
            rsp_nb_value = (rsp[offset + 1] / 2);
            break;
        case MODBUS_FC_WRITE_MULTIPLE_COILS:
        case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
            /* N Write functions */
            req_nb_value = (req[offset + 3] << 8) + req[offset + 4];
            rsp_nb_value = (rsp[offset + 3] << 8) | rsp[offset + 4];
            break;
        case MODBUS_FC_REPORT_SLAVE_ID:
            /* Report slave ID (bytes received) */
            req_nb_value = rsp_nb_value = rsp[offset + 1];
            break;
        default:
            /* 1 Write functions & others */
            req_nb_value = rsp_nb_value = 1;
        }

        if (req_nb_value == rsp_nb_value) {
            rc = rsp_nb_value;
        } else {
            if (ctx->debug) {
                fprintf(stderr,
                        "Quantity not corresponding to the request (%d != %d)\n",
                        rsp_nb_value, req_nb_value);
            }

            if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL) {
                _sleep_response_timeout(ctx);
                modbus_flush(ctx);
            }

            errno = EMBBADDATA;
            rc = -1;
        }
    } else {
        if (ctx->debug) {
            fprintf(stderr,
                    "Message length not corresponding to the computed length (%d != %d)\n",
                    rsp_length, rsp_length_computed);
        }
        if (ctx->error_recovery & MODBUS_ERROR_RECOVERY_PROTOCOL) {
            _sleep_response_timeout(ctx);
            modbus_flush(ctx);
        }
        errno = EMBBADDATA;
        rc = -1;
    }

    return rc;
}

static int response_io_status(uint8_t *tab_io_status,
                              int address, int nb,
                              uint8_t *rsp, int offset)
{
    int shift = 0;
    /* Instead of byte (not allowed in Win32) */
    int one_byte = 0;
    int i;

    for (i = address; i < address + nb; i++) {
        one_byte |= tab_io_status[i] << shift;
        if (shift == 7) {
            /* Byte is full */
            rsp[offset++] = one_byte;
            one_byte = shift = 0;
        } else {
            shift++;
        }
    }

    if (shift != 0)
        rsp[offset++] = one_byte;

    return offset;
}



/* Build the base for a response */
int modbus_build_response_basis(modbus_t *ctx, sft_t *sft, uint8_t *rsp)
{
    return ctx->backend->build_response_basis(sft, rsp);
}


/* Build the base for a request */
int modbus_build_request_basis(modbus_t *ctx, int function, uint8_t *req)
{
    return ctx->backend->build_request_basis(ctx, function, req);
}


/* Build the base for a register access request */
int modbus_build_reg_request_basis(modbus_t *ctx, int function, int addr,
                                   int nb, uint8_t *req)
{
    int reqLen = ctx->backend->build_request_basis(ctx, function, req);
    req[reqLen++] = addr >> 8;
    req[reqLen++] = addr & 0x00ff;
    req[reqLen++] = nb >> 8;
    req[reqLen++] = nb & 0x00ff;
    return reqLen;
}





/* Build an exception response */
int modbus_build_exception_response(modbus_t *ctx, sft_t *sft,
                                    int exception_code, uint8_t *rsp,
                                    unsigned int to_flush,
                                    const char* tmpl, ...)
{
    int rsp_length;

    /* Print debug message */
    if (ctx->debug) {
        va_list ap;

        va_start(ap, tmpl);
        vfprintf(stderr, tmpl, ap);
        va_end(ap);
    }

    /* Flush if required */
    if (to_flush) {
        _sleep_response_timeout(ctx);
        modbus_flush(ctx);
    }

    /* Build exception response */
    sft->function = sft->function + 0x80;
    rsp_length = modbus_build_response_basis(ctx, sft, rsp);
    rsp[rsp_length++] = exception_code;

    return rsp_length;
}






/* process request and generate response
 *
 * returns size of response. Will return 0 when no response is to be sent. This is when request was a broadcast, or when
 * the answer has been sent already as part of the processing
 */
int modbus_process_request(modbus_t *ctx,
                           const uint8_t *req, int req_length,
                           uint8_t *rsp,
                           modbus_function_handler_t func_handler, void *func_handler_context)
{
    int offset;
    sft_t sft;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    offset = ctx->backend->header_length;
    sft.slave = req[offset - 1];
    sft.function = req[offset];
    sft.t_id = ctx->backend->prepare_response_tid(req, &req_length);

    if (func_handler == NULL) {
        errno = EINVAL;
        return -1;
    }

    int rsp_length = func_handler(ctx, &sft, offset, req, req_length, rsp, func_handler_context);

    /* Suppress any responses when the request was a broadcast */
    return (sft.slave == MODBUS_BROADCAST_ADDRESS) ? 0 : rsp_length;
}


/* standard register mapping handler, with optional callback for access notification
 * user_ctx must be a modbus_mapping_ex_t
 */
static int reg_mapping_handler_ex(modbus_t* ctx, sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp, void *user_ctx)
{
    uint16_t address;
    const char *errTxt = NULL;
    int rsp_length = 0;
    int function = sft->function;

    modbus_mapping_ex_t* mb_mapping_ex = (modbus_mapping_ex_t*)user_ctx;
    if (mb_mapping_ex==NULL) return -1;
    modbus_mapping_t *mb_mapping = mb_mapping_ex->mappings;
    if (mb_mapping==NULL) return -1;
    modbus_access_handler_t accesshandler = mb_mapping_ex->access_handler;

    /* note: payload[0]==function code, altough already contained in sft */
    // 00 01 00 00 00 06 01 03 00 65 00 14
    //                      ^^=0=FC
    address = (req[offset + 1] << 8) + req[offset + 2];

    switch (function) {
    case MODBUS_FC_READ_COILS:
    case MODBUS_FC_READ_DISCRETE_INPUTS: {
        unsigned int is_input = (function == MODBUS_FC_READ_DISCRETE_INPUTS);
        int start_bits = is_input ? mb_mapping->start_input_bits : mb_mapping->start_bits;
        int nb_bits = is_input ? mb_mapping->nb_input_bits : mb_mapping->nb_bits;
        uint8_t *tab_bits = is_input ? mb_mapping->tab_input_bits : mb_mapping->tab_bits;
        const char * const name = is_input ? "read_input_bits" : "read_bits";
        int nb = (req[offset + 3] << 8) + req[offset + 4];
        /* The mapping can be shifted to reduce memory consumption and it
           doesn't always start at address zero. */
        int mapping_address = address - start_bits;

        if (nb < 1 || MODBUS_MAX_READ_BITS < nb) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, TRUE,
                "Illegal nb of values %d in %s (max %d)\n",
                nb, name, MODBUS_MAX_READ_BITS);
        } else if (mapping_address < 0 || (mapping_address + nb) > nb_bits) {
            rsp_length = modbus_build_exception_response(
                ctx, sft,
                MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in %s\n",
                mapping_address < 0 ? address : address + nb, name);
        } else {
            if (accesshandler) {
                errTxt = accesshandler(ctx, mb_mapping, is_input ? read_input_bit : read_bit, mapping_address, nb, (modbus_data_t)tab_bits, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            rsp_length = modbus_build_response_basis(ctx, sft, rsp);
            rsp[rsp_length++] = (nb / 8) + ((nb % 8) ? 1 : 0);
            rsp_length = response_io_status(tab_bits, mapping_address, nb,
                                            rsp, rsp_length);
        }
    }
        break;
    case MODBUS_FC_READ_HOLDING_REGISTERS:
    case MODBUS_FC_READ_INPUT_REGISTERS: {
        unsigned int is_input = (function == MODBUS_FC_READ_INPUT_REGISTERS);
        int start_registers = is_input ? mb_mapping->start_input_registers : mb_mapping->start_registers;
        int nb_registers = is_input ? mb_mapping->nb_input_registers : mb_mapping->nb_registers;
        uint16_t *tab_registers = is_input ? mb_mapping->tab_input_registers : mb_mapping->tab_registers;
        const char * const name = is_input ? "read_input_registers" : "read_registers";
        int nb = (req[offset + 3] << 8) + req[offset + 4];
        /* The mapping can be shifted to reduce memory consumption and it
           doesn't always start at address zero. */
        int mapping_address = address - start_registers;

        if (nb < 1 || MODBUS_MAX_READ_REGISTERS < nb) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, TRUE,
                "Illegal nb of values %d in %s (max %d)\n",
                nb, name, MODBUS_MAX_READ_REGISTERS);
        } else if (mapping_address < 0 || (mapping_address + nb) > nb_registers) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in %s\n",
                mapping_address < 0 ? address : address + nb, name);
        } else {
            int i;
            if (accesshandler) {
                errTxt = accesshandler(ctx, mb_mapping, is_input ? read_input_reg : read_reg, mapping_address, nb, (modbus_data_t)tab_registers, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            rsp_length = modbus_build_response_basis(ctx, sft, rsp);
            rsp[rsp_length++] = nb << 1;
            for (i = mapping_address; i < mapping_address + nb; i++) {
                rsp[rsp_length++] = tab_registers[i] >> 8;
                rsp[rsp_length++] = tab_registers[i] & 0xFF;
            }
        }
    }
        break;
    case MODBUS_FC_WRITE_SINGLE_COIL: {
        int mapping_address = address - mb_mapping->start_bits;

        if (mapping_address < 0 || mapping_address >= mb_mapping->nb_bits) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in write_bit\n",
                address);
        } else {
            int data = (req[offset + 3] << 8) + req[offset + 4];

            if (data == 0xFF00 || data == 0x0) {
                if (accesshandler) {
                    errTxt = accesshandler(ctx, mb_mapping, write_bit, mapping_address, 1, (modbus_data_t)mb_mapping->tab_bits, mb_mapping_ex->access_handler_user_ctx);
                    if (errTxt) break;
                }
                mb_mapping->tab_bits[mapping_address] = data ? ON : OFF;
                memcpy(rsp, req, req_length);
                rsp_length = req_length;
            } else {
                rsp_length = modbus_build_exception_response(
                    ctx, sft,
                    MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, FALSE,
                    "Illegal data value 0x%0X in write_bit request at address %0X\n",
                    data, address);
            }
        }
    }
        break;
    case MODBUS_FC_WRITE_SINGLE_REGISTER: {
        int mapping_address = address - mb_mapping->start_registers;

        if (mapping_address < 0 || mapping_address >= mb_mapping->nb_registers) {
            rsp_length = modbus_build_exception_response(
                ctx, sft,
                MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in write_register\n",
                address);
        } else {
            int data = (req[offset + 3] << 8) + req[offset + 4];

            if (accesshandler) {
                errTxt = accesshandler(ctx, mb_mapping, write_reg, mapping_address, 1, (modbus_data_t)mb_mapping->tab_registers, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            mb_mapping->tab_registers[mapping_address] = data;
            memcpy(rsp, req, req_length);
            rsp_length = req_length;
        }
    }
        break;
    case MODBUS_FC_WRITE_MULTIPLE_COILS: {
        int nb = (req[offset + 3] << 8) + req[offset + 4];
        int mapping_address = address - mb_mapping->start_bits;

        if (nb < 1 || MODBUS_MAX_WRITE_BITS < nb) {
            /* May be the indication has been truncated on reading because of
             * invalid address (eg. nb is 0 but the request contains values to
             * write) so it's necessary to flush. */
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, TRUE,
                "Illegal number of values %d in write_bits (max %d)\n",
                nb, MODBUS_MAX_WRITE_BITS);
        } else if (mapping_address < 0 ||
                   (mapping_address + nb) > mb_mapping->nb_bits) {
            rsp_length = modbus_build_exception_response(
                ctx, sft,
                MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in write_bits\n",
                mapping_address < 0 ? address : address + nb);
        } else {
            /* 6 = byte count */
            modbus_set_bits_from_bytes(mb_mapping->tab_bits, mapping_address, nb,
                                       &req[offset + 6]);
            if (accesshandler) {
                errTxt = accesshandler(ctx, mb_mapping, write_bit, mapping_address, nb, (modbus_data_t)mb_mapping->tab_bits, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            rsp_length = modbus_build_response_basis(ctx, sft, rsp);
            /* 4 to copy the bit address (2) and the quantity of bits */
            memcpy(rsp + rsp_length, req + rsp_length, 4);
            rsp_length += 4;
        }
    }
        break;
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS: {
        int nb = (req[offset + 3] << 8) + req[offset + 4];
        int mapping_address = address - mb_mapping->start_registers;

        if (nb < 1 || MODBUS_MAX_WRITE_REGISTERS < nb) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, TRUE,
                "Illegal number of values %d in write_registers (max %d)\n",
                nb, MODBUS_MAX_WRITE_REGISTERS);
        } else if (mapping_address < 0 ||
                   (mapping_address + nb) > mb_mapping->nb_registers) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in write_registers\n",
                mapping_address < 0 ? address : address + nb);
        } else {
            int i, j;
            for (i = mapping_address, j = 6; i < mapping_address + nb; i++, j += 2) {
                /* 6 and 7 = first value */
                mb_mapping->tab_registers[i] =
                    (req[offset + j] << 8) + req[offset + j + 1];
            }

            if (accesshandler) {
                errTxt = accesshandler(ctx, mb_mapping, write_reg, mapping_address, nb, (modbus_data_t)mb_mapping->tab_registers, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            rsp_length = modbus_build_response_basis(ctx, sft, rsp);
            /* 4 to copy the address (2) and the no. of registers */
            memcpy(rsp + rsp_length, req + rsp_length, 4);
            rsp_length += 4;
        }
    }
        break;
    case MODBUS_FC_REPORT_SLAVE_ID: {
        size_t str_len;
        int byte_count_pos;

        rsp_length = modbus_build_response_basis(ctx, sft, rsp);
        /* Skip byte count for now */
        byte_count_pos = rsp_length++;
        rsp[rsp_length++] = _REPORT_SLAVE_ID;
        /* Run indicator status to ON */
        rsp[rsp_length++] = 0xFF;
        /* LMB + length of LIBMODBUS_VERSION_STRING */
        str_len = strlen(ctx->slave_id);
        memcpy(rsp + rsp_length, ctx->slave_id, str_len);
        rsp_length += str_len;
        rsp[byte_count_pos] = rsp_length - byte_count_pos - 1;
    }
        break;
    case MODBUS_FC_READ_EXCEPTION_STATUS:
        if (ctx->debug) {
            fprintf(stderr, "FIXME Not implemented\n");
        }
        errno = ENOPROTOOPT;
        return -1;
        break;
    case MODBUS_FC_MASK_WRITE_REGISTER: {
        int mapping_address = address - mb_mapping->start_registers;

        if (mapping_address < 0 || mapping_address >= mb_mapping->nb_registers) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data address 0x%0X in write_register\n",
                address);
        } else {
            uint16_t data = mb_mapping->tab_registers[mapping_address];
            uint16_t and = (req[offset + 3] << 8) + req[offset + 4];
            uint16_t or = (req[offset + 5] << 8) + req[offset + 6];

            data = (data & and) | (or & (~and));
            mb_mapping->tab_registers[mapping_address] = data;

            if (accesshandler) {
                errTxt = accesshandler(ctx, mb_mapping, write_reg, mapping_address, 1, (modbus_data_t)mb_mapping->tab_registers, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            memcpy(rsp, req, req_length);
            rsp_length = req_length;
        }
    }
        break;
    case MODBUS_FC_WRITE_AND_READ_REGISTERS: {
        int nb = (req[offset + 3] << 8) + req[offset + 4];
        uint16_t address_write = (req[offset + 5] << 8) + req[offset + 6];
        int nb_write = (req[offset + 7] << 8) + req[offset + 8];
        int nb_write_bytes = req[offset + 9];
        int mapping_address = address - mb_mapping->start_registers;
        int mapping_address_write = address_write - mb_mapping->start_registers;

        if (nb_write < 1 || MODBUS_MAX_WR_WRITE_REGISTERS < nb_write ||
            nb < 1 || MODBUS_MAX_WR_READ_REGISTERS < nb ||
            nb_write_bytes != nb_write * 2) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, TRUE,
                "Illegal nb of values (W%d, R%d) in write_and_read_registers (max W%d, R%d)\n",
                nb_write, nb, MODBUS_MAX_WR_WRITE_REGISTERS, MODBUS_MAX_WR_READ_REGISTERS);
        } else if (mapping_address < 0 ||
                   (mapping_address + nb) > mb_mapping->nb_registers ||
                   mapping_address < 0 ||
                   (mapping_address_write + nb_write) > mb_mapping->nb_registers) {
            rsp_length = modbus_build_exception_response(
                ctx, sft, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS, rsp, FALSE,
                "Illegal data read address 0x%0X or write address 0x%0X write_and_read_registers\n",
                mapping_address < 0 ? address : address + nb,
                mapping_address_write < 0 ? address_write : address_write + nb_write);
        } else {
            int i, j;
            rsp_length = modbus_build_response_basis(ctx, sft, rsp);
            rsp[rsp_length++] = nb << 1;

            /* Write first.
               10 and 11 are the offset of the first values to write */
            for (i = mapping_address_write, j = 10;
                 i < mapping_address_write + nb_write; i++, j += 2) {
                mb_mapping->tab_registers[i] =
                    (req[offset + j] << 8) + req[offset + j + 1];
            }
            if (accesshandler) {
                /* report write first */
                errTxt = accesshandler(ctx, mb_mapping, write_reg, mapping_address_write, nb_write, (modbus_data_t)mb_mapping->tab_registers, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
                /* then announce reading */
                errTxt = accesshandler(ctx, mb_mapping, read_reg, mapping_address, nb, (modbus_data_t)mb_mapping->tab_registers, mb_mapping_ex->access_handler_user_ctx);
                if (errTxt) break;
            }
            /* and read the data for the response */
            for (i = mapping_address; i < mapping_address + nb; i++) {
                rsp[rsp_length++] = mb_mapping->tab_registers[i] >> 8;
                rsp[rsp_length++] = mb_mapping->tab_registers[i] & 0xFF;
            }
        }
    }
        break;

    default:
        rsp_length = modbus_build_exception_response(
            ctx, sft, MODBUS_EXCEPTION_ILLEGAL_FUNCTION, rsp, TRUE,
            "Unknown Modbus function code: 0x%0X\n", function);
        break;
    }
    /* handle error from value access handler */
    if (errTxt) {
        rsp_length = modbus_build_exception_response(
            ctx, sft,
            MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE, rsp, FALSE,
            "Error accessing data: %-100s\n",
            errTxt);
    }

    return rsp_length;
}


/* public API for using standard register mapping as part of custom request handling
 */
int modbus_reg_mapping_handler(modbus_t* ctx, sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp, modbus_mapping_ex_t* mapping_ex)
{
    return reg_mapping_handler_ex(ctx, sft, offset, req, req_length, rsp, mapping_ex);
}



/* Send a response to the received request.
   Analyses the request and constructs a response.

   If an error occurs, this function construct the response
   accordingly.
*/
int modbus_reply(modbus_t *ctx, const uint8_t *req,
                 int req_length, modbus_mapping_t *mb_mapping)
{
    uint8_t rsp[MAX_MESSAGE_LENGTH];
    int rsp_length = 0;

    /* standard processing via mapping to registers */
    modbus_mapping_ex_t map;
    map.mappings = mb_mapping;
    map.access_handler = NULL;
    map.access_handler_user_ctx = NULL;
    rsp_length = modbus_process_request(ctx, req, req_length, rsp, reg_mapping_handler_ex, &map);

    /* Send response, if any */
    return (rsp_length > 0) ? modbus_send_msg(ctx, rsp, rsp_length) : 0;
}



int modbus_reply_exception(modbus_t *ctx, const uint8_t *req,
                           unsigned int exception_code)
{
    int offset;
    int slave;
    int function;
    uint8_t rsp[MAX_MESSAGE_LENGTH];
    int rsp_length;
    int dummy_length = 99;
    sft_t sft;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    offset = ctx->backend->header_length;
    slave = req[offset - 1];
    function = req[offset];

    sft.slave = slave;
    sft.function = function + 0x80;;
    sft.t_id = ctx->backend->prepare_response_tid(req, &dummy_length);
    rsp_length = modbus_build_response_basis(ctx, &sft, rsp);

    /* Positive exception code */
    if (exception_code < MODBUS_EXCEPTION_MAX) {
        rsp[rsp_length++] = exception_code;
        return modbus_send_msg(ctx, rsp, rsp_length);
    } else {
        errno = EINVAL;
        return -1;
    }
}

/* Reads IO status */
static int read_io_status(modbus_t *ctx, int function,
                          int addr, int nb, uint8_t *dest)
{
    int rc;
    int req_length;

    uint8_t req[_MIN_REQ_LENGTH];
    uint8_t rsp[MAX_MESSAGE_LENGTH];

    req_length = modbus_build_reg_request_basis(ctx, function, addr, nb, req);

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        int i, temp, bit;
        int pos = 0;
        int offset;
        int offset_end;

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
        if (rc == -1)
            return -1;

        offset = ctx->backend->header_length + 2;
        offset_end = offset + rc;
        for (i = offset; i < offset_end; i++) {
            /* Shift reg hi_byte to temp */
            temp = rsp[i];

            for (bit = 0x01; (bit & 0xff) && (pos < nb);) {
                dest[pos++] = (temp & bit) ? TRUE : FALSE;
                bit = bit << 1;
            }

        }
    }

    return rc;
}

/* Reads the boolean status of bits and sets the array elements
   in the destination to TRUE or FALSE (single bits). */
int modbus_read_bits(modbus_t *ctx, int addr, int nb, uint8_t *dest)
{
    int rc;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (nb > MODBUS_MAX_READ_BITS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Too many bits requested (%d > %d)\n",
                    nb, MODBUS_MAX_READ_BITS);
        }
        errno = EMBMDATA;
        return -1;
    }

    rc = read_io_status(ctx, MODBUS_FC_READ_COILS, addr, nb, dest);

    if (rc == -1)
        return -1;
    else
        return nb;
}


/* Same as modbus_read_bits but reads the remote device input table */
int modbus_read_input_bits(modbus_t *ctx, int addr, int nb, uint8_t *dest)
{
    int rc;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (nb > MODBUS_MAX_READ_BITS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Too many discrete inputs requested (%d > %d)\n",
                    nb, MODBUS_MAX_READ_BITS);
        }
        errno = EMBMDATA;
        return -1;
    }

    rc = read_io_status(ctx, MODBUS_FC_READ_DISCRETE_INPUTS, addr, nb, dest);

    if (rc == -1)
        return -1;
    else
        return nb;
}

/* Reads the data from a remove device and put that data into an array */
static int read_registers(modbus_t *ctx, int function, int addr, int nb,
                          uint16_t *dest)
{
    int rc;
    int req_length;
    uint8_t req[_MIN_REQ_LENGTH];
    uint8_t rsp[MAX_MESSAGE_LENGTH];

    if (nb > MODBUS_MAX_READ_REGISTERS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Too many registers requested (%d > %d)\n",
                    nb, MODBUS_MAX_READ_REGISTERS);
        }
        errno = EMBMDATA;
        return -1;
    }

    req_length = modbus_build_reg_request_basis(ctx, function, addr, nb, req);

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        int offset;
        int i;

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
        if (rc == -1)
            return -1;

        offset = ctx->backend->header_length;

        for (i = 0; i < rc; i++) {
            /* shift reg hi_byte to temp OR with lo_byte */
            dest[i] = (rsp[offset + 2 + (i << 1)] << 8) |
                rsp[offset + 3 + (i << 1)];
        }
    }

    return rc;
}

/* Reads the holding registers of remote device and put the data into an
   array */
int modbus_read_registers(modbus_t *ctx, int addr, int nb, uint16_t *dest)
{
    int status;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (nb > MODBUS_MAX_READ_REGISTERS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Too many registers requested (%d > %d)\n",
                    nb, MODBUS_MAX_READ_REGISTERS);
        }
        errno = EMBMDATA;
        return -1;
    }

    status = read_registers(ctx, MODBUS_FC_READ_HOLDING_REGISTERS,
                            addr, nb, dest);
    return status;
}

/* Reads the input registers of remote device and put the data into an array */
int modbus_read_input_registers(modbus_t *ctx, int addr, int nb,
                                uint16_t *dest)
{
    int status;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (nb > MODBUS_MAX_READ_REGISTERS) {
        fprintf(stderr,
                "ERROR Too many input registers requested (%d > %d)\n",
                nb, MODBUS_MAX_READ_REGISTERS);
        errno = EMBMDATA;
        return -1;
    }

    status = read_registers(ctx, MODBUS_FC_READ_INPUT_REGISTERS,
                            addr, nb, dest);

    return status;
}

/* Write a value to the specified register of the remote device.
   Used by write_bit and write_register */
static int write_single(modbus_t *ctx, int function, int addr, int value)
{
    int rc;
    int req_length;
    uint8_t req[_MIN_REQ_LENGTH];

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    req_length = modbus_build_reg_request_basis(ctx, function, addr, value, req);

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        /* Used by write_bit and write_register */
        uint8_t rsp[MAX_MESSAGE_LENGTH];

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
    }

    return rc;
}

/* Turns ON or OFF a single bit of the remote device */
int modbus_write_bit(modbus_t *ctx, int addr, int status)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return write_single(ctx, MODBUS_FC_WRITE_SINGLE_COIL, addr,
                        status ? 0xFF00 : 0);
}

/* Writes a value in one register of the remote device */
int modbus_write_register(modbus_t *ctx, int addr, int value)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return write_single(ctx, MODBUS_FC_WRITE_SINGLE_REGISTER, addr, value);
}

/* Write the bits of the array in the remote device */
int modbus_write_bits(modbus_t *ctx, int addr, int nb, const uint8_t *src)
{
    int rc;
    int i;
    int byte_count;
    int req_length;
    int bit_check = 0;
    int pos = 0;
    uint8_t req[MAX_MESSAGE_LENGTH];

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (nb > MODBUS_MAX_WRITE_BITS) {
        if (ctx->debug) {
            fprintf(stderr, "ERROR Writing too many bits (%d > %d)\n",
                    nb, MODBUS_MAX_WRITE_BITS);
        }
        errno = EMBMDATA;
        return -1;
    }

    req_length = modbus_build_reg_request_basis(ctx,
                                                MODBUS_FC_WRITE_MULTIPLE_COILS,
                                                addr, nb, req);
    byte_count = (nb / 8) + ((nb % 8) ? 1 : 0);
    req[req_length++] = byte_count;

    for (i = 0; i < byte_count; i++) {
        int bit;

        bit = 0x01;
        req[req_length] = 0;

        while ((bit & 0xFF) && (bit_check++ < nb)) {
            if (src[pos++])
                req[req_length] |= bit;
            else
                req[req_length] &=~ bit;

            bit = bit << 1;
        }
        req_length++;
    }

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        uint8_t rsp[MAX_MESSAGE_LENGTH];

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
    }


    return rc;
}

/* Write the values from the array to the registers of the remote device */
int modbus_write_registers(modbus_t *ctx, int addr, int nb, const uint16_t *src)
{
    int rc;
    int i;
    int req_length;
    int byte_count;
    uint8_t req[MAX_MESSAGE_LENGTH];

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (nb > MODBUS_MAX_WRITE_REGISTERS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Trying to write to too many registers (%d > %d)\n",
                    nb, MODBUS_MAX_WRITE_REGISTERS);
        }
        errno = EMBMDATA;
        return -1;
    }

    req_length = modbus_build_reg_request_basis(ctx,
                                                MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                                                addr, nb, req);
    byte_count = nb * 2;
    req[req_length++] = byte_count;

    for (i = 0; i < nb; i++) {
        req[req_length++] = src[i] >> 8;
        req[req_length++] = src[i] & 0x00FF;
    }

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        uint8_t rsp[MAX_MESSAGE_LENGTH];

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
    }

    return rc;
}

int modbus_mask_write_register(modbus_t *ctx, int addr, uint16_t and_mask, uint16_t or_mask)
{
    int rc;
    int req_length;
    /* The request length can not exceed _MIN_REQ_LENGTH - 2 and 4 bytes to
     * store the masks. The ugly substraction is there to remove the 'nb' value
     * (2 bytes) which is not used. */
    uint8_t req[_MIN_REQ_LENGTH + 2];

    req_length = modbus_build_reg_request_basis(ctx,
                                                MODBUS_FC_MASK_WRITE_REGISTER,
                                                addr, 0, req);

    /* HACKISH, count is not used */
    req_length -= 2;

    req[req_length++] = and_mask >> 8;
    req[req_length++] = and_mask & 0x00ff;
    req[req_length++] = or_mask >> 8;
    req[req_length++] = or_mask & 0x00ff;

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        /* Used by write_bit and write_register */
        uint8_t rsp[MAX_MESSAGE_LENGTH];

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
    }

    return rc;
}

/* Write multiple registers from src array to remote device and read multiple
   registers from remote device to dest array. */
int modbus_write_and_read_registers(modbus_t *ctx,
                                    int write_addr, int write_nb,
                                    const uint16_t *src,
                                    int read_addr, int read_nb,
                                    uint16_t *dest)

{
    int rc;
    int req_length;
    int i;
    int byte_count;
    uint8_t req[MAX_MESSAGE_LENGTH];
    uint8_t rsp[MAX_MESSAGE_LENGTH];

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (write_nb > MODBUS_MAX_WR_WRITE_REGISTERS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Too many registers to write (%d > %d)\n",
                    write_nb, MODBUS_MAX_WR_WRITE_REGISTERS);
        }
        errno = EMBMDATA;
        return -1;
    }

    if (read_nb > MODBUS_MAX_WR_READ_REGISTERS) {
        if (ctx->debug) {
            fprintf(stderr,
                    "ERROR Too many registers requested (%d > %d)\n",
                    read_nb, MODBUS_MAX_WR_READ_REGISTERS);
        }
        errno = EMBMDATA;
        return -1;
    }
    req_length = modbus_build_reg_request_basis(ctx,
                                                MODBUS_FC_WRITE_AND_READ_REGISTERS,
                                                read_addr, read_nb, req);

    req[req_length++] = write_addr >> 8;
    req[req_length++] = write_addr & 0x00ff;
    req[req_length++] = write_nb >> 8;
    req[req_length++] = write_nb & 0x00ff;
    byte_count = write_nb * 2;
    req[req_length++] = byte_count;

    for (i = 0; i < write_nb; i++) {
        req[req_length++] = src[i] >> 8;
        req[req_length++] = src[i] & 0x00FF;
    }

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        int offset;

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
        if (rc == -1)
            return -1;

        offset = ctx->backend->header_length;
        for (i = 0; i < rc; i++) {
            /* shift reg hi_byte to temp OR with lo_byte */
            dest[i] = (rsp[offset + 2 + (i << 1)] << 8) |
                rsp[offset + 3 + (i << 1)];
        }
    }

    return rc;
}

/* Send a request to get the slave ID of the device (only available in serial
   communication). */
int modbus_report_slave_id(modbus_t *ctx, int max_dest, uint8_t *dest)
{
    int rc;
    int req_length;
    uint8_t req[_MIN_REQ_LENGTH];

    if (ctx == NULL || max_dest <= 0) {
        errno = EINVAL;
        return -1;
    }

    req_length = modbus_build_reg_request_basis(ctx, MODBUS_FC_REPORT_SLAVE_ID,
                                                0, 0, req);

    /* HACKISH, addr and count are not used */
    req_length -= 4;

    rc = modbus_send_msg(ctx, req, req_length);
    if (rc > 0) {
        int i;
        int offset;
        uint8_t rsp[MAX_MESSAGE_LENGTH];

        rc = modbus_receive_msg(ctx, rsp, MSG_CONFIRMATION);
        if (rc == -1)
            return -1;

        rc = check_confirmation(ctx, req, rsp, rc);
        if (rc == -1)
            return -1;

        offset = ctx->backend->header_length + 2;

        /* Byte count, slave id, run indicator status and
           additional data. Truncate copy to max_dest. */
        for (i=0; i < rc && i < max_dest; i++) {
            dest[i] = rsp[offset + i];
        }
    }

    return rc;
}

void _modbus_init_common(modbus_t *ctx)
{
    /* Slave and socket are initialized to -1 */
    ctx->slave = -1;
    ctx->s = -1;
    ctx->slave_id = "LMB" LIBMODBUS_VERSION_STRING; // default ID

    ctx->debug = FALSE;
    ctx->error_recovery = MODBUS_ERROR_RECOVERY_NONE;

    ctx->response_timeout.tv_sec = 0;
    ctx->response_timeout.tv_usec = _RESPONSE_TIMEOUT;

    ctx->byte_timeout.tv_sec = 0;
    ctx->byte_timeout.tv_usec = _BYTE_TIMEOUT;
}

/* Define the slave number */
int modbus_set_slave(modbus_t *ctx, int slave)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return ctx->backend->set_slave(ctx, slave);
}

int modbus_set_slave_id(modbus_t* ctx, const char* idtext)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    ctx->slave_id = idtext;
    return 0;
}


int modbus_set_error_recovery(modbus_t *ctx,
                              modbus_error_recovery_mode error_recovery)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* The type of modbus_error_recovery_mode is unsigned enum */
    ctx->error_recovery = (uint8_t) error_recovery;
    return 0;
}

int modbus_set_socket(modbus_t *ctx, int s)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    ctx->s = s;
    return 0;
}

int modbus_get_socket(modbus_t *ctx)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return ctx->s;
}

/* Get the timeout interval used to wait for a response */
int modbus_get_response_timeout(modbus_t *ctx, uint32_t *to_sec, uint32_t *to_usec)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    *to_sec = ctx->response_timeout.tv_sec;
    *to_usec = ctx->response_timeout.tv_usec;
    return 0;
}

int modbus_set_response_timeout(modbus_t *ctx, uint32_t to_sec, uint32_t to_usec)
{
    if (ctx == NULL ||
        (to_sec == 0 && to_usec == 0) || to_usec > 999999) {
        errno = EINVAL;
        return -1;
    }

    ctx->response_timeout.tv_sec = to_sec;
    ctx->response_timeout.tv_usec = to_usec;
    return 0;
}

/* Get the timeout interval between two consecutive bytes of a message */
int modbus_get_byte_timeout(modbus_t *ctx, uint32_t *to_sec, uint32_t *to_usec)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    *to_sec = ctx->byte_timeout.tv_sec;
    *to_usec = ctx->byte_timeout.tv_usec;
    return 0;
}

int modbus_set_byte_timeout(modbus_t *ctx, uint32_t to_sec, uint32_t to_usec)
{
    /* Byte timeout can be disabled when both values are zero */
    if (ctx == NULL || to_usec > 999999) {
        errno = EINVAL;
        return -1;
    }

    ctx->byte_timeout.tv_sec = to_sec;
    ctx->byte_timeout.tv_usec = to_usec;
    return 0;
}

int modbus_get_header_length(modbus_t *ctx)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return ctx->backend->header_length;
}

int modbus_connect(modbus_t *ctx)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    return ctx->backend->connect(ctx);
}

void modbus_close(modbus_t *ctx)
{
    if (ctx == NULL)
        return;

    ctx->backend->close(ctx);
}

void modbus_free(modbus_t *ctx)
{
    if (ctx == NULL)
        return;

    ctx->backend->free(ctx);
}

int modbus_set_debug(modbus_t *ctx, int flag)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    ctx->debug = flag;
    return 0;
}

/* Allocates 4 arrays to store bits, input bits, registers and inputs
   registers. The pointers are stored in modbus_mapping structure.

   The modbus_mapping_new_ranges() function shall return the new allocated
   structure if successful. Otherwise it shall return NULL and set errno to
   ENOMEM. */
modbus_mapping_t* modbus_mapping_new_start_address(
    unsigned int start_bits, unsigned int nb_bits,
    unsigned int start_input_bits, unsigned int nb_input_bits,
    unsigned int start_registers, unsigned int nb_registers,
    unsigned int start_input_registers, unsigned int nb_input_registers)
{
    modbus_mapping_t *mb_mapping;

    mb_mapping = (modbus_mapping_t *)malloc(sizeof(modbus_mapping_t));
    if (mb_mapping == NULL) {
        return NULL;
    }

    /* 0X */
    mb_mapping->nb_bits = nb_bits;
    mb_mapping->start_bits = start_bits;
    if (nb_bits == 0) {
        mb_mapping->tab_bits = NULL;
    } else {
        /* Negative number raises a POSIX error */
        mb_mapping->tab_bits =
            (uint8_t *) malloc(nb_bits * sizeof(uint8_t));
        if (mb_mapping->tab_bits == NULL) {
            free(mb_mapping);
            return NULL;
        }
        memset(mb_mapping->tab_bits, 0, nb_bits * sizeof(uint8_t));
    }

    /* 1X */
    mb_mapping->nb_input_bits = nb_input_bits;
    mb_mapping->start_input_bits = start_input_bits;
    if (nb_input_bits == 0) {
        mb_mapping->tab_input_bits = NULL;
    } else {
        mb_mapping->tab_input_bits =
            (uint8_t *) malloc(nb_input_bits * sizeof(uint8_t));
        if (mb_mapping->tab_input_bits == NULL) {
            free(mb_mapping->tab_bits);
            free(mb_mapping);
            return NULL;
        }
        memset(mb_mapping->tab_input_bits, 0, nb_input_bits * sizeof(uint8_t));
    }

    /* 4X */
    mb_mapping->nb_registers = nb_registers;
    mb_mapping->start_registers = start_registers;
    if (nb_registers == 0) {
        mb_mapping->tab_registers = NULL;
    } else {
        mb_mapping->tab_registers =
            (uint16_t *) malloc(nb_registers * sizeof(uint16_t));
        if (mb_mapping->tab_registers == NULL) {
            free(mb_mapping->tab_input_bits);
            free(mb_mapping->tab_bits);
            free(mb_mapping);
            return NULL;
        }
        memset(mb_mapping->tab_registers, 0, nb_registers * sizeof(uint16_t));
    }

    /* 3X */
    mb_mapping->nb_input_registers = nb_input_registers;
    mb_mapping->start_input_registers = start_input_registers;
    if (nb_input_registers == 0) {
        mb_mapping->tab_input_registers = NULL;
    } else {
        mb_mapping->tab_input_registers =
            (uint16_t *) malloc(nb_input_registers * sizeof(uint16_t));
        if (mb_mapping->tab_input_registers == NULL) {
            free(mb_mapping->tab_registers);
            free(mb_mapping->tab_input_bits);
            free(mb_mapping->tab_bits);
            free(mb_mapping);
            return NULL;
        }
        memset(mb_mapping->tab_input_registers, 0,
               nb_input_registers * sizeof(uint16_t));
    }

    return mb_mapping;
}

modbus_mapping_t* modbus_mapping_new(int nb_bits, int nb_input_bits,
                                     int nb_registers, int nb_input_registers)
{
    return modbus_mapping_new_start_address(
        0, nb_bits, 0, nb_input_bits, 0, nb_registers, 0, nb_input_registers);
}

/* Frees the 4 arrays */
void modbus_mapping_free(modbus_mapping_t *mb_mapping)
{
    if (mb_mapping == NULL) {
        return;
    }

    free(mb_mapping->tab_input_registers);
    free(mb_mapping->tab_registers);
    free(mb_mapping->tab_input_bits);
    free(mb_mapping->tab_bits);
    free(mb_mapping);
}

#ifndef HAVE_STRLCPY
/*
 * Function strlcpy was originally developed by
 * Todd C. Miller <Todd.Miller@courtesan.com> to simplify writing secure code.
 * See ftp://ftp.openbsd.org/pub/OpenBSD/src/lib/libc/string/strlcpy.3
 * for more information.
 *
 * Thank you Ulrich Drepper... not!
 *
 * Copy src to string dest of size dest_size.  At most dest_size-1 characters
 * will be copied.  Always NUL terminates (unless dest_size == 0).  Returns
 * strlen(src); if retval >= dest_size, truncation occurred.
 */
size_t strlcpy(char *dest, const char *src, size_t dest_size)
{
    register char *d = dest;
    register const char *s = src;
    register size_t n = dest_size;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
        do {
            if ((*d++ = *s++) == 0)
                break;
        } while (--n != 0);
    }

    /* Not enough room in dest, add NUL and traverse rest of src */
    if (n == 0) {
        if (dest_size != 0)
            *d = '\0'; /* NUL-terminate dest */
        while (*s++)
            ;
    }

    return (s - src - 1); /* count does not include NUL */
}
#endif /* HAVE_STRLCPY */

#endif /* ENABLE_MODBUS */

