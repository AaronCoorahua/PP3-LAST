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

    // Abrir el archivo de entrada
    std::ifstream inputFile(inputFilename, std::ios::binary);
    if (!inputFile.is_open()) {
        FATAL << "Unable to open file: " << inputFilename << ENDL;
        return 1;
    }

    // Inicializar los parámetros de la conexión y el temporizador
    unreliableTransportC connection(hostname, portNum);
    timerC timer(5);

    // Inicializar correctamente base y nextseqnum en cada ejecución
    uint16_t base = 1;           // Reinicia el base a 1
    uint16_t nextseqnum = 1;      // Reinicia el nextseqnum a 1
    bool allSent = false;         // Indica si todos los datos fueron enviados

    // Array para almacenar los paquetes enviados pero no confirmados
    std::array<datagramS, WINDOW_SIZE> sndpkt;

    // Detener el temporizador al inicio
    timer.stop();

    // Bucle principal de envío de datos
    while (!allSent || base != nextseqnum) {
    // Enviar paquetes si hay espacio en la ventana
        if (nextseqnum < base + WINDOW_SIZE && !allSent) {
            datagramS packet = {};
            packet.seqNum = nextseqnum;
            
            // Leer datos del archivo
            inputFile.read(packet.data, MAX_PAYLOAD_LENGTH);
            packet.payloadLength = inputFile.gcount();
            packet.checksum = computeChecksum(packet);
    
            // Almacenar y enviar el paquete
            sndpkt[nextseqnum % WINDOW_SIZE] = packet;
            connection.udt_send(packet);
            TRACE << "Sent packet: " << toString(packet) << ENDL;
    
            // Iniciar el temporizador si es el primer paquete en la ventana
            if (base == nextseqnum) {
                timer.start();
            }
    
            nextseqnum++;
            if (packet.payloadLength < MAX_PAYLOAD_LENGTH) {
                allSent = true;
            }
        }
    
        // Recepción de ACKs
        datagramS ackPacket;
        if (connection.udt_receive(ackPacket) > 0) {
            if (validateChecksum(ackPacket) && ackPacket.ackNum >= base) {
                base = ackPacket.ackNum + 1;
                if (base == nextseqnum) {
                    timer.stop();
                } else {
                    timer.start();
                }
            }
        }
    
        // Timeout y reenvío
        if (timer.timeout()) {
            for (uint16_t i = base; i < nextseqnum; ++i) {
                connection.udt_send(sndpkt[i % WINDOW_SIZE]);
            }
            timer.start();
        }
    }


    // Enviar paquete final para indicar fin del archivo
    datagramS endPacket = {};
    endPacket.seqNum = nextseqnum;
    endPacket.payloadLength = 0;
    endPacket.checksum = computeChecksum(endPacket);
    connection.udt_send(endPacket);
    TRACE << "Sent end of file packet: " << toString(endPacket) << ENDL;

    inputFile.close();
    TRACE << "File transfer completed. Cleaning up and exiting." << ENDL;

    return 0;
}
