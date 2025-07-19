#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/time.h>
#include <omp.h>

#define PORT 8081
#define BUFFER_SIZE 2048
#define ind2d(i,j) (i)*(tam+2)+j

// Funções do Jogo da Vida
double wall_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return(tv.tv_sec + tv.tv_usec/1000000.0);
}

void UmaVida(int* tabulIn, int* tabulOut, int tam) {
    int i, j, vizviv;
    
    #pragma omp parallel for private(i, j, vizviv)
    for (i=1; i<=tam; i++) {
        for (j=1; j<=tam; j++) {
            vizviv = tabulIn[ind2d(i-1,j-1)] + tabulIn[ind2d(i-1,j)] +
                    tabulIn[ind2d(i-1,j+1)] + tabulIn[ind2d(i,j-1)] +
                    tabulIn[ind2d(i,j+1)] + tabulIn[ind2d(i+1,j-1)] +
                    tabulIn[ind2d(i+1,j)] + tabulIn[ind2d(i+1,j+1)];
            
            if (tabulIn[ind2d(i,j)] && vizviv < 2)
                tabulOut[ind2d(i,j)] = 0;
            else if (tabulIn[ind2d(i,j)] && vizviv > 3)
                tabulOut[ind2d(i,j)] = 0;
            else if (!tabulIn[ind2d(i,j)] && vizviv == 3)
                tabulOut[ind2d(i,j)] = 1;
            else
                tabulOut[ind2d(i,j)] = tabulIn[ind2d(i,j)];
        }
    }
}

void InitTabul(int* tabulIn, int* tabulOut, int tam) {
    int ij;
    for (ij=0; ij<(tam+2)*(tam+2); ij++) {
        tabulIn[ij] = 0;
        tabulOut[ij] = 0;
    }
    tabulIn[ind2d(1,2)] = 1; tabulIn[ind2d(2,3)] = 1;
    tabulIn[ind2d(3,1)] = 1; tabulIn[ind2d(3,2)] = 1;
    tabulIn[ind2d(3,3)] = 1;
}

int Correto(int* tabul, int tam) {
    int ij, cnt = 0;
    for (ij=0; ij<(tam+2)*(tam+2); ij++)
        cnt += tabul[ij];
    return (cnt == 5 && tabul[ind2d(tam-2,tam-1)] &&
            tabul[ind2d(tam-1,tam)] && tabul[ind2d(tam,tam-2)] &&
            tabul[ind2d(tam,tam-1)] && tabul[ind2d(tam,tam)]);
}

// Executar Jogo da Vida para um intervalo de POWMIN a POWMAX
int execute_game_of_life(int powmin, int powmax, char* result_buffer, int buffer_size) {
    int pow, i, tam, *tabulIn, *tabulOut;
    double t0, t1, t2, t3, total_time = 0.0;
    int success = 1;
    int pos = 0;
    
    pos += snprintf(result_buffer + pos, buffer_size - pos, 
                   "OpenMP Engine Results (Threads: %d):\\n", omp_get_max_threads());
    
    for (pow = powmin; pow <= powmax; pow++) {
        tam = 1 << pow;
        
        t0 = wall_time();
        tabulIn = (int*)malloc((tam+2)*(tam+2)*sizeof(int));
        tabulOut = (int*)malloc((tam+2)*(tam+2)*sizeof(int));
        
        if (!tabulIn || !tabulOut) {
            pos += snprintf(result_buffer + pos, buffer_size - pos,
                           "ERRO: Falha na alocação para tam=%d\\n", tam);
            success = 0;
            break;
        }
        
        InitTabul(tabulIn, tabulOut, tam);
        t1 = wall_time();
        
        for (i = 0; i < 2*(tam-3); i++) {
            UmaVida(tabulIn, tabulOut, tam);
            UmaVida(tabulOut, tabulIn, tam);
        }
        t2 = wall_time();
        
        int is_correct = Correto(tabulIn, tam);
        t3 = wall_time();
        
        double iteration_time = t3 - t0;
        total_time += iteration_time;
        
        pos += snprintf(result_buffer + pos, buffer_size - pos,
                       "tam=%d: %s - init=%.7f, comp=%.7f, check=%.7f, total=%.7f\\n",
                       tam, is_correct ? "CORRETO" : "ERRADO", 
                       t1-t0, t2-t1, t3-t2, iteration_time);
        
        if (!is_correct) success = 0;
        
        free(tabulIn);
        free(tabulOut);
    }
    
    pos += snprintf(result_buffer + pos, buffer_size - pos,
                   "\\nTempo total: %.6f segundos", total_time);
    
    return success;
}

