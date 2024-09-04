
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/time.h>

#define TFTP_PACKET_SIZE 516

// les codes operations
#define TFTP_OPCODE_RRQ 1
#define TFTP_OPCODE_WRQ 2
#define TFTP_OPCODE_DATA 3
#define TFTP_OPCODE_ACK 4
#define TFTP_OPCODE_ERR 5

#define DATA_PACKET_SIZE (sizeof(TFTP_DataPacket))
#define ACK_PACKET_SIZE (sizeof(TFTP_AckPacket))
#define ERROR_PACKET_SIZE (sizeof(TFTP_ErrorPacket))


#define MAX_RETRIES 3
#define TIMEOUT_SECONDS 5

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

int receive_data_packets(int sockfd, struct sockaddr_in *server_addr, FILE *file, char* request, int request_length);
void send_data_packets(int sockfd, struct sockaddr_in *server_addr, FILE *file, char* request, int request_length);

void send_read_request(int sockfd, struct sockaddr_in *server_addr, char *filename, char *transfer_mode);
void send_write_request(int sockfd, struct sockaddr_in *server_addr, char *filename,char *transfer_mode);

const char *get_filename(const char *full_path);

    

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in server_addr;
    int server_port;
    char *server_ip, *filename, *mode,*transfer_mode;

    // Vérifier le nombre d'arguments
    if (argc != 6) {
        printf("Usage: %s <Server IP> <Server Port> <get/put> <Filename> <netascii/octet>\n", argv[0]);
        exit(EXIT_FAILURE);
    }


    // Extraire les arguments
    server_ip = argv[1];
    server_port = atoi(argv[2]);
    mode = argv[3];
    filename = argv[4];
    transfer_mode = argv[5]; 

    ;
    

    // Vérifier le mode
    if (strcmp(mode, "get") != 0 && strcmp(mode, "put") != 0) {
        printf("Invalid mode. Please use 'get' or 'put'.\n");
        exit(EXIT_FAILURE);
    }

    // Vérifier le mode de transfer
    if (strcasecmp(transfer_mode, "octet") != 0 && strcasecmp(transfer_mode, "netascii") != 0) {
        printf("Mode de transfert invalide. Utilisez 'octet' ou 'netascii'.\n");
        exit(EXIT_FAILURE);
    }




    // Créer un socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Initialiser les informations du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Envoyer la requête appropriée en fonction du mode
    if (strcmp(mode, "get") == 0) {
        send_read_request(sockfd, &server_addr, filename,transfer_mode);
    } else if (strcmp(mode, "put") == 0) {
        // verifier si le fichier existe
        send_write_request(sockfd, &server_addr, filename,transfer_mode);
    } else {
        printf("Invalid mode. Please use 'get' or 'put'.\n");
        exit(EXIT_FAILURE);
    }

    close(sockfd);
    return 0;
}



