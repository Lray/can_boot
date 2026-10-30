#ifndef TA_API_H
#define TA_API_H

#define TOKEN_TA_UUID \
    { 0x724b12aa, 0x6e74, 0x4779, \
      { 0xbf, 0x3a, 0x15, 0x80, 0xa0, 0x76, 0xfe, 0xd3 } }

#define TOKEN_TA_CMD_GET_PUBLIC_KEY 1u
#define TOKEN_TA_CMD_ISSUE_TOKEN 2u

#define TOKEN_TA_PRIVATE_KEY_SIZE 32u
#define TOKEN_TA_PUBLIC_KEY_SIZE 64u

#endif
