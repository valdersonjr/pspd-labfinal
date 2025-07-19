# Relatório do Projeto de Pesquisa: Construção de Aplicações de Larga Escala com Frameworks de Programação Paralela/Distribuída

**Curso:** Engenharia de Computação  
**Disciplina:** PSPD – Programação para Sistemas Paralelos e Distribuídos  
**Turma:** T2  
**Professor:** Fernando W. Cruz  
**Aluno:** Valderson Pontes da Silva Junior  
**Matrícula:** 190020521

---

## 1. Introdução

Este projeto tem como objetivo aplicar requisitos de **performance** e **elasticidade** a uma aplicação baseada no algoritmo "Jogo da Vida" de Conway, utilizando frameworks de programação paralela (OpenMP/MPI) e distribuída (Apache Spark), orquestrados através do Kubernetes.

A aplicação implementa uma arquitetura de microserviços que permite:
- Processamento paralelo local (OpenMP) vs distribuído (Spark)
- Elasticidade automática via Kubernetes 
- Coleta de métricas em tempo real (ElasticSearch + Kibana)
- Balanceamento de carga através de socket server

---

## 2. Metodologia

### 2.1 Planejamento Individual
- **Cronograma:** Desenvolvimento iterativo ao longo de 4 semanas
- **Metodologia:** Implementação incremental com testes contínuos
- **Organização:**
  - Semana 1: Setup do ambiente Kubernetes e containers
  - Semana 2: Implementação dos engines OpenMP e Spark
  - Semana 3: Integração com monitoramento e correção de bugs
  - Semana 4: Testes de performance e documentação

### 2.2 Ferramentas Utilizadas
- **Versionamento:** Git/GitHub
- **Containerização:** Docker com containerd/nerdctl
- **Orquestração:** Kubernetes (3 VMs: vm1, vm2, vm3)
- **Monitoramento:** ElasticSearch + Kibana (host externo)
- **Comunicação:** Socket server HTTP

---

## 3. Requisito de Performance

### 3.1 Engine com Apache Spark

#### Implementação
O engine Spark foi implementado usando PySpark com:
- **Arquitetura distribuída:** Spark Master + Workers + Engine
- **Processamento:** DataFrames distribuídos com particionamento por linha
- **Configurações:** 1 executor, 1 core, 500MB RAM por executor

#### Integração
- **Socket Server:** Comunicação via HTTP REST API
- **Endpoint:** `/process?powmin=X&powmax=Y`
- **IPs diretos:** Contorna problemas de DNS usando IPs fixos dos services

#### Dificuldades e Soluções
| Problema | Solução Implementada |
|----------|---------------------|
| DNS resolution nos initContainers | Uso de IPs diretos (10.97.239.134:7077) |
| Memória insuficiente nos executors | Aumento para 500MB + 600MB driver |
| Reutilização de sessões Spark | AppName único por requisição com UUID |
| LiveListenerBus stopped | Restart do pod entre execuções |

#### Testes Realizados
- **Configuração:** 1 Master + 1 Worker + 1 Engine
- **Ambiente:** Kubernetes com 3 VMs (2 cores, 6GB RAM cada)
- **Resultados:** Conectividade ✅, Processamento ⚠️ (requer restart entre execuções)

### 3.2 Engine com OpenMP/MPI

#### Implementação
- **OpenMP:** Paralelização com 4 threads nativas
- **Linguagem:** C com pthread para socket server
- **Algoritmo:** Jogo da Vida otimizado com cálculo de vizinhança paralela

#### Integração
- **Socket Server:** HTTP server em C com threads
- **Endpoint:** `/process` via parâmetros GET
- **Load Balancing:** Round-robin entre OpenMP e Spark

#### Performance
- **Threads:** 4 threads OpenMP configuráveis via OMP_NUM_THREADS
- **Réplicas:** 2 pods para alta disponibilidade
- **Anti-affinity:** Pods distribuídos entre diferentes nodes

#### Testes Realizados
```bash
# Dados reais coletados do ElasticSearch (20 métricas)
OpenMP Performance:
- Tempo médio: 0.030-0.056s 
- Consistência: 100% sucesso
- Amostras: requests 10-16

Spark Performance:  
- Tempo médio: 60.4-60.7s
- Consistência: 100% quando funciona
- Amostras: requests 7-9
- Overhead: ~1500x maior que OpenMP
```

