/*
File    : read_peak_data.c
Author  : Sooyeon Kim
Date    : June 06, 2023
Update  : April 21, 2024
Description : Client program for communicating with an I4 Interrogator device.
Protocol    : TCP/IP

This program serves as a client to communicate with an I4 Interrogator device over a TCP/IP connection.
The protocol involves the following packet structure:

- Header Packet: Contains information about packet counter, sweeping type, trigger mode, data offset, data length, and timestamp.
- Payload Packet: Carries data related to peak values or time-stamped peak values.
- Error Packet: Indicates errors encountered during data transmission.
- Flag Packet: Holds sweep counter information.

Updates:
- Added functions for processing different types of payload packets (peak payload and time-stamped peak payload).
- Implemented error handling for receiving payload and error packets.
- Included a calibration function to calculate force based on wavelength measurements.
- Defined constants for initial wavelengths of FBGs on different channels and sensors.
*/


#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <stdint.h>
#include <Windows.h>
#include <time.h>
#include <inttypes.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 9931
#define SERVER_IP "10.100.51.16"

// packet size
#define HEADER_SIZE 16
#define ERROR_PAYLOAD_SIZE 8
#define PEAK_PAYLOAD_SIZE 8
#define TSPEAK_PAYLOAD_SIZE 12
#define FLAG_SIZE 8

// initial wavelength of FBGs [mm]
#define CHANNEL_1_SENSOR_1_WAVELENGTH 1534.63
#define CHANNEL_1_SENSOR_2_WAVELENGTH 1549.65
#define CHANNEL_2_SENSOR_1_WAVELENGTH 1534.63
#define CHANNEL_2_SENSOR_2_WAVELENGTH 1549.65
#define CHANNEL_3_SENSOR_1_WAVELENGTH 1534.63
#define CHANNEL_3_SENSOR_2_WAVELENGTH 1549.65
#define CHANNEL_4_SENSOR_1_WAVELENGTH 1534.63
#define CHANNEL_4_SENSOR_2_WAVELENGTH 1549.65

#pragma pack(1)
struct ts_peak_payload_t {
    uint32_t ts_peak_payload_LSB : 32;
    uint32_t ts_peak_payload_MSB : 32;
    uint32_t time_stamp : 32;
};
struct peak_payload_t {
    uint32_t peak_payload_LSB : 32;
    uint32_t peak_payload_MSB : 32;
};
struct error_payload_t {
    uint32_t error_id : 32;
    uint32_t error_description : 32;
};
struct I4PacketHeader {
    uint16_t info : 16; // packetCounter(12) + sweepingType(3) + triggerMode(1)
    uint16_t dataOffset : 16;
    uint32_t dataLength : 32;
    uint64_t timeStamp : 64;
};
struct I4PacketFlag {
    uint32_t sweep_counter : 32;
    uint32_t reserved : 32;
};
#pragma pack()


// function redefinition
typedef uint32_t peak_data_t[2];
typedef uint32_t ts_peak_data_t[3];

int processPacket_Header(char* buffer_header, int* sweep_type, int* DO, int* DL);
int processPacket_Payload(char* buffer_payload, int* channel, int* fiber, int* sensor, double* wavelenght);
int processPacket_tsPayload(char* buffer_payload, int* channel, int* fiber, int* sensor, double* wavelenght);
int processPacket_errorPayload(char* error_payload);
int calibrationForce(const double* initial_wavelength, const double* wavelength, double* force);

double wavelength(const uint32_t* const peak_data);
uint8_t channel_id(const uint32_t* const peak_data);
uint8_t fiber_id(const uint32_t* const peak_data);
uint8_t sensor_id(const uint32_t* const peak_data);
double time_stamp(const uint32_t* const peak_data);


