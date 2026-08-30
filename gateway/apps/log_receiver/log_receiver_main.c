#include <errno.h>
#include <linux/can/raw.h>
#include <stdio.h>
#include <unistd.h>

#include "log_frame.h"
#include "log_receiver.h"
#include "log_receiver_stats.h"
#include "log_sink.h"
#include "socketcan_raw.h"

int main(int argc, char **argv)
{
    const struct can_filter filter = {
        .can_id = ECU_ULOG_CAN_ID,
        .can_mask = CAN_SFF_MASK,
    };
    LogReceiver_t receiver;
    LogReceiverStats_t stats = {0};
    LogSink_t stdout_sink = {0};
    int fd;

    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s <ifname>\n", argv[0]);
        return 2;
    }

    fd = socketcan_raw_open_filtered(argv[1], &filter, 1u);
    if (fd < 0) {
        perror("socketcan_raw_open_filtered");
        return 1;
    }

    log_receiver_init(&receiver);
    log_sink_init(&stdout_sink, stdout);
    for (;;) {
        struct can_frame raw_frame = {0};
        int recv_result = socketcan_raw_recv(fd, &raw_frame, -1);

        if (recv_result > 0) {
            EcuUlogFrame_t frame = {0};
            LogRecordView_t record = {0};
            LogFrameDecodeResult_t frame_result =
                log_frame_decode(&raw_frame, &frame);

            if (frame_result == LOG_FRAME_OTHER_CAN_ID) {
                continue;
            }
            if (frame_result == LOG_FRAME_MALFORMED) {
                stats.rejected_frames++;
                if (log_receiver_abort(&receiver)) {
                    stats.abandoned_records++;
                }
                (void)fprintf(stderr,
                              "discarded malformed ULog frame: abandoned_records=%u rejected_frames=%u\n",
                              stats.abandoned_records,
                              stats.rejected_frames);
                continue;
            }

            {
                LogReceiverResult_t result = log_receiver_accept(
                    &receiver,
                    &frame,
                    &record);

                if (result.abandoned_record) {
                    stats.abandoned_records++;
                }
                if (result.status == LOG_RECEIVER_REJECTED) {
                    stats.rejected_frames++;
                    (void)fprintf(stderr,
                                  "discarded malformed ULog frame: abandoned_records=%u rejected_frames=%u\n",
                                  stats.abandoned_records,
                                  stats.rejected_frames);
                    continue;
                }
                if (result.status == LOG_RECEIVER_COMPLETE) {
                    if (log_sink_write(&stdout_sink, &record) != 0) {
                        (void)fprintf(stderr,
                                      "failed to write completed ULog record\n");
                    }
                }
            }
            continue;
        }
        if (recv_result == SOCKETCAN_RAW_TIMEOUT)
        {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        perror("socketcan_raw_recv");
        (void)close(fd);
        return 1;
    }
}
