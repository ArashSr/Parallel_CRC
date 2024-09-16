

#include <hls_stream.h>
#include <ap_int.h>
#include <hls_vector.h>
#include <cstdint>
#include <stddef.h>


#define WIDTH 32
#define TABLE_SIZE 256
#define BLOCK_SIZE 16
#define DATA_SIZE 32

typedef hls::stream<ap_uint<8>> bStream;


void read_input(const unsigned char* in, 
                bStream& byte0, bStream& byte1, bStream& byte2, bStream& byte3, 
                bStream& byte4, bStream& byte5, bStream& byte6, bStream& byte7, 
                bStream& byte8, bStream& byte9, bStream& byte10, bStream& byte11, 
                bStream& byte12, bStream& byte13, bStream& byte14, bStream& byte15, 
                const unsigned int numChunks, const unsigned int chunkSize) {
    mem_rd:
    for (int i = 0; i < numChunks* chunkSize; i++) {
        #pragma HLS PIPELINE II=1
        ap_uint<128> data = *(reinterpret_cast<const ap_uint<128>*>(&in[i * 16]));

        
        byte0 << data(7, 0);
        byte1 << data(15, 8);
        byte2 << data(23, 16);
        byte3 << data(31, 24);
        byte4 << data(39, 32);
        byte5 << data(47, 40);
        byte6 << data(55, 48);
        byte7 << data(63, 56);
        byte8 << data(71, 64);
        byte9 << data(79, 72);
        byte10 << data(87, 80);
        byte11 << data(95, 88);
        byte12 << data(103, 96);
        byte13 << data(111, 104);
        byte14 << data(119, 112);
        byte15 << data(127, 120);
    }
}





static void process_blocks(uint32_t crcTables[BLOCK_SIZE][TABLE_SIZE], 
                bStream& byte0, bStream& byte1, bStream& byte2, bStream& byte3, 
                bStream& byte4, bStream& byte5, bStream& byte6, bStream& byte7, 
                bStream& byte8, bStream& byte9, bStream& byte10, bStream& byte11, 
                bStream& byte12, bStream& byte13, bStream& byte14, bStream& byte15, 
                const uint32_t crc_size, const uint32_t init_value, hls::stream<ap_uint<32>>& outStream, int numChunks, int chunkSize) {
   uint32_t mask = 0xFFFFFFFF;
   if (crc_size < 32) {
       mask = (1U << crc_size) - 1;
   }
   uint32_t crc = init_value;

   ap_uint<32> table0[TABLE_SIZE];
   ap_uint<32> table1[TABLE_SIZE];
   ap_uint<32> table2[TABLE_SIZE];
   ap_uint<32> table3[TABLE_SIZE];

   ap_uint<32> table4[TABLE_SIZE];
   ap_uint<32> table5[TABLE_SIZE];
   ap_uint<32> table6[TABLE_SIZE];
   ap_uint<32> table7[TABLE_SIZE];

   ap_uint<32> table8[TABLE_SIZE];
   ap_uint<32> table9[TABLE_SIZE];
   ap_uint<32> table10[TABLE_SIZE];
   ap_uint<32> table11[TABLE_SIZE];

   ap_uint<32> table12[TABLE_SIZE];
   ap_uint<32> table13[TABLE_SIZE];
   ap_uint<32> table14[TABLE_SIZE];
   ap_uint<32> table15[TABLE_SIZE];

   for (int i = 0 ; i < TABLE_SIZE; i++) {
       #pragma HLS UNROLL
       table0[i] = crcTables[15][i];
       table1[i] = crcTables[14][i];
       table2[i] = crcTables[13][i];
       table3[i] = crcTables[12][i];

       table4[i] = crcTables[11][i];
       table5[i] = crcTables[10][i];
       table6[i] = crcTables[9][i];
       table7[i] = crcTables[8][i];

       table8[i] = crcTables[7][i];
       table9[i] = crcTables[6][i];
       table10[i] = crcTables[5][i];
       table11[i] = crcTables[4][i];

       table12[i] = crcTables[3][i];
       table13[i] = crcTables[2][i];
       table14[i] = crcTables[1][i];
       table15[i] = crcTables[0][i];
   }

   ap_uint<32> tmp0;
   ap_uint<32> tmp1;
   ap_uint<32> tmp2;
   ap_uint<32> tmp3;

   ap_uint<32> tmp4;
   ap_uint<32> tmp5;
   ap_uint<32> tmp6;
   ap_uint<32> tmp7;

   ap_uint<32> tmp8;
   ap_uint<32> tmp9;
   ap_uint<32> tmp10;
   ap_uint<32> tmp11;

   ap_uint<32> tmp12;
   ap_uint<32> tmp13;
   ap_uint<32> tmp14;
   ap_uint<32> tmp15;
   
   for (int blockInd = 0; blockInd < numChunks * chunkSize; blockInd++) {

#pragma HLS PIPELINE II=2
       tmp0 = table0[((crc & 0xFF) ^ byte0.read())];
       tmp1 = table1[(((crc >> 8) & 0xFF) ^ byte1.read())];
       tmp2 = table2[(((crc >> 16) & 0xFF) ^ byte2.read())];
       tmp3 = table3[(((crc >> 24) & 0xFF) ^ byte3.read())];

       tmp4 = table4[byte4.read()];
       tmp5 = table5[byte5.read()];
       tmp6 = table6[byte6.read()];
       tmp7 = table7[byte7.read()];

       tmp8 = table8[byte8.read()];
       tmp9 = table9[byte9.read()];
       tmp10 = table10[byte10.read()];
       tmp11 = table11[byte11.read()]; 

       tmp12 = table12[byte12.read()];
       tmp13 = table13[byte13.read()];
       tmp14 = table14[byte14.read()];
       tmp15 = table15[byte15.read()];
       
       tmp0 ^= tmp1;
       tmp2 ^= tmp3;
       tmp4 ^= tmp5;
       tmp6 ^= tmp7;
       tmp8 ^= tmp9;
       tmp10 ^= tmp11;
       tmp12 ^= tmp13;
       tmp14 ^= tmp15;
       

       tmp0 ^= tmp2;
       tmp4 ^= tmp6;
       tmp8 ^= tmp10;
       tmp12 ^= tmp14;

       

       tmp0 ^= tmp4;
       tmp8 ^= tmp12;

       crc = (tmp0 ^ tmp8) & mask;
       if (blockInd > 0 && ((blockInd % chunkSize) == 0)) {
           outStream << crc;           
       }
   }
   outStream << crc;
   

}

