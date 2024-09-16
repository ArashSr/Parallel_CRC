#include <cstdint>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "cmdlineparser.h"
#include "xcl2.hpp"
#include <unistd.h>
#include <string>
#include <chrono>

#define DATA_SIZE 32
#define BLOCK_SIZE 16
#define TABLE_SIZE 256

uint32_t maskCRC(uint32_t value, int width)
{
    if (width == 32)
    {
        return value;
    }
    return value & ((1u << width) - 1);
}

uint32_t reverseBits(uint32_t value, int width)
{
    uint32_t reversed = 0;
    for (int i = 0; i < width; ++i)
    {
        reversed = (reversed << 1) | (value & 1);
        value >>= 1;
    }
    return maskCRC(reversed, width);
}

uint32_t *generateStandardCRCTable(uint32_t polynomial, int width)
{
    uint32_t *crcTable = new uint32_t[256];
    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ polynomial;
            }
            else
            {
                crc = crc >> 1;
            }
        }
        crcTable[i] = maskCRC(crc, width);
    }
    return crcTable;
}

uint32_t standardCompute(const std::vector<uint8_t> &data, uint32_t *crcTable, uint32_t crc_initial, uint32_t crc_final, int width)
{
    uint32_t crc = maskCRC(crc_initial, width);
    for (uint8_t byte : data)
    {
        crc = (crc >> 8) ^ crcTable[(crc ^ byte) & 0xFF];
        crc = maskCRC(crc, width);
    }
    return maskCRC(crc ^ crc_final, width);
}

std::vector<uint32_t> generateParallelCRCTables(uint32_t *standardTable, int width)
{
    std::vector<uint32_t> crcTables(16 * 256, 0);

    for (int tableIndex = 0; tableIndex < 16; ++tableIndex)
    {
        for (int byteValue = 0; byteValue < 256; ++byteValue)
        {
            std::vector<uint8_t> data(16, 0);
            data[tableIndex] = static_cast<uint8_t>(byteValue & 0xFF);
            uint32_t crc = standardCompute(data, standardTable, 0xFFFFFFFF, 0xFFFFFFFF, width);
            crcTables[(256 * (15 - tableIndex)) + byteValue] = maskCRC(crc, width);
        }
    }
    return crcTables;
}

void reflectInput(std::vector<uint8_t> &data, int size)
{
    for (int i = 0; i < size; ++i)
    {
        data[i] = (reverseBits(data[i], 8) & 0xFF);
    }
}

void load_data_chunk(std::vector<unsigned char> &data, std::ifstream &inputfile, size_t size)
{
    data.clear();
    data.resize(size);
    inputfile.read(reinterpret_cast<char *>(&data[0]),size);
}

void loadConfig(std::string filename, uint32_t &polynomial, uint32_t &init_val, uint32_t &xor_out, bool &refInput, bool &refOutput, int &crcWidth, int& chunkSize)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        exit(1);
    }
    file >> crcWidth;
    file >> std::hex >> polynomial;
    file >> std::hex >> init_val;
    file >> refInput;
    file >> refOutput;
    file >> std::hex >> xor_out;
    file >> chunkSize;
}

void printData(const std::vector<uint8_t> & data) {
for (uint8_t byte : data) {
std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " "; 
}
std::cout << std::endl;}