// Fonction pour recevoir des données depuis un serveur TFTP
int receive_data_packets(int sockfd, struct sockaddr_in *server_addr, FILE *file, char* request, int request_length) {
    // char buffer[TFTP_PACKET_SIZE];
    // int len, block = 1;

    TFTP_DataPacket dataPacket;
    TFTP_AckPacket ackPacket;
    char buffer[TFTP_PACKET_SIZE];
    socklen_t server_len = sizeof(struct sockaddr_in);
    uint16_t expectedBlockNumber = 1;

    struct timeval tv;
    tv.tv_sec = TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    int retryCount = 0;

    while (1) {
        ssize_t recvlen = recvfrom(sockfd, buffer, TFTP_PACKET_SIZE, 0, (struct sockaddr*)server_addr, &server_len);
       

        if (recvlen == -1) {
            // Timeout, retransmission
            if (retryCount < MAX_RETRIES) {

                if (expectedBlockNumber == 1){
                    sendto(sockfd, request, request_length, 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in));
                    printf("[RRQ] Demande de lecture envoyée au port %d.\n", ntohs(server_addr->sin_port));
                    retryCount++;
                    continue;
                } else {
                    printf("Timeout, retransmission de l'ACK précédent\n");
                    sendto(sockfd, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr*)server_addr, sizeof(*server_addr));
                    retryCount++;
                    continue;
                }
                
            } else {
                printf("Nombre maximum de tentatives atteint, abandon de la transmission.\n");
                fclose(file);
                close(sockfd);
                return -1;
            }
        }
        retryCount = 0;

        // Vérification du type de paquet
        uint16_t opcode;
        memcpy(&opcode, buffer, sizeof(uint16_t));
        opcode = ntohs(opcode);

        if (opcode == TFTP_OPCODE_DATA) {
            // Paquet de données
            memcpy(&dataPacket, buffer, recvlen);
            printf("Paquet DATA [%d] : Données reçues (Taille: %ld) du port %d\n", ntohs(dataPacket.block_num), recvlen, ntohs(server_addr->sin_port));

            if (dataPacket.block_num == htons(expectedBlockNumber)) {
                fwrite(dataPacket.data, 1, recvlen - 4, file);

                // Envoi de l'ACK au serveur
                
                ackPacket.opcode = htons(TFTP_OPCODE_ACK);
                ackPacket.block_num = dataPacket.block_num;
                sendto(sockfd, &ackPacket, 4, 0, (struct sockaddr*)server_addr, server_len);

                expectedBlockNumber++;
            } else if (dataPacket.block_num == htons(expectedBlockNumber - 1)) {
                // Envoi de l'ACK au serveur (ACK répété)
                TFTP_AckPacket ackPacket;
                ackPacket.opcode = htons(TFTP_OPCODE_ACK);
                ackPacket.block_num = dataPacket.block_num;
                sendto(sockfd, &ackPacket, 4, 0, (struct sockaddr*)server_addr, server_len);
            } else {
                printf("Numéro de bloc incorrect, attendu %d, reçu %d\n", expectedBlockNumber, ntohs(dataPacket.block_num));
            }

            if (recvlen < TFTP_PACKET_SIZE) {
                fclose(file);
                printf("Fin de la transmission.\n");
                break;
            }
        } else if (opcode == TFTP_OPCODE_ERR) {
            // Paquet d'erreur
            TFTP_ErrorPacket *errorPacket = (TFTP_ErrorPacket *)buffer;
            printf("Paquet ERROR reçu - Code d'erreur: %d, Message: %s\n", ntohs(errorPacket->err_code), errorPacket->err_msg);
            fclose(file);
            exit(EXIT_FAILURE);
        } else {
            // Autre type de paquet (non pris en charge dans cet exemple)
            printf("Paquet reçu de type inconnu\n");
            fclose(file);
            exit(EXIT_FAILURE);
        }
    }
}



void send_read_request(int sockfd, struct sockaddr_in *server_addr, char *filename, char *transfer_mode) {
    char request[TFTP_PACKET_SIZE];
    socklen_t server_len;
    
    // Opcode pour une requête RRQ
    request[0] = 0x00; // Opcode 1ère partie
    request[1] = 0x01; // Opcode 2ème partie
    
    // Copie du nom de fichier dans la requête
    strcpy(&request[2], filename);
    
    // Calcul de la longueur du nom du fichier
    int longueurNomFichier = strlen(filename);
    
    // Ajout d'un octet nul après le nom de fichier
    request[2 + longueurNomFichier] = 0x00;
    
    // Mode de transfert (ici "octet")
    strcpy(&request[3 + longueurNomFichier], "octet");
    
    // Ajout d'un octet nul après le mode de transfert
    int request_length = 4 + longueurNomFichier + strlen("octet") + 1;
    request[request_length - 1] = 0x00;

    // Envoi de la demande de lecture au serveur
    sendto(sockfd, request, request_length, 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in));

    printf("[RRQ] Demande de lecture envoyée au port %d.\n", ntohs(server_addr->sin_port));

    // Création d'un fichier pour écrire les données reçues
    FILE *file;

    // Ouvrir le fichier en fonction du mode de transfert
    if (strcasecmp(transfer_mode, "octet") == 0) {
        file = fopen(filename, "wb");
    } else if (strcasecmp(transfer_mode, "netascii") == 0) {
        file = fopen(filename, "w");
    }

    if (file == NULL) {
        perror("Erreur lors de l'ouverture du fichier");
        exit(EXIT_FAILURE);
    }

    receive_data_packets(sockfd, server_addr, file,request,request_length);
    printf("Fichier reçu avec succès et enregistré sous le nom '%s'.\n", filename);
}