static void write_output(hls::stream<ap_uint<32>>& outStream, int numChunks, unsigned int* crc_out) {
    for (int i = 0; i < numChunks; i++) {
        #pragma HLS PIPELINE II=2
        crc_out[i] = static_cast<unsigned int>(outStream.read());                
    }
}






extern "C" {
void calculate_crc(const unsigned char* data_in, unsigned int* crc_out, const uint32_t* tables, const unsigned int numChunks, const unsigned int chunkSize, const uint32_t crc_size, const uint32_t init_value) {


   static bStream inByte0("Input Byte Stream 0");
   static bStream inByte1("Input Byte Stream 1");
   static bStream inByte2("Input Byte Stream 2");
   static bStream inByte3("Input Byte Stream 3");


   static bStream inByte4("Input Byte Stream 4");
   static bStream inByte5("Input Byte Stream 5");
   static bStream inByte6("Input Byte Stream 6");
   static bStream inByte7("Input Byte Stream 7");


   static bStream inByte8("Input Byte Stream 8");
   static bStream inByte9("Input Byte Stream 9");
   static bStream inByte10("Input Byte Stream 10");
   static bStream inByte11("Input Byte Stream 11");


   static bStream inByte12("Input Byte Stream 12");
   static bStream inByte13("Input Byte Stream 13");
   static bStream inByte14("Input Byte Stream 14");
   static bStream inByte15("Input Byte Stream 15");

   static hls::stream<ap_uint<32>> outStream("Output Value Stream");

    

   uint32_t crcTables[BLOCK_SIZE][TABLE_SIZE];
#pragma HLS INTERFACE m_axi port = data_in bundle = gmem0
#pragma HLS INTERFACE m_axi port = tables  bundle = gmem1
#pragma HLS INTERFACE m_axi port = crc_out bundle = gmem0




   // Initialize the CRC table
   for (int i = 0; i < BLOCK_SIZE; ++i) {
       for (int j = 0; j < TABLE_SIZE; ++j) {
#pragma HLS PIPELINE
           crcTables[i][j] = tables[(i*256 )+ j];
       }
   }


   #pragma HLS DATAFLOW
   read_input(data_in, inByte0, inByte1, inByte2, inByte3, inByte4, inByte5, inByte6, inByte7, inByte8, inByte9, inByte10, inByte11, inByte12, inByte13, inByte14, inByte15, numChunks, chunkSize);
   process_blocks(crcTables, inByte0, inByte1, inByte2, inByte3, inByte4, inByte5, inByte6, inByte7, inByte8, inByte9, inByte10, inByte11, inByte12, inByte13, inByte14, inByte15, crc_size, init_value, outStream, numChunks, chunkSize);
    write_output(outStream, numChunks, crc_out);

  
}
}