int main(int argc, char **argv)
{

    // Parse command line arguments
    sda::utils::CmdLineParser parser;

    parser.addSwitch("--xclbin_file", "-x", "Binary file string", "");
    parser.addSwitch("--frequency", "-f", "Operating frequency, in MHz", "300");
    parser.addSwitch("--buf_size_mb", "-m", "Test buffer size, in MB", "16");
    parser.addSwitch("--buf_size_kb", "-k", "Test buffer size, in KB", "0");
    parser.parse(argc, argv);

    std::string xclbinFile = parser.value("xclbin_file");

    float frequency = stof(parser.value("frequency"));
    int64_t buf_size_mb = stoi(parser.value("buf_size_mb"));
    int64_t buf_size_kb = stoi(parser.value("buf_size_kb"));

    if (xclbinFile.empty())
    {
        parser.printHelp();
        return EXIT_FAILURE;
    }
    if (access(xclbinFile.c_str(), R_OK) != 0)
    {
        std::cerr << "ERROR: " << xclbinFile << " file not found" << std::endl;
        parser.printHelp();
        return EXIT_FAILURE;
    }

    if (buf_size_kb == 0)
    {
        buf_size_kb = buf_size_mb * 1024;
    }
    int64_t buf_size_bytes = buf_size_kb * 1024;
    buf_size_bytes -= buf_size_bytes % 16;

    // Setup Hardware

    cl_int err;
    cl::Context context;
    cl::CommandQueue queue;
    cl::Kernel kernel;

    auto devices = xcl::get_xil_devices();
    auto device = devices[0];

    auto fileBuf = xcl::read_binary_file(xclbinFile);
    cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
    bool valid_device = false;
    for (unsigned int i = 0; i < devices.size(); i++)
    {
        auto device = devices[i];
        OCL_CHECK(err, context = cl::Context(device, nullptr, nullptr, nullptr, &err));
        OCL_CHECK(err, queue = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err));

        std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
        cl::Program program(context, {device}, bins, nullptr, &err);
        if (err != CL_SUCCESS)
        {
            std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
        }
        else
        {
            std::cout << "Device[" << i << "]: program successful!\n";
            OCL_CHECK(err, kernel = cl::Kernel(program, "calculate_crc", &err));
            valid_device = true;
            break;
        }
    }
    if (!valid_device)
    {
        std::cerr << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }

    // Create Buffers
    OCL_CHECK(err, cl::Buffer buffer_in(context, CL_MEM_READ_ONLY, buf_size_bytes, nullptr, &err));
    OCL_CHECK(err, cl::Buffer buffer_out(context, CL_MEM_WRITE_ONLY, buf_size_bytes, nullptr, &err));
    OCL_CHECK(err, cl::Buffer buffer_table(context, CL_MEM_READ_ONLY, 256 * 16 * sizeof(uint32_t), nullptr, &err));

    uint32_t polynomial = 0x1021;
    uint32_t init_val = 0x0;
    uint32_t xor_out = 0x0;
    bool refInput = false;
    bool refOutput = false;
    int crcWidth = 16;
    int chunkSize = 65536;
    unsigned long loopCount = 5;
    unsigned long counter = 0;

    // FPGA TOP LOOP
    std::string rerun = "yes";
    while (rerun == "yes")
    {
        std::cout << "Enter Config File Name: ";
        std::string filename;
        std::cin >> filename;

        loadConfig(filename, polynomial, init_val, xor_out, refInput, refOutput, crcWidth, chunkSize);
        int dataSize = buf_size_bytes - (buf_size_bytes % chunkSize);
        int numChunks = dataSize / chunkSize;

        std::cout << "Enter Input File Name: ";
        std::string input_filename;
        std::cin >> input_filename;

        std::ifstream input_file(input_filename, std::ios::ate | std::ios::binary);
        if (!input_file.is_open())
        {
            std::cerr << "ERROR: Could not open file " << input_filename << std::endl;
            return EXIT_FAILURE;
        }
        long int file_size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);

        loopCount = file_size / buf_size_bytes;

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<unsigned char> input_data_1(dataSize, 0);
        std::vector<unsigned char> input_data_2(dataSize, 0);
        std::vector<uint32_t> output_data(dataSize, 0);
        std::vector<unsigned char> *data_ptr = &input_data_1;
        std::vector<unsigned char> leftover_data;

        auto start_check = std::chrono::high_resolution_clock::now();
        uint32_t *standardTable = generateStandardCRCTable(reverseBits(polynomial, crcWidth), crcWidth);

        std::vector<uint32_t> lookup_table = generateParallelCRCTables(standardTable, crcWidth);
        auto end_check = std::chrono::high_resolution_clock::now();
        auto table_gen_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();
        if (lookup_table.size() != 256 * 16)
        {
            std::cerr << "ERROR: Lookup table must contain 256 entries!" << std::endl;
            return EXIT_FAILURE;
        }
        start_check = std::chrono::high_resolution_clock::now();
        if (file_size > dataSize) {
            load_data_chunk(*data_ptr,  input_file, dataSize);
            file_size -= buf_size_bytes;
        } else {
            load_data_chunk(*data_ptr, input_file, file_size - (file_size % 16));
            file_size = file_size % 16; 
        }
        end_check = std::chrono::high_resolution_clock::now();
        auto load_data_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();
        // Reflect Input
        start_check = std::chrono::high_resolution_clock::now();
        if (!refInput)
        {
            reflectInput(*data_ptr, data_ptr->size());
        }
        end_check = std::chrono::high_resolution_clock::now();
        auto reflect_input_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();

        
        // Write the lookup table to the device
        start_check = std::chrono::high_resolution_clock::now();
        OCL_CHECK(err, err = queue.enqueueWriteBuffer(buffer_table, CL_TRUE, 0, 256 * 16 * sizeof(uint32_t), lookup_table.data(), nullptr, nullptr));
        end_check = std::chrono::high_resolution_clock::now();

        auto table_buffer_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();   

        auto data_buffer_write_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_check - end_check).count();
        auto kernel_arg_set_time = data_buffer_write_time;
        auto kernel_run_time = data_buffer_write_time;
        auto output_read_time = data_buffer_write_time;      
         unsigned int result = init_val;
         polynomial = reverseBits(polynomial, crcWidth);
         std::cout << "Polynomial: " << std::hex << polynomial << std::endl;
        bool firstLoop = true;
        file_size += 16;        
        while (file_size >= 16)
        {
            if(firstLoop) {
                file_size -= 16;
                firstLoop = false;            
            }
            start_check = std::chrono::high_resolution_clock::now();
            // Write the input data to the device
            OCL_CHECK(err, err = queue.enqueueWriteBuffer(buffer_in, CL_TRUE, 0, buf_size_bytes, data_ptr->data(), nullptr, nullptr));
            
            end_check = std::chrono::high_resolution_clock::now();
            data_buffer_write_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();
            // Set the Kernel Arguments
            OCL_CHECK(err, err = kernel.setArg(0, buffer_in));                                   // Input buffer
            OCL_CHECK(err, err = kernel.setArg(1, buffer_out));                                  // Output buffer
            OCL_CHECK(err, err = kernel.setArg(2, buffer_table));                                // Tables
            OCL_CHECK(err, err = kernel.setArg(3, (static_cast<unsigned int>(numChunks))));      // Number of Chunks
            OCL_CHECK(err, err = kernel.setArg(4, (static_cast<unsigned int>(chunkSize/16))));   // Chunk Size (in blocks)
            OCL_CHECK(err, err = kernel.setArg(5, static_cast<uint32_t>(crcWidth)));             // crc_size

            OCL_CHECK(err, err = kernel.setArg(6, static_cast<uint32_t>(result)));               // init_value

            start_check = std::chrono::high_resolution_clock::now();         
            kernel_arg_set_time += std::chrono::duration_cast<std::chrono::milliseconds>(start_check - end_check).count();          
               
            // Begin Calculation on Data Chunk
            OCL_CHECK(err, err = queue.enqueueTask(kernel));

            // Synchronize (wait for kernel to finish)
            OCL_CHECK(err, err = queue.finish());
            end_check = std::chrono::high_resolution_clock::now();
            kernel_run_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();            
            // Load Next Data Chunk
            data_ptr = (data_ptr == &input_data_1) ? &input_data_2 : &input_data_1;
            if (file_size > buf_size_bytes) {
                load_data_chunk(*data_ptr,  input_file, buf_size_bytes);
                file_size -= buf_size_bytes;
            } else {
                load_data_chunk(*data_ptr, input_file, file_size - (file_size % 16));
                file_size = (file_size % 16) ;    
                if (data_ptr->size() > 0) {
                    file_size  += 16;
                    firstLoop = true;              
                }
            }
            start_check = std::chrono::high_resolution_clock::now();
            load_data_time += std::chrono::duration_cast<std::chrono::milliseconds>(start_check - end_check).count();

            if (!refInput)
            {
                reflectInput(*data_ptr, data_ptr->size());
            }

            end_check = std::chrono::high_resolution_clock::now();
            reflect_input_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_check - start_check).count();
            // Read the result
            OCL_CHECK(err, err = queue.enqueueReadBuffer(buffer_out, CL_TRUE, 0, sizeof(unsigned int), reinterpret_cast<uint32_t>(&output_data.data()[0])));
            start_check = std::chrono::high_resolution_clock::now(); 
            output_read_time += std::chrono::duration_cast<std::chrono::milliseconds>(start_check - end_check).count();
            counter++;
        }
        start_check = std::chrono::high_resolution_clock::now(); 
        load_data_chunk(*data_ptr, input_file, file_size);
                
        if (!refInput)
            {
                reflectInput(*data_ptr, data_ptr->size());
            }
                    printData(*data_ptr);
        uint32_t finalVal = standardCompute(*data_ptr, standardTable, result, xor_out, crcWidth);
        
        if (!refOutput)
        {
            finalVal = reverseBits(finalVal, crcWidth);
        }
        input_file.close();
        auto stop_time = std::chrono::high_resolution_clock::now();
        auto final_calc_time = std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_check);
        auto calc_time = std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time);
        std::cout << "CRC32: 0x" << std::hex << finalVal << std::endl;
        std::cout << "Table Gen Time: " << std::dec << table_gen_time << std::endl;
        std::cout << "Table Write Time: " << std::dec << table_buffer_time << std::endl;   
        std::cout << "Data Load Time: " << std::dec << load_data_time << std::endl;  
        std::cout << "Data Reflect Time: " << std::dec << reflect_input_time << std::endl;     
        std::cout << "Data Buffer Time: "  << std::dec << data_buffer_write_time << std::endl;
        std::cout << "Kernel Arg Set Time: " << std::dec << kernel_arg_set_time << std::endl;   
        std::cout << "Kernel Run Time: " << std::dec << kernel_run_time << std::endl;  
        std::cout << "Output Read Time: " << std::dec << output_read_time << std::endl;            
        std::cout << "Total Compute time: " << std::dec << calc_time.count() << std::endl;
        auto unaccounted_time = calc_time.count() - (table_gen_time + table_buffer_time + load_data_time + reflect_input_time + data_buffer_write_time + kernel_arg_set_time + kernel_run_time + output_read_time);        
        std::cout << "Extra Time (missed in subcalculations) : " << std::dec << unaccounted_time << std::endl;
        std::cout << "Performing Standard Calculation..." << std::endl;
        auto standard_start = std::chrono::high_resolution_clock::now();
        std::ifstream std_file(input_filename, std::ios::ate | std::ios::binary);
        
        file_size = std_file.tellg();
        std::cout << "File Size: " << std::dec << file_size << std::endl;
        std_file.seekg(0, std::ios::beg);
        finalVal = init_val;
        while (file_size > buf_size_bytes) {
            load_data_chunk(*data_ptr, std_file, buf_size_bytes);
            if (!refInput)
            {
                reflectInput(*data_ptr, data_ptr->size());
            }
            finalVal = standardCompute(*data_ptr, standardTable, finalVal, 0, crcWidth);
            file_size -= buf_size_bytes;

        }
        load_data_chunk(*data_ptr, std_file, file_size);
        if (!refInput)
        {
            reflectInput(*data_ptr, data_ptr->size());
        }
        finalVal = standardCompute(*data_ptr, standardTable, finalVal, xor_out, crcWidth);
        if (!refOutput)
        {
            finalVal = reverseBits(finalVal, crcWidth);
        }
        auto standard_end = std::chrono::high_resolution_clock::now();
        auto standard_time = std::chrono::duration_cast<std::chrono::milliseconds>(standard_end - standard_start);
        std::cout << "CRC32: 0x" << std::hex << finalVal << std::endl;
        std::cout << "Time: " << std::dec << standard_time.count() << std::endl;
        std_file.close();

        // CHANGE: Add Loop logic to calculate more packets
        rerun = "no";
    }

    return 0;
}