int main() {

    double FBGs_info[4][1][2]; // channel - fibre - sensor
    FBGs_info[0][0][0] = CHANNEL_1_SENSOR_1_WAVELENGTH / 1e9;
    FBGs_info[0][0][1] = CHANNEL_1_SENSOR_2_WAVELENGTH / 1e9;
    FBGs_info[1][0][0] = CHANNEL_2_SENSOR_1_WAVELENGTH / 1e9;
    FBGs_info[1][0][1] = CHANNEL_2_SENSOR_2_WAVELENGTH / 1e9;
    FBGs_info[2][0][0] = CHANNEL_3_SENSOR_1_WAVELENGTH / 1e9;
    FBGs_info[2][0][1] = CHANNEL_3_SENSOR_2_WAVELENGTH / 1e9;
    FBGs_info[3][0][0] = CHANNEL_4_SENSOR_1_WAVELENGTH / 1e9;
    FBGs_info[3][0][1] = CHANNEL_4_SENSOR_2_WAVELENGTH / 1e9;


    /* Initialize winsock */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    SOCKET hSocket;
    hSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (hSocket == INVALID_SOCKET) {
        fprintf(stderr, "Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    SOCKADDR_IN tAddr;
    tAddr.sin_family = AF_INET;
    tAddr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &tAddr.sin_addr) <= 0) {
        fprintf(stderr, "inet_pton failed.\n");
        closesocket(hSocket);
        WSACleanup();
        return 1;
    }

    if (connect(hSocket, (SOCKADDR*)&tAddr, sizeof(tAddr)) == SOCKET_ERROR) {
        fprintf(stderr, "Connection failed.\n");
        closesocket(hSocket);
        WSACleanup();
        return 1;
    }


    while (1) {
        /* 1. Receiving header packet */
        char* buffer_header[HEADER_SIZE] = { 0 };
        int hbytesRead = recv(hSocket, buffer_header, HEADER_SIZE, 0);

       if (hbytesRead == SOCKET_ERROR) {
            perror("Receiving failed");
        }
        else if (hbytesRead == 0) {
            printf("Client disconnected\n");
        }
        if (hbytesRead != HEADER_SIZE) {
            perror("packet header size error");
        }

        int sweep_type, DO, DL; // offset for error handling
        processPacket_Header(buffer_header, &sweep_type, &DO, &DL);

        int channel=0, fiber=0, sensor=0;
        double wavelength=0, force=0;

        /* 2. Receiving payload packet */
        if (DO == 16) { // or 0X0010
            // receiving peak payload
            if (sweep_type == 0) { // peak
                char* buffer_payload[PEAK_PAYLOAD_SIZE] = { 0 };
                for (int i = 0; i < (DL / PEAK_PAYLOAD_SIZE); i++) {
                    int pbytesRead = recv(hSocket, buffer_payload, PEAK_PAYLOAD_SIZE, 0);
                    if (pbytesRead == SOCKET_ERROR) {
                        perror("Receiving failed");
                    }
                    else if (pbytesRead == 0) {
                        printf("Client disconnected\n");
                    }
                    processPacket_Payload(buffer_payload, &channel, &fiber, &sensor, &wavelength);
                    double initial_wavelength = FBGs_info[channel][fiber][sensor];
                    calibrationForce(&initial_wavelength, &wavelength, &force);
                }
            }
            // receiving time-stamped peak payload
            else if (sweep_type == 2) { // peak with timestamps
                char* buffer_payload[TSPEAK_PAYLOAD_SIZE] = { 0 };
                for (int i = 0; i < (DL / TSPEAK_PAYLOAD_SIZE); i++) {
                    int pbytesRead = recv(hSocket, buffer_payload, TSPEAK_PAYLOAD_SIZE, 0);
                    if (pbytesRead == SOCKET_ERROR) {
                        perror("Receiving failed");
                    }
                    else if (pbytesRead == 0) {
                        printf("Client disconnected\n");
                    }
                    processPacket_tsPayload(buffer_payload, &channel, &fiber, &sensor, &wavelength);
                    double initial_wavelength = FBGs_info[channel][fiber][sensor];
                    calibrationForce(&initial_wavelength, &wavelength, &force);
                    printf("Sensor#%u, ", sensor);
                    printf("Fiber#%u, ", fiber);
                    printf("Channel#%u\t", channel);
                    printf("Force:%.5f mN\n", force);
                }
            }
        }

        else { // if error exists..
            // receiving error payload
            char* error_payload[ERROR_PAYLOAD_SIZE] = { 0 };
            int ebytesRead = recv(hSocket, error_payload, ERROR_PAYLOAD_SIZE, 0);
            if (ebytesRead == SOCKET_ERROR) {
                perror("error receiving failed");
            }
            else if (ebytesRead == 0) {
                printf("Client disconnected\n");
            }
            processPacket_errorPayload(error_payload);

            // receiving peak payload
            if (sweep_type == 0) { // peak
                char* buffer_payload[PEAK_PAYLOAD_SIZE] = { 0 };
                for (int i = 0; i < (DL / PEAK_PAYLOAD_SIZE); i++) {
                    int pbytesRead = recv(hSocket, buffer_payload, PEAK_PAYLOAD_SIZE, 0);
                    if (pbytesRead == SOCKET_ERROR) {
                        perror("Receiving failed");
                    }
                    else if (pbytesRead == 0) {
                        printf("Client disconnected\n");
                    }
                    processPacket_Payload(buffer_payload, &channel, &fiber, &sensor, &wavelength);
                    double initial_wavelength = FBGs_info[channel][fiber][sensor];
                    calibrationForce(&initial_wavelength, &wavelength, &force);
                }
            }
            // receiving time-stamped peak payload
            else if (sweep_type == 2) { // peak with timestamps
                char* buffer_payload[TSPEAK_PAYLOAD_SIZE] = { 0 };
                for (int i = 0; i < (DL / TSPEAK_PAYLOAD_SIZE); i++) {
                    int pbytesRead = recv(hSocket, buffer_payload, TSPEAK_PAYLOAD_SIZE, 0);
                    if (pbytesRead == SOCKET_ERROR) {
                        perror("Receiving failed");
                    }
                    else if (pbytesRead == 0) {
                        printf("Client disconnected\n");
                    }
                    processPacket_tsPayload(buffer_payload, &channel, &fiber, &sensor, &wavelength);
                    double initial_wavelength = FBGs_info[channel][fiber][sensor];
                    calibrationForce(&initial_wavelength, &wavelength, &force);
                    printf("Sensor#%u, ", sensor);
                    printf("Fiber#%u, ", fiber);
                    printf("Channel#%u\t", channel);
                    printf("Force:%.5f mN\n", force);
                }
            }
            
        }


        /* 3. Receiving flag packet */
        char* buffer_flag[FLAG_SIZE] = { 0 };
        int fbytesRead = recv(hSocket, buffer_flag, FLAG_SIZE, 0);

        if (fbytesRead == SOCKET_ERROR) {
            perror("flag receiving failed");
        }
        else if (fbytesRead == 0) {
            printf("Client disconnected\n");
        }

        //free(buffer_header);
        //free(buffer_payload);
        //closesocket(hSocket);
        //WSACleanup();

        //break; // read 1st packet only

    }
    return 0;
}


