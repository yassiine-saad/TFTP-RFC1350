
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define TFTP_OPCODE_RRQ 1
#define TFTP_OPCODE_WRQ 2
#define TFTP_OPCODE_DATA 3
#define TFTP_OPCODE_ACK 4
#define TFTP_OPCODE_ERR 5

typedef struct {
    uint16_t opcode;
    char filename[512];
    char mode[10]; // octet netascii
} TFTP_Request;

typedef struct {
    uint16_t opcode;
    uint16_t block_num;
    char data[512];
} TFTP_DataPacket;

typedef struct {
    uint16_t opcode;
    uint16_t block_num;
} TFTP_AckPacket;

typedef struct {
    uint16_t opcode;
    uint16_t err_code;
    char err_msg[512];
} TFTP_ErrorPacket;


#define TIMEOUT_SECONDS 1
#define MAX_RETRIES 3

#define MAX_PACKET_SIZE 516
#define TFTP_DATA_PACKET_SIZE 516


enum TFTPError {
    NotDefined = 0,
    FileNotFound = 1,
    AccessViolation = 2,
    DiskFullOrAllocationExceeded = 3,
    IllegalOperation = 4,
    UnknownTransferID = 5,
    FileAlreadyExists = 6,
    NoSuchUser = 7,
    NUM_TFTP_ERRORS 
};

// Tableau de messages d'erreur correspondant aux codes d'erreur TFTP
const char* TFTPErrorMessages[NUM_TFTP_ERRORS] = {
    "Not defined, see error message (if any)",
    "File not found",
    "Access violation",
    "Disk full or allocation exceeded",
    "Illegal TFTP operation",
    "Unknown transfer ID",
    "File already exists",
    "No such user"
};

typedef void (*TFTP_HandlerFunction)(int sockfd, struct sockaddr_in* client_addr, TFTP_Request* request);

void handle_read_request(int sockfd, struct sockaddr_in* client_addr, TFTP_Request *request);
void sendErrorPacket(int sockfd, struct sockaddr_in client_addr, uint16_t errorCode, const char *errorMsg);
void handle_write_request(int sockfd, struct sockaddr_in* client_addr, TFTP_Request *request);
const char* get_error_message(enum TFTPError error);




int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;


    // Création du socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Erreur lors de la création du socket");
        exit(1);
    }

    // Configuration de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(69); // Port du serveur TFTP

    // Liaison du socket à l'adresse du serveur
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erreur lors de la liaison du socket");
        exit(1);
    }

    printf("Serveur TFTP en attente de connexions (port 69)...\n");

    // Réception de la demande de lecture (RRQ)
    

   
    TFTP_Request request;
    client_len = sizeof(client_addr);
    char buffer[MAX_PACKET_SIZE];

    while (1){
        ssize_t num_bytes_received = recvfrom(sockfd, &buffer, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (num_bytes_received == -1) {
            perror("Erreur lors de la réception de la demande");
            continue;
        }

        printf("Taille du paquet reçu: %zd octets\n", num_bytes_received);
        memcpy(&request.opcode, buffer, sizeof(uint16_t));
        TFTP_HandlerFunction selectedHandler = NULL;


        // Gestion de la demande en fonction de l'opcode
        if (ntohs(request.opcode) == TFTP_OPCODE_RRQ) {
            selectedHandler = handle_read_request;
        } else if (ntohs(request.opcode) == TFTP_OPCODE_WRQ) {
            selectedHandler = handle_write_request;
        } else {
            // Opcode non pris en charge, envoi d'un paquet d'erreur au client
            sendErrorPacket(sockfd, client_addr, 0, "Opcode non pris en charge");
            continue;
        }


       

        // Extraction du nom de fichier
        size_t filename_length = strlen(buffer + 2);
        strcpy(request.filename, buffer + 2);
        if (filename_length == 0) {
            // Gestion de l'erreur : Nom de fichier vide
            printf("Erreur: Nom de fichier vide.\n");
            // Envoyer un paquet d'erreur au client
            sendErrorPacket(sockfd, client_addr, NotDefined, "Nom de fichier vide");
            continue;
        }
       

        // Extraction du mode de transfert
        size_t mode_offset = 2 + filename_length + 1; // Offset pour accéder au début du mode
        size_t mode_length = strlen(buffer + mode_offset);

        strcpy(request.mode, buffer + mode_offset);
        if (mode_length == 0 || (strcasecmp(request.mode, "netascii") != 0 && strcasecmp(request.mode, "octet") != 0) ) {
            // Gestion de l'erreur : Mode de transfert non reconnu
            printf("Erreur: Mode de transfert non reconnu.\n");
            // Envoyer un paquet d'erreur au client
            sendErrorPacket(sockfd, client_addr, NotDefined, "Mode de transfert non reconnu");
            continue;
        }
        
        if (selectedHandler != NULL) {
            selectedHandler(sockfd, &client_addr, &request);
        } else {
            // Opcode non pris en charge, envoi d'un paquet d'erreur au client
            sendErrorPacket(sockfd, client_addr, 0, "Opcode non pris en charge");
        }

    }
    close(sockfd);
    return 0;
}