// void send_write_request(int sockfd, struct sockaddr_in *server_addr, char *filename, char *transfer_mode) {
//     char request[TFTP_PACKET_SIZE];
//     socklen_t server_len;
     
//     FILE *file;
//     // Ouvrir le fichier en fonction du mode de transfert
//     if (strcasecmp(transfer_mode, "octet") == 0) {
//         file = fopen(filename, "rb");
//     } else if (strcasecmp(transfer_mode, "netascii") == 0) {
//         file = fopen(filename, "r");
//     }


//     if (file == NULL) {
//         perror("Erreur lors de l'ouverture du fichier en lecture");
//         exit(EXIT_FAILURE);
//     }


//     // Opcode pour une requête WRQ
//     request[0] = 0x00; // Opcode 1ère partie
//     request[1] = 0x02; // Opcode 2ème partie
    
//     // Copie du nom de fichier dans la requête
//     strcpy(&request[2], get_filename(filename));
    
//     // Calcul de la longueur du nom du fichier
//     int longueurNomFichier = strlen(filename);
    
//     // Ajout d'un octet nul après le nom de fichier
//     request[2 + longueurNomFichier] = 0x00;
    
//     // Mode de transfert (ici "octet")
//     strcpy(&request[3 + longueurNomFichier], transfer_mode);  //netascii
    
//     // Ajout d'un octet nul après le mode de transfert
//     int request_length = 4 + longueurNomFichier + strlen(transfer_mode) + 1;
//     request[request_length - 1] = 0x00;

//     // Envoi de la demande d'écriture au serveur
//     sendto(sockfd, request, request_length, 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in));


//     printf("[WRQ] Demande d'écriture envoyée au port %d.\n", ntohs(server_addr->sin_port));

//     // Attendre la réponse du serveur
//     server_len = sizeof(struct sockaddr_in);
//     char buffer[TFTP_PACKET_SIZE];
//     ssize_t recvlen = recvfrom(sockfd, buffer, TFTP_PACKET_SIZE, 0, (struct sockaddr*)server_addr, &server_len);
//     if (recvlen == -1) {
//         perror("Erreur lors de la réception de la réponse du serveur");
//         exit(EXIT_FAILURE);
//     }

//     // Vérification de la réponse du serveur (ACK)
//     uint16_t opcode;
//     memcpy(&opcode, buffer, sizeof(uint16_t));
//     opcode = ntohs(opcode);

//     if (opcode == TFTP_OPCODE_ACK) {
//         uint16_t block_num;
//         memcpy(&block_num, buffer + sizeof(uint16_t), sizeof(uint16_t));
//         block_num = ntohs(block_num);

//         if (block_num != 0) {
//             printf("Réponse inattendue du serveur. Attendu : ACK du bloc 0, Reçu : ACK du bloc %d.\n", block_num);
//             exit(EXIT_FAILURE);
//         }

//         printf("ACK[%d] reçu en réponse à la demande d'écriture. \n",block_num);