int processPacket_Header(char* buffer_header, int* sweep_type, int* DO, int* DL) {

    struct I4PacketHeader* header = (struct I4PacketHeader*)(buffer_header);

    // Packet Header (bit field mask)
    uint16_t packetCounterMask = 0xFFF; // lower 12 bits
    uint16_t sweepingTypeMask = 0x7000; // middle 3 bits
    uint16_t triggerModeMask = 0x8000;  // top 1 bit

    uint16_t packetCounter = header->info & packetCounterMask;
    uint16_t sweepingType = (header->info & sweepingTypeMask) >> 12;
    uint16_t triggerMode = (header->info & triggerModeMask) >> 15;

    uint16_t dataOffset = header->dataOffset; // 0~65535 byte offset
    uint32_t dataLength = header->dataLength; // bytes
    uint32_t totalPacketSize = dataOffset + dataLength + 8;

    *sweep_type = sweepingType;
    *DO = dataOffset;
    *DL = dataLength;

    // time stamp
    //time_t unix_time = (header->timeStamp / 1000000000) - 2208988800;
    //struct tm tm_info;
    //gmtime_s(&tm_info, &unix_time);
    //char time_buffer[30];
    //strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm_info);

    // data print
    //printf("Time\t\t:%s\n", time_buffer);
    //printf("Packet Counter\t:%u\n", packetCounter);
    //printf("Sweeping Type\t:%u  (Peak(0), Spectral(1), Peak with timestamps(2))\n", sweepingType);
    //printf("Trigger Mode\t:%u  (Internal trigger(0), External trigger(1))\n", triggerMode);
    //printf("Data Offset\t:0x%04X (%"PRIu16")\n", dataOffset, dataOffset);
    //printf("Data Length\t:%"PRIu32"\n", dataLength);
    //printf("Packet Size\t:%"PRIu32"\n", totalPacketSize);

    printf("Counter:%u\t", packetCounter);

    return 0;
}

