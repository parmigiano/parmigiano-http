#ifndef S3_h
#define S3_h

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char* endpoint;
    char* bucket;
    char* access_key;
    char* secret_key;
    char* region;
} s3_config_t;

char* s3_upload_file_pub(FILE* f, const char* filename, char* content_type, const char* key, s3_config_t* cfg);

char* s3_upload_file_prv(FILE* f, const char* filename, char* content_type, const char* key, s3_config_t* cfg);

int s3_delete_file(const char* url, s3_config_t* cfg);

#endif