void handle_read_request(int sockfd, struct sockaddr_in* client_addr, TFTP_Request *request) {
    printf("[RRQ] @IP %s:%d, file: %s, Mode: %s\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), request->filename, request->mode);
   
    // Ouverture du fichier demandé
    FILE *file;
    if (strcasecmp(request->mode, "netascii") == 0) {
        file = fopen(request->filename, "r");
    } else if (strcasecmp(request->mode, "octet") == 0) {
        file = fopen(request->filename, "rb");
    }

    if (file == NULL) {
        printf("Erreur: fichier non trouvé\n");
        // Envoi d'un paquet d'erreur au client
        sendErrorPacket(sockfd, *client_addr,FileNotFound, "Fichier non trouvé");
        return;
    }

    // Création de la nouvelle socket pour les données
    int sockfd_data;
    if ((sockfd_data = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Erreur lors de la création de la nouvelle socket pour les données");
        exit(1);
    }

    // Liaison de la nouvelle socket à un port éphémère
    struct sockaddr_in server_addr_data;
    memset(&server_addr_data, 0, sizeof(server_addr_data));
    server_addr_data.sin_family = AF_INET;
    server_addr_data.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr_data.sin_port = htons(0); // Utilisation d'un port éphémère
    if (bind(sockfd_data, (struct sockaddr*)&server_addr_data, sizeof(server_addr_data)) == -1) {
        perror("Erreur lors de la liaison de la nouvelle socket");
        exit(1);
    }

    // printf("[PORT] Nouveau port : %d\n", ntohs(server_addr_data.sin_port));

    // Envoi des paquets de données
    TFTP_DataPacket data_packet;
    TFTP_AckPacket ack_packet;

    size_t num_bytes_read;
    int block_num = 1;
    
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(sockfd_data, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    size_t total_bytes_sent = 0;

    do {
        num_bytes_read = fread(data_packet.data, 1, sizeof(data_packet.data), file);
        if (num_bytes_read == -1) {
            perror("Erreur lors de la lecture du fichier");
            exit(EXIT_FAILURE);
        }

        data_packet.opcode = htons(TFTP_OPCODE_DATA);
        data_packet.block_num = htons(block_num);

        if (sendto(sockfd_data, &data_packet, num_bytes_read + 4, 0, (struct sockaddr*)client_addr, sizeof(*client_addr)) == -1) {
            perror("Erreur lors de l'envoi du paquet de données");
            exit(EXIT_FAILURE);
        }

        printf("[DATA] Packet : %d (%zd Bytes) -> @IP %s:%d\n", block_num,num_bytes_read+4,inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));


        int retryCount = 0;
        while (1) {
            ssize_t recvlen = recvfrom(sockfd_data, &ack_packet, sizeof(ack_packet), 0, NULL, NULL);
            if (recvlen > 0 && ack_packet.opcode == htons(TFTP_OPCODE_ACK) && ack_packet.block_num == htons(block_num)) {
                printf("[ACK] Packet : %d <- @IP %s:%d\n", ntohs(ack_packet.block_num),inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
                total_bytes_sent += num_bytes_read;
                break; // ACK reçu
            } else if (recvlen == -1) {
                // Timeout, retransmission
                if (retryCount < MAX_RETRIES) {
                    printf("[TIMEOUT], retransmission du bloc %zd\n", block_num);
                    sendto(sockfd_data, &data_packet, num_bytes_read + 4, 0, (struct sockaddr*)client_addr, sizeof(*client_addr));
                    retryCount++;
                } else {
                    printf("[!] Nombre maximum de tentatives atteint, envoi d'un paquet d'erreur et abandon.\n");
                    sendErrorPacket(sockfd, *client_addr,NotDefined, "Nombre maximum de tentatives atteint");
                    fclose(file);
                    close(sockfd_data);
                    return;
                }
            }
        }
        block_num++;


        
    } while (num_bytes_read == sizeof(data_packet.data));


    printf("|->Transmission terminée avec succès. | file : %s (%zu):\n",request->filename, total_bytes_sent);

    // Fermeture du fichier et de la socket de données
    fclose(file);
    close(sockfd_data);
}





void handle_write_request(int sockfd, struct sockaddr_in* client_addr, TFTP_Request *request) {
    printf("[WRQ] @IP %s:%d, file: %s, Mode: %s\n", inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), request->filename, request->mode);


    // Ouverture du fichier en écriture
    FILE *file;
    if (strcasecmp(request->mode, "netascii") == 0) {
        file = fopen(request->filename, "w");
    } else if (strcasecmp(request->mode, "octet") == 0) {
        file = fopen(request->filename, "wb");
    }

    if (file == NULL) {
        printf("Erreur: impossible d'ouvrir le fichier en écriture\n");
        // Envoi d'un paquet d'erreur au client
        sendErrorPacket(sockfd, *client_addr, DiskFullOrAllocationExceeded, "Impossible d'ouvrir le fichier en écriture");
        return;
    }

    // Création de la nouvelle socket pour les données
    int sockfd_data;
    if ((sockfd_data = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Erreur lors de la création de la nouvelle socket pour les données");
        exit(1);
    }

    // Liaison de la nouvelle socket à un port éphémère
    struct sockaddr_in server_addr_data;
    memset(&server_addr_data, 0, sizeof(server_addr_data));
    server_addr_data.sin_family = AF_INET;
    server_addr_data.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr_data.sin_port = htons(0); // Utilisation d'un port éphémère
    if (bind(sockfd_data, (struct sockaddr*)&server_addr_data, sizeof(server_addr_data)) == -1) {
        perror("Erreur lors de la liaison de la nouvelle socket à un port éphémère");
        exit(1);
    }

    // printf("[PORT] Nouveau port : %d\n", ntohs(server_addr_data.sin_port));

    // Envoi du premier ACK
    TFTP_AckPacket ackPacket;
    ackPacket.opcode = htons(TFTP_OPCODE_ACK);
    ackPacket.block_num = htons(0);
    sendto(sockfd_data, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr*)client_addr, sizeof(*client_addr));

    // Réception et écriture des paquets de données
    int blockNumber = 1;

    struct timeval tv;
    tv.tv_sec = TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(sockfd_data, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    int retryCount = 0;

    size_t total_bytes_received = 0;

    while (1) {
        TFTP_DataPacket dataPacket;
        ssize_t recvlen = recvfrom(sockfd_data, &dataPacket, sizeof(dataPacket), 0, NULL, NULL);

        if (recvlen == -1) {
            // Timeout, retransmission de l'ACK précédent
            printf("Timeout, retransmission de l'ACK précédent\n");
            if (retryCount < MAX_RETRIES) {
                sendto(sockfd_data, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr*)client_addr, sizeof(*client_addr));
                retryCount++;
                continue;
            } else {
                printf("Nombre maximum de tentatives atteint, abandon de la transmission.\n");
                fclose(file);
                close(sockfd_data);
                return;
            }
        }
        retryCount = 0;

        if (dataPacket.opcode == htons(TFTP_OPCODE_DATA) && ntohs(dataPacket.block_num) == blockNumber-1) {
            sendto(sockfd_data, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr*)client_addr, sizeof(*client_addr));
            continue;
        }

        if (dataPacket.opcode == htons(TFTP_OPCODE_DATA) && ntohs(dataPacket.block_num) == blockNumber) {
            total_bytes_received += recvlen - 4;
            size_t bytesWritten = fwrite(dataPacket.data, 1, recvlen - 4, file);
            if (bytesWritten < recvlen - 4) {
                printf("Erreur lors de l'écriture dans le fichier\n");
                // Envoi d'un paquet d'erreur au client
                sendErrorPacket(sockfd_data, *client_addr, DiskFullOrAllocationExceeded, "Erreur lors de l'écriture dans le fichier");
                fclose(file);
                close(sockfd_data);
                return;
            }

            // Envoi de l'ACK
            ackPacket.block_num = dataPacket.block_num;
            sendto(sockfd_data, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr*)client_addr, sizeof(*client_addr));

            if (recvlen < TFTP_DATA_PACKET_SIZE) {
                // Dernier paquet reçu, fin de la transmission
                break;
            }
            blockNumber++;
        } else if (ntohs(dataPacket.opcode) == TFTP_OPCODE_ERR) {
            printf("Erreur reçue du serveur : %s\n",dataPacket.data);
            fclose(file);
            return;
        } else {
            fclose(file);
            close(sockfd_data);
            sendErrorPacket(sockfd_data, *client_addr, NotDefined, "Paquet invalide reçu du serveur.");
            return;
        }
    }
    printf("|->Réception terminée avec succès. | file : %s (%zu):\n",request->filename, total_bytes_received);
    // Fermeture du fichier et de la socket de données
    fclose(file);
    close(sockfd_data);
}

void sendErrorPacket(int sockfd, struct sockaddr_in client_addr, uint16_t errorCode, const char *errorMsg) {
    TFTP_ErrorPacket errPacket;
    errPacket.opcode = htons(TFTP_OPCODE_ERR);
    errPacket.err_code = htons(errorCode);
    strcpy(errPacket.err_msg, errorMsg);
    sendto(sockfd, &errPacket, sizeof(errPacket), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
}

const char* get_error_message(enum TFTPError error) {
    if (error >= 0 && error < NUM_TFTP_ERRORS) {
        return TFTPErrorMessages[error];
    } else {
        return "Unknown error";
    }
}