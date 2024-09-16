#include <hls_stream.h>
#include <ap_int.h>
#include <hls_vector.h>
#include <cstdint>
#include <stddef.h>

#define WIDTH 32
#define TABLE_SIZE 256
#define BLOCK_SIZE 16
#define DATA_SIZE 32

static void read_input(const unsigned char* in, hls::stream<unsigned char>& dataStream, const unsigned int size) {
    mem_rd:
        for (int i = 0; i < size; i++) {
            dataStream << in[i];
        }
}

static void process_blocks(uint32_t polynomial, hls::stream<unsigned char>& dataStream, const uint32_t crc_size, unsigned int size, const uint32_t init_value, unsigned int* crc_out) {
    uint32_t mask = 0xFFFFFFFF;
    if (crc_size < 32) {
        mask = (1U << crc_size) - 1;
    }
    uint32_t crc = init_value;
    for (int i = 0; i < size; i++) {
#pragma HLS PIPELINE 
        uint8_t byte = dataStream.read();
        crc ^= byte;
        for (int j = 0; j < 8; j++) {
            crc = crc >> 1;
            if (crc & 1) {
                crc = crc ^ polynomial;
            } 
        }
        crc = crc & mask;
    }

    *crc_out = static_cast<unsigned int>(crc);
}



extern "C" {
void calculate_crc(const unsigned char* data_in, unsigned int* crc_out, const unsigned int size, const uint32_t polynomial, const uint32_t crc_size, const uint32_t init_value) {

    static hls::stream<unsigned char> dataStream("data_stream");

    uint32_t crcTables[BLOCK_SIZE][TABLE_SIZE];
#pragma HLS INTERFACE m_axi port = data_in bundle = gmem0

#pragma HLS INTERFACE m_axi port = crc_out bundle = gmem0



    #pragma HLS DATAFLOW
    read_input(data_in, dataStream, size);
    process_blocks(polynomial, dataStream, crc_size, size, init_value, crc_out);

    
}
}
