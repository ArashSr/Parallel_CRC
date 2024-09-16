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

struct CRC_Config {
    int width;
    uint32_t polynomial;
    uint32_t init_val;
    bool refInput;
    bool refOutput;
    uint32_t xor_out;
    int chunkSize;    
};

struct FPGA_Interface {
    cl::CommandQueue queue;
    cl::Buffer input_buffer;
    int64_t buffer_size;
    cl::Buffer table_buffer;
    cl::Buffer output_buffer;
    cl::Kernel kernel;
};

void loadConfig(std::string filename, CRC_Config &config)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        exit(1);
    }
    file >> config.width;
    file >> std::hex >> config.polynomial;
    file >> std::hex >> config.init_val;
    file >> config.refInput;
    file >> config.refOutput;
    file >> std::hex >> config.xor_out;
    file >> config.chunkSize;   
    file.close();
}

void printData(const std::vector<uint8_t> & data) {
    for (uint8_t byte : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " "; 
    }
    std::cout << std::endl;
}



void update_tables(FPGA_Interface fpga, CRC_Config &current_config, CRC_Config& next_config, bool forceChange) {
    cl_int err;
    if (forceChange || current_config.polynomial != next_config.polynomial || current_config.width != next_config.width) {
        std::vector<uint32_t> crcTables = generateParallelCRCTables(generateStandardCRCTable(next_config.polynomial, next_config.width), next_config.width);
        OCL_CHECK(err, err = fpga.queue.enqueueWriteBuffer(fpga.table_buffer, CL_TRUE, 0, 256 * 16 * sizeof(uint32_t), crcTables.data(), nullptr, nullptr));
        current_config = next_config;
    }   
}

FPGA_Interface program_device(std::string xclbinFile, int64_t buf_size_bytes) {
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
    OCL_CHECK(err, cl::Buffer buffer_out(context, CL_MEM_WRITE_ONLY, sizeof(unsigned int), nullptr, &err));
    OCL_CHECK(err, cl::Buffer buffer_table(context, CL_MEM_READ_ONLY, 256 * 16 * sizeof(uint32_t), nullptr, &err));

    FPGA_Interface progDevice;
    progDevice.queue = queue;
    progDevice.input_buffer = buffer_in;
    progDevice.output_buffer = buffer_out;
    progDevice.table_buffer = buffer_table;
    progDevice.kernel = kernel;  
    progDevice.buffer_size = buf_size_bytes;  
    return progDevice;
}