//     } else if (opcode == TFTP_OPCODE_ERR) {
//         // Paquet d'erreur
//         TFTP_ErrorPacket *errorPacket = (TFTP_ErrorPacket *)(buffer); // skip opcode
//         printf("Paquet ERROR reçu - Code d'erreur: %d, Message: %s\n", ntohs(errorPacket->err_code), errorPacket->err_msg);
//         exit(EXIT_FAILURE);
//     } else {
//         printf("Réponse inattendue du serveur.\n");
//         exit(EXIT_FAILURE);
//     }


//     send_data_packets(sockfd, server_addr, file,request,request_length);
//     fclose(file);

// }


void send_write_request(int sockfd, struct sockaddr_in *server_addr, char *filename, char *transfer_mode) {
    char request[TFTP_PACKET_SIZE];
    socklen_t server_len;
     
    FILE *file;
    // Ouvrir le fichier en fonction du mode de transfert
    if (strcasecmp(transfer_mode, "octet") == 0) {
        file = fopen(filename, "rb");
    } else if (strcasecmp(transfer_mode, "netascii") == 0) {
        file = fopen(filename, "r");
    }

    if (file == NULL) {
        perror("Erreur lors de l'ouverture du fichier en lecture");
        exit(EXIT_FAILURE);
    }

    // Opcode pour une requête WRQ
    request[0] = 0x00; // Opcode 1ère partie
    request[1] = 0x02; // Opcode 2ème partie
    
    // Copie du nom de fichier dans la requête
    strcpy(&request[2], get_filename(filename));
    
    // Calcul de la longueur du nom du fichier
    int longueurNomFichier = strlen(filename);
    
    // Ajout d'un octet nul après le nom de fichier
    request[2 + longueurNomFichier] = 0x00;
    
    // Mode de transfert (ici "octet")
    strcpy(&request[3 + longueurNomFichier], transfer_mode);  //netascii
    
    // Ajout d'un octet nul après le mode de transfert
    int request_length = 4 + longueurNomFichier + strlen(transfer_mode) + 1;
    request[request_length - 1] = 0x00;

    // Envoi de la demande d'écriture au serveur
    sendto(sockfd, request, request_length, 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in));

    printf("[WRQ] Demande d'écriture envoyée au port %d.\n", ntohs(server_addr->sin_port));

    // Attendre la réponse du serveur avec retransmission
    server_len = sizeof(struct sockaddr_in);
    char buffer[TFTP_PACKET_SIZE];
    ssize_t recvlen;
    int retryCount = 0;

    struct timeval tv;
    tv.tv_sec = TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    while (1) {
        recvlen = recvfrom(sockfd, buffer, TFTP_PACKET_SIZE, 0, (struct sockaddr*)server_addr, &server_len);
        if (recvlen > 0) {
            // Vérification de la réponse du serveur (ACK)
            uint16_t opcode;
            memcpy(&opcode, buffer, sizeof(uint16_t));
            opcode = ntohs(opcode);

            if (opcode == TFTP_OPCODE_ACK) {
                uint16_t block_num;
                memcpy(&block_num, buffer + sizeof(uint16_t), sizeof(uint16_t));
                block_num = ntohs(block_num);

                if (block_num != 0) {
                    printf("Réponse inattendue du serveur. Attendu : ACK du bloc 0, Reçu : ACK du bloc %d.\n", block_num);
                    exit(EXIT_FAILURE);
                }

                printf("ACK[%d] reçu en réponse à la demande d'écriture.\n", block_num);
                break; // Sortir de la boucle si un ACK est reçu
            } else if (opcode == TFTP_OPCODE_ERR) {
                // Paquet d'erreur
                TFTP_ErrorPacket *errorPacket = (TFTP_ErrorPacket *)(buffer); // skip opcode
                printf("Paquet ERROR reçu - Code d'erreur: %d, Message: %s\n", ntohs(errorPacket->err_code), errorPacket->err_msg);
                exit(EXIT_FAILURE);
            } else {
                printf("Réponse inattendue du serveur.\n");
                exit(EXIT_FAILURE);
            }
        } else if (recvlen == -1) {
            // Timeout, retransmission de la demande WRQ
            if (retryCount < MAX_RETRIES) {
                printf("Timeout, retransmission de la demande d'écriture.\n");
                sendto(sockfd, request, request_length, 0, (struct sockaddr*)server_addr, sizeof(struct sockaddr_in));
                retryCount++;
            } else {
                perror("Nombre maximal de tentatives atteint, abandon.");
                exit(EXIT_FAILURE);
            }
        }
    }

    // Envoi des paquets de données
    send_data_packets(sockfd, server_addr, file, request, request_length);
    fclose(file);
}