### 3.3 Comparativo entre Spark e OpenMP/MPI

| Métrica | OpenMP | Spark | Observações |
|---------|--------|-------|-------------|
| **Tempo médio** | ~0.041s | ~60.5s | Spark 1500x mais lento |
| **Escalabilidade** | Local (4 threads) | Distribuída (multi-node) | Spark teoricamente superior |
| **Complexidade** | Baixa | Alta | OpenMP mais simples |
| **Overhead** | Mínimo | Altíssimo (JVM + rede) | Para pequenos datasets |
| **Confiabilidade** | 100% sucesso | Requer restart | OpenMP mais estável |
| **Recursos** | 256MB RAM | 1GB+ RAM | Spark 4x mais recursos |

**Conclusão:** Para o escopo do laboratório, OpenMP demonstra melhor performance e estabilidade, enquanto Spark oferece melhor escalabilidade teórica para aplicações de larga escala real.

---

## 4. Requisito de Elasticidade

### 4.1 Instalação e Configuração do Kubernetes

#### Topologia do Cluster
- **vm1:** Master node (control plane) + worker
- **vm2:** Worker node  
- **vm3:** Worker node
- **Recursos:** 2 cores + 6GB RAM por VM

#### Configurações Implementadas
```yaml
# Exemplo de deployment com recursos e health checks
resources:
  requests:
    memory: "128Mi"
    cpu: "100m"
  limits:
    memory: "256Mi" 
    cpu: "200m"
readinessProbe:
  httpGet:
    path: /health
    port: 8082
```

### 4.2 Integração do Kubernetes com a Aplicação

#### Arquitetura de Containers
- **Socket Server:** 1 pod (entry point da aplicação)
- **OpenMP Engine:** 2 pods com anti-affinity
- **Spark Master:** 1 pod (coordenador)
- **Spark Worker:** 1 pod (executor)
- **Spark Engine:** 1 pod (processamento)

#### Conectividade entre Serviços
- **ElasticSearch:** Host externo (192.168.122.1:9200)
- **Services:** ClusterIP para comunicação interna
- **NodePort:** Socket server exposto na porta 30080
- **DNS Workaround:** IPs diretos para contornar problemas de resolução

### 4.3 Testes de Tolerância a Falhas e Elasticidade

#### Teste de Auto-recovery
```bash
# Simulação de falha executada
kubectl delete pod -l app=openmp-engine -n pspd --force
# Resultado: Kubernetes recriou automaticamente em ~10s
```

#### Simulação de Carga
- **Múltiplos clientes:** Scripts de stress test com 5 clientes simultâneos
- **Resultado:** OpenMP suportou carga concorrente sem degradação
- **Monitoramento:** Métricas coletadas em tempo real

---

## 5. Banco de Dados e Visualização de Resultados

### 5.1 ElasticSearch + Kibana

#### Configuração
- **Host:** Máquina externa (192.168.122.1)
- **ElasticSearch:** 7.17.0 (single-node)
- **Kibana:** 7.17.0 com dashboards customizados
- **Índice:** `pspd-metrics`

#### Estrutura dos Documentos
```json
{
  "timestamp": "2025-07-19T20:43:25Z",
  "engine": "openmp",
  "powmin": 3,
  "powmax": 6,
  "request_id": 17,
  "success": true,
  "processing_time": 0.039956,
  "client_ip": "10.244.0.0",
  "active_clients": 1,
  "total_requests": 17
}
```

#### Métricas Coletadas
- Tempo de processamento por engine
- Taxa de sucesso/falha
- Clientes simultâneos
- Timeline de execuções
- IPs dos clientes

### 5.2 Análise de Dados

#### Dashboards Criados
1. **Tempo médio por Engine:** Gráfico de barras comparativo
2. **Taxa de sucesso:** Pie chart OpenMP vs Spark
3. **Timeline:** Linha do tempo das execuções
4. **Distribuição de carga:** Métricas de concorrência

#### Insights Obtidos
- **OpenMP:** Extremamente rápido e consistente (0.030-0.056s)
- **Spark:** Funcional mas com overhead massivo (~60s para mesma tarefa)
- **Kubernetes:** Recovery automático funcionando corretamente
- **Coleta:** 20+ métricas coletadas com timestamps precisos
- **Distribuição:** Spark adequado apenas para datasets muito grandes

---

