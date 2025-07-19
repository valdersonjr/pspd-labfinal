#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>

#define PORT 8080
#define BUFFER_SIZE 2048
#define ELASTICSEARCH_HOST "192.168.122.1"
#define ELASTICSEARCH_PORT 9200

typedef struct {
    int socket;
    struct sockaddr_in address;
    int thread_id;
} client_data_t;

int active_clients = 0;
int total_requests = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Função para obter timestamp atual em formato ISO 8601
void get_iso_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* utc_tm = gmtime(&now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", utc_tm);
}

// Função para obter timestamp em milissegundos
long long get_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (long long)(tv.tv_usec) / 1000;
}

// Enviar métricas para ElasticSearch
int send_metrics_to_elasticsearch(const char* engine, int powmin, int powmax, 
                                 int request_id, int success, double processing_time, 
                                 const char* client_ip, const char* error_msg) {
    int sock;
    struct sockaddr_in server_addr;
    char request[2048];
    char response[1024];
    char timestamp[64];
    char json_body[1024];
    
    // Obter timestamp atual
    get_iso_timestamp(timestamp, sizeof(timestamp));
    
    // Criar JSON com métricas
    snprintf(json_body, sizeof(json_body),
        "{"
        "\"timestamp\":\"%s\","
        "\"engine\":\"%s\","
        "\"powmin\":%d,"
        "\"powmax\":%d,"
        "\"request_id\":%d,"
        "\"success\":%s,"
        "\"processing_time\":%.6f,"
        "\"client_ip\":\"%s\","
        "\"active_clients\":%d,"
        "\"total_requests\":%d"
        "%s%s%s"
        "}",
        timestamp, engine, powmin, powmax, request_id,
        success ? "true" : "false", processing_time, client_ip,
        active_clients, total_requests,
        error_msg ? ",\"error\":\"" : "",
        error_msg ? error_msg : "",
        error_msg ? "\"" : ""
    );
    
    // Criar socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("ERRO: Falha ao criar socket para ElasticSearch\n");
        return 0;
    }
    
    // Configurar endereço do ElasticSearch
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ELASTICSEARCH_PORT);
    
    if (inet_aton(ELASTICSEARCH_HOST, &server_addr.sin_addr) == 0) {
        printf("ERRO: IP inválido do ElasticSearch: %s\n", ELASTICSEARCH_HOST);
        close(sock);
        return 0;
    }
    
    // Conectar ao ElasticSearch
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("AVISO: Falha ao conectar no ElasticSearch %s:%d\n", ELASTICSEARCH_HOST, ELASTICSEARCH_PORT);
        close(sock);
        return 0;
    }
    
    // Montar requisição HTTP POST
    snprintf(request, sizeof(request),
        "POST /pspd-metrics/_doc HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        ELASTICSEARCH_HOST, ELASTICSEARCH_PORT, strlen(json_body), json_body);
    
    // Enviar requisição
    if (send(sock, request, strlen(request), 0) < 0) {
        printf("ERRO: Falha ao enviar métricas para ElasticSearch\n");
        close(sock);
        return 0;
    }
    
    // Receber resposta (opcional)
    int bytes_received = recv(sock, response, sizeof(response) - 1, 0);
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        // Verificar se foi sucesso (status 200 ou 201)
        if (strstr(response, "HTTP/1.1 20") != NULL) {
            printf("Métricas enviadas para ElasticSearch com sucesso\n");
        }
    }
    
    close(sock);
    return 1;
}