void send_data_packets(int sockfd, struct sockaddr_in *server_addr, FILE *file, char* request, int request_length) {
    uint8_t buffer[TFTP_PACKET_SIZE];
    socklen_t server_len = sizeof(struct sockaddr_in);
    uint16_t block_num = 1;
    
    
    int retryCount = 0;

    while (1) {
        // Lecture des données à envoyer depuis le fichier
        ssize_t bytes_read = fread(buffer + 4, 1, 512, file);
        if (bytes_read  <=  0) {
            // Fin du fichier, arrêter l'envoi
            break;
        }
        
        // Préparation du paquet de données
        *(uint16_t*)buffer = htons(TFTP_OPCODE_DATA);
        *(uint16_t*)(buffer + 2) = htons(block_num);
        
        // Envoi du paquet de données
        ssize_t bytes_sent = sendto(sockfd, buffer, bytes_read + 4, 0, (struct sockaddr*)server_addr, server_len);
        if (bytes_sent == -1) {
            perror("Erreur lors de l'envoi du paquet de données");
            exit(EXIT_FAILURE);
        }
        
        printf("Paquet DATA [%d] envoyé.\n", block_num);
        
        // Attendre le paquet de réponse du serveur
        ssize_t recvlen;
        while (1) {
            recvlen = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)server_addr, &server_len);
            if (recvlen > 0) {
                // Vérifier le type de paquet reçu
                if (*(uint16_t*)buffer == htons(TFTP_OPCODE_ACK) && *(uint16_t*)(buffer + 2) == htons(block_num)) {
                    // Paquet ACK reçu pour le bloc attendu
                    printf("ACK [%d] reçu.\n", block_num);
                    break;
                } else if (*(uint16_t*)buffer == htons(TFTP_OPCODE_ERR)) {
                    // Paquet d'erreur reçu
                    printf("Paquet d'erreur reçu.\n");
                    exit(EXIT_FAILURE);
                } else {
                    // Paquet inattendu, ignorer et continuer à attendre
                    printf("Paquet inattendu reçu, en attente de l'ACK attendu...\n");
                }
            } else if (recvlen == -1) {
                // Timeout, retransmission du paquet de données
                if (retryCount < MAX_RETRIES) {
                    printf("Timeout, retransmission du paquet DATA [%d].\n", block_num);
                    bytes_sent = sendto(sockfd, buffer, bytes_read + 4, 0, (struct sockaddr*)server_addr, server_len);
                    if (bytes_sent == -1) {
                        perror("Erreur lors de la retransmission du paquet de données");
                        exit(EXIT_FAILURE);
                    }
                    retryCount++;
                } else {
                    perror("Nombre maximal de tentatives atteint, abandon.");
                    exit(EXIT_FAILURE);
                }
            }
        }
        
        // Réinitialiser le compteur de tentatives de retransmission
        retryCount = 0;
        
        // Incrémenter le numéro de bloc
        block_num++;
    }
}

const char *get_filename(const char *full_path) {
    const char *last_slash = strrchr(full_path, '/');
    if (last_slash != NULL) {
        // Si un slash est trouvé, renvoie le nom du fichier après le slash
        return last_slash + 1;
    } else {
        // Pas de slash trouvé, le nom du fichier est le chemin complet lui-même
        return full_path;
    }
}