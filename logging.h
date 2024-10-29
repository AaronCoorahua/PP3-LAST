#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <array>
#include "timerC.h"
#include "unreliableTransport.h"
#include "datagram.h"
#include "logging.h"

#define WINDOW_SIZE 10
#define MAX_PAYLOAD_LENGTH 255

int main(int argc, char* argv[]) {
    // Default parameters
    uint16_t portNum(12345);
    std::string hostname("isengard.mines.edu");
    std::string inputFilename("file1.html");
    int LOG_LEVEL = 5;

    int opt;
    try {
        while ((opt = getopt(argc, argv, "f:h:p:d:")) != -1) {
            switch (opt) {
                case 'p':
                    portNum = std::stoi(optarg);
                    break;
                case 'h':
                    hostname = optarg;
                    break;
                case 'd':
                    LOG_LEVEL = std::stoi(optarg);
                    break;
                case 'f':
                    inputFilename = optarg;
                    break;
                case '?':
                default:
                    std::cout << "Usage: " << argv[0] << " [-h hostname] [-p port] [-d debug_level] [-f filename]" << std::endl;
                    return 1;
            }
        }
    } catch (std::exception &e) {
        FATAL << "Invalid command line arguments: " << e.what() << ENDL;
        return 1;
    }

    TRACE << "Command line arguments parsed." << ENDL;
    TRACE << "\tServername: " << hostname << ENDL;
    TRACE << "\tPort number: " << portNum << ENDL;
    TRACE << "\tDebug Level: " << LOG_LEVEL << ENDL;
    TRACE << "\tInput file name: " << inputFilename << ENDL;

    // Open the input file
    std::ifstream inputFile(inputFilename, std::ios::binary);
    if (!inputFile.is_open()) {
        FATAL << "Unable to open file: " << inputFilename << ENDL;
        return 1;
    }

    INFO << "File opened successfully: " << inputFilename << ENDL;

    // Initialize connection parameters and timer
    unreliableTransportC connection(hostname, portNum);
    timerC timer(10);

    // Initialize base and nextseqnum for each execution
    uint16_t base = 1;           // Reset base to 1
    uint16_t nextseqnum = 1;      // Reset nextseqnum to 1
    bool allSent = false;         // Indicates if all data has been sent

    // Array to store sent but unacknowledged packets
    std::array<datagramS, WINDOW_SIZE> sndpkt;

    // Stop the timer at the beginning
    timer.stop();

    INFO << "Starting data transfer..." << ENDL;

    // Main loop for sending data
    while (!allSent || base != nextseqnum) {
        // Send packets if there is space in the window
        if (nextseqnum < base + WINDOW_SIZE && !allSent) {
            datagramS packet = {};
            packet.seqNum = nextseqnum;
            
            // Read data from the file
            inputFile.read(packet.data, MAX_PAYLOAD_LENGTH);
            packet.payloadLength = inputFile.gcount();
            packet.checksum = computeChecksum(packet);
    
            // Store and send the packet
            sndpkt[nextseqnum % WINDOW_SIZE] = packet;
            connection.udt_send(packet);
            TRACE << "Sent packet: " << toString(packet) << ENDL;

            DEBUG << "Packet with seqNum " << packet.seqNum << " sent. Payload length: " << packet.payloadLength << ENDL;

            // Start the timer if it's the first packet in the window
            if (base == nextseqnum) {
                timer.start();
                DEBUG << "Timer started as this is the first packet in the window." << ENDL;
            }
    
            nextseqnum++;
            if (packet.payloadLength < MAX_PAYLOAD_LENGTH) {
                allSent = true;
                INFO << "All data from the file has been read and sent." << ENDL;
            }
        }
    
        // Receive ACKs
        datagramS ackPacket;
        if (connection.udt_receive(ackPacket) > 0) {
            if (validateChecksum(ackPacket) && ackPacket.ackNum >= base) {
                INFO << "Received valid ACK for seqNum: " << ackPacket.ackNum << ENDL;
                base = ackPacket.ackNum + 1;

                // Stop the timer if all packets in the window are acknowledged
                if (base == nextseqnum) {
                    timer.stop();
                    DEBUG << "Timer stopped. All packets in the window are acknowledged." << ENDL;
                } else {
                    timer.start();
                    DEBUG << "Timer restarted for remaining packets in the window." << ENDL;
                }
            } else {
                WARNING << "Received an invalid or duplicate ACK with seqNum: " << ackPacket.ackNum << ENDL;
            }
        }
    
        // Handle timeout and retransmissions
        if (timer.timeout()) {
            WARNING << "Timeout occurred. Retransmitting packets from base " << base << " to " << (nextseqnum - 1) << ENDL;
            for (uint16_t i = base; i < nextseqnum; ++i) {
                connection.udt_send(sndpkt[i % WINDOW_SIZE]);
                DEBUG << "Retransmitted packet with seqNum: " << sndpkt[i % WINDOW_SIZE].seqNum << ENDL;
            }
            timer.start();  // Restart timer after retransmitting
        }

        // Log the window state for debug purposes
        TRACE << "Window State: base=" << base << ", nextseqnum=" << nextseqnum << ENDL;
    }

    // Send final packet to indicate end of file
    datagramS endPacket = {};
    endPacket.seqNum = nextseqnum;
    endPacket.payloadLength = 0;
    endPacket.checksum = computeChecksum(endPacket);
    connection.udt_send(endPacket);
    INFO << "Sent end of file packet: " << toString(endPacket) << ENDL;

    inputFile.close();
    TRACE << "File transfer completed. Cleaning up and exiting." << ENDL;

    return 0;
}