int processPacket_tsPayload(char* buffer_payload, int* channel, int* fiber, int* sensor, double* wavelenght) {

    struct ts_peak_payload_t* ts_payload = (struct ts_peak_payload_t*)(buffer_payload);

    ts_peak_data_t peak_data;
    peak_data[0] = ts_payload->ts_peak_payload_LSB;
    peak_data[1] = ts_payload->ts_peak_payload_MSB;
    peak_data[2] = ts_payload->time_stamp;

    *channel = channel_id(peak_data);
    *fiber = fiber_id(peak_data);
    *sensor = sensor_id(peak_data);
    *wavelenght = wavelength(peak_data);

    //printf("Sensor ID\t:%u\n", sensor_id(peak_data));
    //printf("Fiber ID\t:%u\n", fiber_id(peak_data));
    //printf("Channel ID\t:%u\n", channel_id(peak_data));
    //printf("Wavelength\t:%.10e meters\n", wavelength(peak_data));
    //printf("Timestamp\t:%f ns\n", time_stamp(peak_data) * 0.5);

    //printf("(Sensor#%u, ", sensor_id(peak_data));
    //printf("Fiber#%u, ", fiber_id(peak_data));
    //printf("Channel#%u)\t", channel_id(peak_data));
    //printf("Wavelength:%.10e meters\n", wavelength(peak_data));

    return 0;
}

int processPacket_Payload(char* buffer_payload, int* channel, int* fiber, int* sensor, double* wavelenght) {

    struct peak_payload_t* payload = (struct peak_payload_t*)(buffer_payload);

    peak_data_t peak_data;
    peak_data[0] = payload->peak_payload_LSB;
    peak_data[1] = payload->peak_payload_MSB;

    *channel = channel_id(peak_data);
    *fiber = fiber_id(peak_data);
    *sensor = sensor_id(peak_data);
    *wavelenght = wavelength(peak_data);

    //printf("Sensor ID\t:%u\n", sensor_id(peak_data));
    //printf("Fiber ID\t:%u\n", fiber_id(peak_data));
    //printf("Channel ID\t:%u\n", channel_id(peak_data));
    //printf("Wavelength\t:%.10e meters\n", wavelength(peak_data));

    //printf("(Sensor#%u, ", sensor_id(peak_data));
    //printf("Fiber#%u, ", fiber_id(peak_data));
    //printf("Channel#%u)\t", channel_id(peak_data));
    //printf("Wavelength\t:%.10e meters\n", wavelength(peak_data));

    return 0;
}

int processPacket_errorPayload(char* error_payload) {

    struct error_payload_t* payload = (struct error_payload_t*)(error_payload);

    peak_data_t data;
    data[1] = payload->error_id;
    data[0] = payload->error_description;

    if (data[1] == 500) {
        printf("Error type : Missing Peak\n");
        printf("Sensor #%u, Fiber #%u, Channel #%u\n",
            sensor_id(data), fiber_id(data), channel_id(data));
        printf("Error source: No peak was detected where one was "
            "expected from the interrogator configuration.\n"
            "Possible causes include :\n"
            "* Misconfiguration of sensor wavelength range or threshold\n"
            "* disconnected sensor\n");
    }
    else if (data[1] == 501) {
        printf("Error type : Multiple Peaks\n");
        printf("Sensor #%u, Fiber #%u, Channel #%u\n",
            sensor_id(data), fiber_id(data), channel_id(data));
        printf("Error source : More than the expected number of peaks was "
            "detected in the sensors wavelength range.\n"
            "Possible causes include :\n"
            "* Misconfiguration of sensor wavelength range or threshold\n");
    }
    else {
        printf("Error type : Internal Error\n");
        printf("Error source: Internal error\n"
            "Possible causes include :\n"
            "* Transient mismatch of configuration and data stream\n"
            "* Internal failure\n");
    }

    return 0;
}

int calibrationForce(const double* initial_wavelength, const double* wavelength, double* force) {
    double present_wavelength = *wavelength;
    double delta_wavelength = present_wavelength - *initial_wavelength;
    double P_epsilon = 0.28;
    double strain = delta_wavelength / *initial_wavelength / (1 - P_epsilon);
    double K = 1.0 / 460.0 / 0.02986; // [/N]
    *force = strain / K *1000; // mN

    return 0;
}

/* extract wavelength value in meters */
double wavelength(const uint32_t* const peak_data) {
    uint32_t peak_value[2];
    peak_value[0] = (peak_data[0] & ~0xffff) | 0x7fff; /* mask out id bits */
    peak_value[1] = peak_data[1];
    return *(double*)&peak_value;
}
uint8_t channel_id(const uint32_t* const peak_data) {
    return (uint8_t)(peak_data[0] >> 12) & 0x0f;
}
uint8_t fiber_id(const uint32_t* const peak_data) {
    return (uint8_t)(peak_data[0] >> 8) & 0x0f;
}
uint8_t sensor_id(const uint32_t* const peak_data) {
    return (uint8_t)(peak_data[0] & 0xff);
}
/* extract time stamp value in seconds */
double time_stamp(const uint32_t* const peak_data) {
    return (double)peak_data[2] * 5e-10;
}
