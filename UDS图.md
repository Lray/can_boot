


linux:uds_enter_session    SESSION_PROGRAMMING  0x10 02
mcu: 0x10 session p2_h p2_l p2*_h p2*_l
0x7F sid nrc

linux:uds_security_request_seed  0x27 01
mcu:0x27 level seed
uds_security_send_token     0x27 02
0x27 

uds_erase_memory 0x31 01 FF 00

uds_erase_memory_results 0x32 03 FF 00


uds_request_download 0x34  00 44

uds_transfer_data  0x36 block_sequence

uds_request_transfer_exit   0x37

 uds_mcu_reset_hard 0x11  0x01