// Fazer requisição HTTP para engine
int call_engine_http(const char* service_host, int service_port, const char* path, 
                     int powmin, int powmax, char* response_buffer, int buffer_size) {
    int sock;
    struct sockaddr_in server_addr;
    char request[512];
    char response[4096];
    int bytes_received;
    
    // Criar socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(response_buffer, buffer_size, "ERRO: Falha ao criar socket para engine");
        return 0;
    }
    
    // Configurar endereço do engine
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(service_port);
    
    // Converter IP direto (sem DNS)
    if (inet_aton(service_host, &server_addr.sin_addr) == 0) {
        snprintf(response_buffer, buffer_size, "ERRO: IP inválido %s", service_host);
        close(sock);
        return 0;
    }
    
    // Conectar ao engine
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        snprintf(response_buffer, buffer_size, "ERRO: Falha ao conectar em %s:%d", service_host, service_port);
        close(sock);
        return 0;
    }
    
    // Montar requisição HTTP GET
    snprintf(request, sizeof(request),
            "GET %s?powmin=%d&powmax=%d HTTP/1.1\r\n"
            "Host: %s:%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, powmin, powmax, service_host, service_port);
    
    // Enviar requisição
    if (send(sock, request, strlen(request), 0) < 0) {
        snprintf(response_buffer, buffer_size, "ERRO: Falha ao enviar requisição para engine");
        close(sock);
        return 0;
    }
    
    // Receber resposta
    bytes_received = recv(sock, response, sizeof(response) - 1, 0);
    close(sock);
    
    if (bytes_received <= 0) {
        snprintf(response_buffer, buffer_size, "ERRO: Nenhuma resposta do engine");
        return 0;
    }
    
    response[bytes_received] = '\0';
    
    // Encontrar o JSON na resposta (após headers HTTP)
    char* json_start = strstr(response, "\r\n\r\n");
    if (json_start) {
        json_start += 4; // Pular \r\n\r\n
        strncpy(response_buffer, json_start, buffer_size - 1);
        response_buffer[buffer_size - 1] = '\0';
        return 1;
    } else {
        snprintf(response_buffer, buffer_size, "ERRO: Resposta inválida do engine");
        return 0;
    }
}

void* handle_client(void* arg) {
    client_data_t* client = (client_data_t*)arg;
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char engine_response[1024];
    char client_ip[INET_ADDRSTRLEN];
    
    // Obter IP do cliente
    strcpy(client_ip, inet_ntoa(client->address.sin_addr));
    
    // Incrementar contador de clientes ativos
    pthread_mutex_lock(&stats_mutex);
    active_clients++;
    total_requests++;
    int request_id = total_requests;
    pthread_mutex_unlock(&stats_mutex);
    
    // Timestamp início do processamento
    long long start_time = get_timestamp_ms();
    
    printf("Cliente %d conectado de %s\n", request_id, client_ip);
    
    // Receber dados do cliente
    ssize_t bytes_received = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        printf("Erro ao receber dados do cliente %d\n", request_id);
        goto cleanup;
    }
    
    buffer[bytes_received] = '\0';
    printf("Cliente %d enviou: %s\n", request_id, buffer);
    
    // Parsear POWMIN, POWMAX e ENGINE
    int powmin, powmax;
    char engine_type[32] = "auto";
    
    int parsed = sscanf(buffer, "%d %d %s", &powmin, &powmax, engine_type);
    if (parsed < 2) {
        const char* error_msg = "Formato inválido de entrada";
        send_metrics_to_elasticsearch("unknown", 0, 0, request_id, 0, 0.0, client_ip, error_msg);
        
        strcpy(response, "ERRO: Formato inválido. Use: <POWMIN> <POWMAX> [engine]\n"
                        "Exemplo: 3 6 spark\n"
                        "Engines disponíveis: openmp, spark, auto");
        send(client->socket, response, strlen(response), 0);
        goto cleanup;
    }
    
    // Validar parâmetros
    if (powmin < 3 || powmax > 15 || powmin > powmax) {
        const char* error_msg = "Parâmetros inválidos";
        send_metrics_to_elasticsearch(engine_type, powmin, powmax, request_id, 0, 0.0, client_ip, error_msg);
        
        strcpy(response, "ERRO: POWMIN deve estar entre 3-15 e POWMIN <= POWMAX");
        send(client->socket, response, strlen(response), 0);
        goto cleanup;
    }
    
    // Determinar qual engine usar - USANDO IPs DIRETOS
    const char* service_host;
    int service_port;
    
    if (strcmp(engine_type, "openmp") == 0) {
        service_host = "10.108.84.193";  // IP do openmpmpi-service
        service_port = 8081;
    } else if (strcmp(engine_type, "spark") == 0) {
        service_host = "10.101.15.95";   // IP do spark-service
        service_port = 8082;
    } else {
        // Se não especificado ou "auto", alternar entre engines (round-robin)
        static int engine_counter = 0;
        if ((engine_counter++ % 2) == 0) {
            service_host = "10.108.84.193";  // IP do openmpmpi-service
            service_port = 8081;
            strcpy(engine_type, "openmp");
        } else {
            service_host = "10.101.15.95";   // IP do spark-service
            service_port = 8082;
            strcpy(engine_type, "spark");
        }
    }
    
    printf("Cliente %d: Redirecionando para engine %s (%s:%d) - POWMIN=%d, POWMAX=%d\n", 
           request_id, engine_type, service_host, service_port, powmin, powmax);
    
    // Chamar engine apropriado
    int success = call_engine_http(service_host, service_port, "/process", 
                                  powmin, powmax, engine_response, sizeof(engine_response));
    
    // Calcular tempo de processamento
    long long end_time = get_timestamp_ms();
    double processing_time = (end_time - start_time) / 1000.0; // em segundos
    
    // Enviar métricas para ElasticSearch
    send_metrics_to_elasticsearch(engine_type, powmin, powmax, request_id, success, 
                                 processing_time, client_ip, success ? NULL : engine_response);
    
    if (success) {
        snprintf(response, BUFFER_SIZE, 
                "=== PSPD - Resultado do Processamento ===\n"
                "Engine: %s\n"
                "POWMIN: %d, POWMAX: %d\n"
                "Request ID: %d\n"
                "Status: SUCESSO\n\n"
                "Detalhes:\n%s\n"
                "=========================================",
                engine_type, powmin, powmax, request_id, engine_response);
    } else {
        snprintf(response, BUFFER_SIZE,
                "=== PSPD - Erro no Processamento ===\n"
                "Engine: %s\n"
                "POWMIN: %d, POWMAX: %d\n"
                "Request ID: %d\n"
                "Status: FALHA\n\n"
                "Erro: %s\n"
                "===================================",
                engine_type, powmin, powmax, request_id, engine_response);
    }
    
    // Enviar resposta para cliente
    send(client->socket, response, strlen(response), 0);
    
    printf("Cliente %d: Processamento finalizado (%s) - %.3fs\n", 
           request_id, success ? "SUCESSO" : "FALHA", processing_time);