uint32_t processChunk(std::ifstream& inFile, std::ofstream& outfile, bool writeToFile, FPGA_Interface& device, CRC_Config& config, std::vector<unsigned char>& data_ptr, int size){
    uint32_t crcVal = config.init_val;
    cl_int err;
    auto start_time = std::chrono::high_resolution_clock::now();
    auto end_time = std::chrono::high_resolution_clock::now();
    auto fpga_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - end_time);
    while (size > device.buffer_size) {
        
        load_data_chunk(data_ptr,  inFile, device.buffer_size);
        if (writeToFile) {
            outfile.write(reinterpret_cast<const char*>(data_ptr.data()), data_ptr.size());
        }
        if (!config.refInput)
        {
            reflectInput(data_ptr, data_ptr.size());
        }
        OCL_CHECK(err, err = device.queue.enqueueWriteBuffer(device.input_buffer, CL_TRUE, 0, device.buffer_size, data_ptr.data(), nullptr, nullptr));
        
        // Set the Kernel Arguments
        OCL_CHECK(err, err = device.kernel.setArg(0, device.input_buffer));                                   // Input buffer
        OCL_CHECK(err, err = device.kernel.setArg(1, device.output_buffer));                                  // Output buffer
        OCL_CHECK(err, err = device.kernel.setArg(2, (static_cast<unsigned int>(data_ptr.size()/16)))); // Data size
        OCL_CHECK(err, err = device.kernel.setArg(3, device.table_buffer));                                // Tables
        OCL_CHECK(err, err = device.kernel.setArg(4, static_cast<uint32_t>(config.width)));             // crc_size
        OCL_CHECK(err, err = device.kernel.setArg(5, static_cast<uint32_t>(crcVal))); // init_value     
        start_time = std::chrono::high_resolution_clock::now();
        // Begin Calculation on Data Chunk
        OCL_CHECK(err, err = device.queue.enqueueTask(device.kernel));

        // Synchronize (wait for kernel to finish)
        OCL_CHECK(err, err = device.queue.finish());
        end_time = std::chrono::high_resolution_clock::now();
        fpga_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
       

        OCL_CHECK(err, err = device.queue.enqueueReadBuffer(device.output_buffer, CL_TRUE, 0, sizeof(unsigned int), &crcVal));
        size -= device.buffer_size;
    }

    int trimmed = size - (size %16);
    load_data_chunk(data_ptr,  inFile, trimmed);
    if (writeToFile) {
            outfile.write(reinterpret_cast<const char*>(data_ptr.data()), data_ptr.size());
        }
    //std::cout << "Check 1 - Size: " << data_ptr.size() << std::endl;
    if (!config.refInput)
    {
        reflectInput(data_ptr, data_ptr.size());
    }
    OCL_CHECK(err, err = device.queue.enqueueWriteBuffer(device.input_buffer, CL_TRUE, 0, data_ptr.size(), data_ptr.data(), nullptr, nullptr));
    //std::cout << "Check 2" << std::endl;
    // Set the Kernel Arguments
    OCL_CHECK(err, err = device.kernel.setArg(0, device.input_buffer));                                   // Input buffer
    OCL_CHECK(err, err = device.kernel.setArg(1, device.output_buffer));                                  // Output buffer
    OCL_CHECK(err, err = device.kernel.setArg(2, (static_cast<unsigned int>(data_ptr.size()/16)))); // Data size
    OCL_CHECK(err, err = device.kernel.setArg(3, device.table_buffer));                                // Tables
    OCL_CHECK(err, err = device.kernel.setArg(4, static_cast<uint32_t>(config.width)));             // crc_size
    OCL_CHECK(err, err = device.kernel.setArg(5, static_cast<uint32_t>(crcVal))); // init_value     
    start_time = std::chrono::high_resolution_clock::now();
    // Begin Calculation on Data Chunk
    OCL_CHECK(err, err = device.queue.enqueueTask(device.kernel));

    // Synchronize (wait for kernel to finish)
    OCL_CHECK(err, err = device.queue.finish());
    end_time = std::chrono::high_resolution_clock::now();
    fpga_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "Kernel time: " << std::dec << fpga_time.count() << std::endl;

    OCL_CHECK(err, err = device.queue.enqueueReadBuffer(device.output_buffer, CL_TRUE, 0, sizeof(unsigned int), &crcVal));
    size -= trimmed;
    load_data_chunk(data_ptr,  inFile, size);
    if (writeToFile) {
            outfile.write(reinterpret_cast<const char*>(data_ptr.data()), data_ptr.size());
        }
        if (!config.refInput)
        {
            reflectInput(data_ptr, data_ptr.size());
        }    
    uint32_t result = standardCompute(data_ptr, generateStandardCRCTable(config.polynomial, config.width), crcVal, config.xor_out, config.width);
    if (!config.refOutput)
        {
            result = reverseBits(result, config.width);
        }
    return result;
            
            
}

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
    FPGA_Interface device = program_device(xclbinFile, buf_size_bytes);

    CRC_Config activeConfig;
    CRC_Config nextConfig;

    bool firstPacket = true;    

    // FPGA TOP LOOP
    std::string rerun = "yes";
    while (rerun == "yes")
    {
        std::cout << "Enter Config File Name: ";
        std::string filename;
        std::cin >> filename;

        std::cout << "Enter Input File Name: ";
        std::string input_filename;
        std::cin >> input_filename;

        std::cout << "Enter Output File Name: ";
        std::string output_fileName;
        std::cin >> output_fileName;        

        std::ifstream input_file(input_filename, std::ios::ate | std::ios::binary);
        if (!input_file.is_open())
        {
            std::cerr << "ERROR: Could not open file " << input_filename << std::endl;
            return EXIT_FAILURE;
        }
        long int file_size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);

        
        loadConfig(filename, nextConfig);
        std::cout << "Polynomial: " << std::hex << nextConfig.polynomial << std::endl;
         nextConfig.polynomial = reverseBits(nextConfig.polynomial, nextConfig.width);
        update_tables(device, activeConfig, nextConfig, firstPacket);
        // Tables must be forced to be generated on tehe first packet
        firstPacket = false;
        std::cout << "Updated Tables..." << std::endl;
        std::cout << "ChunkSize: " << activeConfig.chunkSize << std::endl;
        std::vector<unsigned char> input_data(activeConfig.chunkSize, 0);
        
        std::vector<unsigned char> leftover_data;

        std::ofstream out_file(output_fileName, std::ios::binary);
        auto start_time = std::chrono::high_resolution_clock::now();
        // Generate CRC values to be sent across the network
        int chunkCount = 0;
        while (file_size > activeConfig.chunkSize) {
            uint32_t result = processChunk(input_file, out_file, false, device, activeConfig, input_data, activeConfig.chunkSize);
            //std::cout << std::hex << result << std::endl;
            //out_file.write(reinterpret_cast<const char*>(&result), sizeof(uint32_t));
            chunkCount++;
            file_size -= activeConfig.chunkSize;
        }
        if (file_size > 0) {
            uint32_t result = processChunk(input_file, out_file, false, device, activeConfig, input_data, file_size);
            //std::cout << std::hex << result << std::endl;
            //out_file.write(reinterpret_cast<const char*>(&result), sizeof(uint32_t));
            
            chunkCount++;
            file_size = 0;
        }
        
        
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto fpga_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        input_file.close();
        out_file.close();
        
        std::cout << "Time to generate sender CRC Values: " << std::dec << fpga_time.count() << "ms" <<std::endl;
        std::cout << "Number of Chunks: " << chunkCount << std::endl;
        start_time = std::chrono::high_resolution_clock::now();
        input_file.open(output_fileName, std::ios::ate | std::ios::binary);
        file_size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);
        chunkCount = 0;

        // Check Recieved Data
    /*
        while (file_size > activeConfig.chunkSize) {
            uint32_t result = processChunk(input_file, out_file, false, device, activeConfig, input_data, activeConfig.chunkSize);
            //std::cout << std::hex << result << std::endl;
            uint32_t expected;
            input_file.read(reinterpret_cast<char*>(&expected), sizeof(uint32_t));
            if (result != expected) {
                std::cout << "Value does not match expected! (Chunk: " << chunkCount << ")" << std::endl;
            }
            chunkCount++;
            
            file_size -= activeConfig.chunkSize + 4;
        }
        if (file_size > 0) {
            uint32_t result = processChunk(input_file, out_file, false, device, activeConfig, input_data, file_size - 4);
            //std::cout << std::hex << result << std::endl;
            out_file.write(reinterpret_cast<const char*>(&result), sizeof(uint32_t));
            uint32_t expected;
            input_file.read(reinterpret_cast<char*>(&expected), sizeof(uint32_t));
            if (result != expected) {
                std::cout << "Value does not match expected! (Chunk: " << chunkCount << ")" << std::endl;
            }
            chunkCount++;
            
            file_size = 0;
        }
*/
        end_time = std::chrono::high_resolution_clock::now();
        fpga_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        std::cout << "Time to perform reciever side CRC: " << std::dec << fpga_time.count() << "ms" <<std::endl;

        auto standard_start = std::chrono::high_resolution_clock::now();
        std::ifstream std_file(input_filename, std::ios::ate | std::ios::binary);
        
        file_size = std_file.tellg();
        //std::cout << "File Size: " << std::dec << file_size << std::endl;
        std_file.seekg(0, std::ios::beg);
        uint32_t crcVal = activeConfig.init_val;
        while (file_size > activeConfig.chunkSize) {
            load_data_chunk(input_data, std_file, activeConfig.chunkSize);
            if (!activeConfig.refInput)
            {
                reflectInput(input_data, input_data.size());
            }
            uint32_t result = standardCompute(input_data, generateStandardCRCTable(activeConfig.polynomial, activeConfig.width), crcVal, activeConfig.xor_out, activeConfig.width);
            if (!activeConfig.refOutput)
        {
            result = reverseBits(result, activeConfig.width);
        }
            file_size -= activeConfig.chunkSize;
            //std::cout << std::hex << result << std::endl; 

        }
        load_data_chunk(input_data, std_file, file_size);
            if (!activeConfig.refInput)
            {
                reflectInput(input_data, input_data.size());
            }
            uint32_t result = standardCompute(input_data, generateStandardCRCTable(activeConfig.polynomial, activeConfig.width), crcVal, activeConfig.xor_out, activeConfig.width);
            file_size -= activeConfig.chunkSize;
            if (!activeConfig.refOutput)
        {
            result = reverseBits(result, activeConfig.width);
        }
            //std::cout << std::hex << result << std::endl;
        
        auto standard_end = std::chrono::high_resolution_clock::now();
        auto standard_time = std::chrono::duration_cast<std::chrono::milliseconds>(standard_end - standard_start);
        
        std::cout << "Time for Standard Calculation: " << std::dec << standard_time.count() << "ms" << std::endl;
        std_file.close();

        // CHANGE: Add Loop logic to calculate more packets
        rerun = "no";
    }

    return 0;
}