## 6. Conclusão

### 6.1 Resultados Obtidos
- ✅ **Arquitetura distribuída** implementada com Kubernetes
- ✅ **OpenMP engine** funcionando com alta performance
- ✅ **Spark engine** implementado (com limitações de estabilidade)
- ✅ **Coleta de métricas** automatizada
- ✅ **Elasticidade** demonstrada via auto-recovery
- ✅ **Visualização** profissional via Kibana

### 6.2 Dificuldades e Soluções
| Desafio | Solução |
|---------|---------|
| DNS resolution no K8s | IPs diretos nos deployments |
| Sessões Spark conflitantes | UUID único + restart entre execuções |
| Recursos limitados | Otimização de requests/limits |
| Conectividade ElasticSearch | Host externo para economia de recursos |

### 6.3 Benefícios de Cada Abordagem

#### OpenMP/MPI
- **Vantagens:** Simplicidade, performance, estabilidade
- **Uso ideal:** Aplicações locais com paralelismo intensivo

#### Apache Spark  
- **Vantagens:** Escalabilidade, distribuição, tolerância a falhas
- **Uso ideal:** Big Data, processamento distribuído real

### 6.4 Melhorias Futuras
- Implementar Kafka como broker para maior escalabilidade
- Resolver instabilidade das sessões Spark
- Horizontal Pod Autoscaler baseado em CPU/memória
- Testes em cluster com mais recursos
- Integração com ferramentas de CI/CD

### 6.5 Comentário Pessoal

**Valderson Pontes da Silva Junior (190020521):**
- **Contribuição:** Desenvolvimento completo do laboratório individual
- **Principais atividades:**
  - Setup e configuração do cluster Kubernetes (3 VMs)
  - Implementação dos engines OpenMP e Spark
  - Resolução de problemas de DNS e conectividade 
  - Configuração do stack de monitoramento (ElasticSearch/Kibana)
  - Desenvolvimento de scripts de teste automatizados
  - Análise de performance e documentação
- **Aprendizados:** 
  - Aprofundamento significativo em Kubernetes e orquestração
  - Comparação prática entre paralelismo local vs distribuído
  - Troubleshooting avançado de problemas de rede e DNS
  - Implementação de stack de monitoramento profissional
  - Análise de trade-offs entre simplicidade e escalabilidade
- **Desafios superados:**
  - Problemas de resolução DNS resolvidos com IPs diretos
  - Instabilidade das sessões Spark contornada com restarts
  - Limitações de recursos gerenciadas com otimizações
- **Autoavaliação:** 9/10 - Projeto ambicioso implementado com sucesso, demonstrando domínio das tecnologias solicitadas

---

## 7. Anexos

### 7.1 Arquivos de Configuração
- `manifests/namespace.yaml` - Namespace do projeto
- `manifests/socketserver.yaml` - Deployment do socket server
- `manifests/spark-engine.yaml` - Cluster Spark completo  
- `manifests/openmpmpi-engine.yaml` - Engine OpenMP
- `docker-compose.yml` - ElasticSearch + Kibana

### 7.2 Scripts de Teste
- `testes/teste_basico.sh` - Conectividade básica
- `testes/teste_performance.sh` - Comparação de performance
- `testes/teste_stress.sh` - Múltiplos clientes
- `testes/teste_elasticidade.sh` - Recovery automático
- `testes/coletar_metricas.sh` - Extração de dados

### 7.3 Código Fonte
- `images/socketserver/socketserver_http.c` - Socket server com métricas
- `images/sparkengine/spark_distributed_server.py` - Engine Spark
- `images/openmpmpiengine/http_server.c` - Engine OpenMP

### 7.4 Instruções de Execução

#### Pré-requisitos
```bash
# Kubernetes cluster com 3 nodes
# Docker/containerd configurado
# Registry local: vm1:5000
```

#### Deploy da aplicação
```bash
kubectl apply -f manifests/namespace.yaml
kubectl apply -f manifests/
```

#### Testes
```bash
cd testes/
./executar_todos.sh  # Executa todos os testes
```

#### Monitoramento
- **Kibana:** http://192.168.122.1:5601
- **ElasticSearch:** http://192.168.122.1:9200

---

**GitHub Repository:** [Link do repositório se disponível]

**Observação:** Todos os arquivos necessários para reprodução estão organizados com instruções de instalação e execução detalhadas.