// Parsear query string HTTP
void parse_query_params(char* query, int* powmin, int* powmax) {
    *powmin = 3; *powmax = 6; // defaults
    
    char* token = strtok(query, "&");
    while (token != NULL) {
        if (strncmp(token, "powmin=", 7) == 0) {
            *powmin = atoi(token + 7);
        } else if (strncmp(token, "powmax=", 7) == 0) {
            *powmax = atoi(token + 7);
        }
        token = strtok(NULL, "&");
    }
}

void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    char buffer[BUFFER_SIZE];
    char response[4096];
    char result_buffer[3072];
    
    ssize_t bytes = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        close(client_socket);
        return NULL;
    }
    
    buffer[bytes] = '\0';
    printf("Recebido: %s\n", buffer);
    
    // Verificar se é requisição HTTP GET /process
    if (strncmp(buffer, "GET /process", 12) == 0) {
        char* query_start = strstr(buffer, "?");
        int powmin = 3, powmax = 6;
        
        if (query_start) {
            char* query_end = strstr(query_start, " HTTP");
            if (query_end) {
                *query_end = '\0';
                char query[256];
                strcpy(query, query_start + 1);
                parse_query_params(query, &powmin, &powmax);
            }
        }
        
        printf("Executando OpenMP Game of Life: POWMIN=%d, POWMAX=%d\n", powmin, powmax);
        
        double start_time = wall_time();
        int success = execute_game_of_life(powmin, powmax, result_buffer, sizeof(result_buffer));
        double processing_time = wall_time() - start_time;
        
        // Resposta HTTP JSON
        snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{"
                "\"success\":%s,"
                "\"engine\":\"OpenMP\","
                "\"powmin\":%d,"
                "\"powmax\":%d,"
                "\"processing_time\":%.6f,"
                "\"threads\":%d,"
                "\"details\":\"%s\""
                "}",
                success ? "true" : "false",
                powmin, powmax, processing_time,
                omp_get_max_threads(), result_buffer);
        
        printf("Processamento concluído: %.6f segundos\n", processing_time);
    
    } else if (strncmp(buffer, "GET /health", 11) == 0) {
        // Health check
        snprintf(response, sizeof(response),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "\r\n"
                "{\"status\":\"healthy\",\"engine\":\"OpenMP\",\"threads\":%d}",
                omp_get_max_threads());
    
    } else {
        // 404
        snprintf(response, sizeof(response),
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: application/json\r\n"
                "\r\n"
                "{\"error\":\"Not found\",\"endpoints\":[\"/process?powmin=X&powmax=Y\",\"/health\"]}");
    }
    
    send(client_socket, response, strlen(response), 0);
    close(client_socket);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    printf("OpenMP HTTP Engine iniciando na porta %d...\n", PORT);
    printf("Threads OpenMP disponíveis: %d\n", omp_get_max_threads());
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Erro ao criar socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro no bind");
        return 1;
    }
    
    if (listen(server_socket, 10) < 0) {
        perror("Erro no listen");
        return 1;
    }
    
    printf("OpenMP Engine aguardando requisições HTTP na porta %d...\n", PORT);
    printf("Endpoints: /process?powmin=X&powmax=Y, /health\n");
    
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) continue;
        
        int* client_sock = malloc(sizeof(int));
        *client_sock = client_socket;
        
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_sock);
        pthread_detach(thread);
    }
    
    close(server_socket);
    return 0;
}
