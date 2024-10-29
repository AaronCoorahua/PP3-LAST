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
    timerC timer(5000); // Timeout de 5000 ms para diagnóstico

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
        TRACE << "Window State: base=" << base << ", nextseqnum=" << nextseqnum << ENDL;

        // Enviar paquetes en la ventana si hay espacio y aún no se han enviado todos los datos
        if (nextseqnum < base + WINDOW_SIZE && !allSent) {
            datagramS packet = {};
            packet.seqNum = nextseqnum; // Asigna el número de secuencia

            // Leer datos del archivo
            inputFile.read(packet.data, MAX_PAYLOAD_LENGTH);
            packet.payloadLength = inputFile.gcount();
            packet.checksum = computeChecksum(packet);

            // Almacenar el paquete en la ventana y enviarlo
            sndpkt[nextseqnum % WINDOW_SIZE] = packet;
            connection.udt_send(packet);
            TRACE << "Sent packet: " << toString(packet) << ENDL;

            // Iniciar el temporizador si es el primer paquete en la ventana
            if (base == nextseqnum) {
                TRACE << "Starting timer for base: " << base << ENDL;
                timer.start();
            }

            nextseqnum++; // Incrementar el número de secuencia para el siguiente paquete
            DEBUG << "Incremented nextseqnum to: " << nextseqnum << ENDL;

            // Verificar si hemos llegado al final del archivo
            if (packet.payloadLength < MAX_PAYLOAD_LENGTH) {
                allSent = true; // Marcar que todos los datos han sido enviados
                TRACE << "All data read from file, marking allSent as true." << ENDL;
            }
        }

        // Recepción de ACKs
        datagramS ackPacket;
        if (connection.udt_receive(ackPacket) > 0) {
            TRACE << "Received ACK with ackNum=" << ackPacket.ackNum << ", base=" << base << ENDL;
            if (validateChecksum(ackPacket) && ackPacket.ackNum >= base) {
                TRACE << "Valid ACK for packet " << ackPacket.ackNum << ENDL;
                base = ackPacket.ackNum + 1; // Actualiza el base al siguiente número esperado

                // Detener el temporizador si todos los paquetes en la ventana han sido confirmados
                if (base == nextseqnum) {
                    TRACE << "All packets in window acknowledged, stopping timer." << ENDL;
                    timer.stop();
                } else {
                    TRACE << "Packets still unacknowledged, restarting timer." << ENDL;
                    timer.start(); // Reiniciar el temporizador para esperar más ACKs
                }
                DEBUG << "Updated base to: " << base << ENDL;
            } else {
                TRACE << "Received corrupted or invalid ACK, ignoring." << ENDL;
            }
        }

        // Reenviar en caso de timeout
        if (timer.timeout()) {
            TRACE << "Timeout occurred for base: " << base << ". Resending packets from base to nextseqnum - 1." << ENDL;
            for (uint16_t i = base; i < nextseqnum; ++i) {
                connection.udt_send(sndpkt[i % WINDOW_SIZE]);
                TRACE << "Resent packet: " << toString(sndpkt[i % WINDOW_SIZE]) << " with seqNum=" << sndpkt[i % WINDOW_SIZE].seqNum << ENDL;
            }
            timer.start(); // Reiniciar el temporizador después de reenviar
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
