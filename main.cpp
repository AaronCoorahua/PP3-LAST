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
#define MAX_PAYLOAD_LENGTH 512

int main(int argc, char* argv[]) {
    // Default parameters
    uint16_t portNum(12345);
    std::string hostname("isengard.mines.edu");
    std::string inputFilename("input.dat");
    int LOG_LEVEL = 0; // Set the default log level

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

    // Open input file
    std::ifstream inputFile(inputFilename, std::ios::binary);
    if (!inputFile.is_open()) {
        FATAL << "Unable to open file: " << inputFilename << ENDL;
        return 1;
    }

    // Initialize unreliable transport, timer, and window variables
    unreliableTransportC connection(hostname, portNum);
    timerC timer(500); // Set a 500 ms timeout duration
    std::array<datagramS, WINDOW_SIZE> sndpkt;
    uint16_t base = 1;
    uint16_t nextseqnum = 1;
    bool allSent = false;

    timer.stop();

    while (!allSent || base != nextseqnum) {
        // Check if there is space in the window
        if (nextseqnum < base + WINDOW_SIZE && !allSent) {
            datagramS packet = {};
            packet.seqNum = nextseqnum;
            
            // Read data from the file
            inputFile.read(packet.data, MAX_PAYLOAD_LENGTH);
            packet.payloadLength = inputFile.gcount();
            packet.checksum = computeChecksum(packet);

            // Store packet in the window and send it
            sndpkt[nextseqnum % WINDOW_SIZE] = packet;
            connection.udt_send(packet);
            TRACE << "Sent packet: " << toString(packet) << ENDL;

            // Start timer if it's the first packet in the window
            if (base == nextseqnum) {
                timer.start();
            }

            nextseqnum++;

            // Check if we've reached the end of the file
            if (packet.payloadLength < MAX_PAYLOAD_LENGTH) {
                allSent = true;
            }
        }

        // Check for acknowledgments
        datagramS ackPacket;
        if (connection.udt_receive(ackPacket) > 0) {
            if (validateChecksum(ackPacket) && ackPacket.ackNum >= base) {
                TRACE << "Received ACK for packet " << ackPacket.ackNum << ENDL;
                base = ackPacket.ackNum + 1;

                // Stop timer if all packets in the window are acknowledged
                if (base == nextseqnum) {
                    timer.stop();
                } else {
                    timer.start(); // Restart the timer for remaining packets
                }
            }
        }

        // Check for timeout
        if (timer.timeout()) {
            TRACE << "Timeout occurred. Resending packets from base: " << base << " to " << (nextseqnum - 1) << ENDL;
            for (uint16_t i = base; i < nextseqnum; ++i) {
                connection.udt_send(sndpkt[i % WINDOW_SIZE]);
                TRACE << "Resent packet: " << toString(sndpkt[i % WINDOW_SIZE]) << ENDL;
            }
            timer.start(); // Restart timer after retransmitting
        }
    }

    // Send final packet to indicate end of file
    datagramS endPacket = {};
    endPacket.seqNum = nextseqnum;
    endPacket.payloadLength = 0;
    endPacket.checksum = computeChecksum(endPacket);
    connection.udt_send(endPacket);
    TRACE << "Sent end of file packet: " << toString(endPacket) << ENDL;

    // Close file and cleanup
    inputFile.close();
    return 0;
}