cleanup:
    // Decrementar contador de clientes ativos
    pthread_mutex_lock(&stats_mutex);
    active_clients--;
    pthread_mutex_unlock(&stats_mutex);
    
    close(client->socket);
    free(client);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    pthread_t thread_id;
    
    printf("Inicializando Socket Server HTTP na porta %d...\n", PORT);
    printf("ElasticSearch configurado para: %s:%d\n", ELASTICSEARCH_HOST, ELASTICSEARCH_PORT);
    
    // Criar socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Erro ao criar socket");
        exit(1);
    }
    
    // Configurar socket para reutilizar endereço
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configurar endereço do servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro no bind");
        close(server_socket);
        exit(1);
    }
    
    // Listen
    if (listen(server_socket, 50) < 0) {
        perror("Erro no listen");
        close(server_socket);
        exit(1);
    }
    
    printf("Socket Server aguardando conexões na porta %d...\n", PORT);
    printf("Engines disponíveis:\n");
    printf("  OpenMP: 10.108.84.193:8081\n");
    printf("  Spark:  10.101.15.95:8082\n");
    printf("\nFormato de entrada: <POWMIN> <POWMAX> [engine]\n");
    printf("Exemplo: 3 6 spark\n\n");
    
    // Loop principal - aceitar conexões
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Erro ao aceitar conexão");
            continue;
        }
        
        // Criar dados do cliente
        client_data_t* client_data = malloc(sizeof(client_data_t));
        client_data->socket = client_socket;
        client_data->address = client_addr;
        
        // Criar thread para lidar com cliente
        if (pthread_create(&thread_id, NULL, handle_client, client_data) != 0) {
            perror("Erro ao criar thread");
            close(client_socket);
            free(client_data);
        }
        
        // Detach thread para limpeza automática
        pthread_detach(thread_id);
    }
    
    close(server_socket);
    return 0;
}
