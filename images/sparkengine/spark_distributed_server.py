from flask import Flask, request, jsonify
from pyspark.sql import SparkSession
from pyspark.sql.functions import col, when, sum as spark_sum
from pyspark.sql.types import IntegerType, StructType, StructField
import time
import os
import uuid

app = Flask(__name__)

def create_distributed_spark_session():
    """Conectar ao cluster Spark distribuído REAL com AppName único"""
    
    spark_master = os.getenv('SPARK_MASTER_URL', 'spark://spark-master-service:7077')
    driver_host = os.getenv('SPARK_DRIVER_HOST', '0.0.0.0')
    
    # AppName único para cada requisição
    app_name = f"GameOfLife-{uuid.uuid4().hex[:8]}"
    
    print(f"Conectando ao cluster Spark: {spark_master}")
    
    spark = SparkSession.builder \
        .appName(app_name) \
        .config("spark.master", spark_master) \
        .config("spark.driver.host", driver_host) \
        .config("spark.driver.port", "0") \
        .config("spark.driver.bindAddress", "0.0.0.0") \
        .config("spark.executor.instances", "1") \
        .config("spark.executor.cores", "1") \
        .config("spark.executor.memory", os.getenv("SPARK_EXECUTOR_MEMORY", "400m")) \
        .config("spark.driver.memory", os.getenv("SPARK_DRIVER_MEMORY", "512m")) \
        .config("spark.sql.adaptive.enabled", "true") \
        .config("spark.sql.adaptive.coalescePartitions.enabled", "true") \
        .config("spark.default.parallelism", "4") \
        .config("spark.serializer", "org.apache.spark.serializer.KryoSerializer") \
        .getOrCreate()
    
    spark.sparkContext.setLogLevel("ERROR")
    
    # Método mais compatível para contar executors
    try:
        executors = len(spark.sparkContext.statusTracker().getExecutorInfos())
    except:
        executors = 1
    
    if executors == 0:
        executors = 1
        
    print(f"Cluster inicializado com {executors} executors conectados")
    
    return spark

def init_board(tam):
    board = [[0 for _ in range(tam+2)] for _ in range(tam+2)]
    board[1][2] = 1; board[2][3] = 1; board[3][1] = 1; board[3][2] = 1; board[3][3] = 1
    return board

def board_to_distributed_dataframe(spark, board, tam):
    print(f"Distribuindo tabuleiro {tam}x{tam} entre workers...")
    
    data = []
    for i in range(1, tam + 1):
        for j in range(1, tam + 1):
            data.append((i, j, int(board[i][j])))
    
    schema = StructType([
        StructField("row", IntegerType(), False),
        StructField("col", IntegerType(), False),
        StructField("alive", IntegerType(), False)
    ])
    
    df = spark.createDataFrame(data, schema)
    df = df.repartition(4, "row")
    print(f"DataFrame criado com {df.rdd.getNumPartitions()} partições distribuídas")
    
    return df

def create_neighbors_mapping_distributed(spark, tam):
    neighbors_data = []
    
    for i in range(1, tam + 1):
        for j in range(1, tam + 1):
            neighbors = [
                (i-1, j-1), (i-1, j), (i-1, j+1),
                (i, j-1),             (i, j+1),
                (i+1, j-1), (i+1, j), (i+1, j+1)
            ]
            for ni, nj in neighbors:
                if 1 <= ni <= tam and 1 <= nj <= tam:
                    neighbors_data.append((i, j, ni, nj))
    
    schema = StructType([
        StructField("cell_row", IntegerType(), False),
        StructField("cell_col", IntegerType(), False),
        StructField("neighbor_row", IntegerType(), False),
        StructField("neighbor_col", IntegerType(), False)
    ])
    
    neighbors_df = spark.createDataFrame(neighbors_data, schema)
    neighbors_df = neighbors_df.repartition(4, "cell_row")
    
    return neighbors_df

def execute_distributed_game_of_life(powmin, powmax):
    spark = None
    
    try:
        spark = create_distributed_spark_session()
        
        # Contar workers
        try:
            num_workers = len(spark.sparkContext.statusTracker().getExecutorInfos())
        except:
            num_workers = 1
        
        if num_workers == 0:
            num_workers = 1
            
        results = []
        total_time = 0.0
        success = True
        details = f"Spark Distributed Cluster ({num_workers} workers):\\n"
        
        for pow_val in range(powmin, powmax + 1):
            tam = 1 << pow_val
            print(f"Processando tabuleiro {tam}x{tam} no cluster distribuído")
            
            start_time = time.time()
            board = init_board(tam)
            current_df = board_to_distributed_dataframe(spark, board, tam)
            neighbors_df = create_neighbors_mapping_distributed(spark, tam)
            
            current_df.cache()
            neighbors_df.cache()
            current_df.count()
            neighbors_df.count()
            
            comp_start = time.time()
            num_generations = 2 * (tam - 3)
            
            for generation in range(num_generations):
                neighbors_with_state = neighbors_df.join(
                    current_df,
                    (neighbors_df.neighbor_row == current_df.row) & 
                    (neighbors_df.neighbor_col == current_df.col),
                    "left"
                ).select(
                    col("cell_row").alias("row"),
                    col("cell_col").alias("col"),
                    when(col("alive").isNull(), 0).otherwise(col("alive")).alias("neighbor_alive")
                )
                
                neighbor_counts = neighbors_with_state.groupBy("row", "col") \
                    .agg(spark_sum("neighbor_alive").alias("live_neighbors"))
                
                current_df = current_df.join(neighbor_counts, ["row", "col"]) \
                    .select(
                        col("row"),
                        col("col"),
                        when(
                            (col("alive") == 1) & (col("live_neighbors") < 2), 0
                        ).when(
                            (col("alive") == 1) & (col("live_neighbors") > 3), 0
                        ).when(
                            (col("alive") == 0) & (col("live_neighbors") == 3), 1
                        ).otherwise(
                            col("alive")
                        ).alias("alive")
                    )
                
                current_df.cache()
                current_df.count()
            
            comp_time = time.time() - comp_start
            final_data = current_df.collect()
            is_correct = verify_distributed_result(final_data, tam)
            
            iteration_time = time.time() - start_time
            total_time += iteration_time
            
            status = "CORRETO" if is_correct else "ERRADO"
            details += f"tam={tam}: {status} - comp={comp_time:.6f}, total={iteration_time:.6f}\\n"
            
            results.append({
                'tam': tam,
                'time': iteration_time,
                'correct': is_correct
            })
            
            if not is_correct:
                success = False
            
            print(f"Cluster: tam={tam} - {iteration_time:.6f}s ({status})")
    
    except Exception as e:
        print(f"ERRO no cluster Spark: {e}")
        return {
            'success': False,
            'total_time': 0.0,
            'results': [],
            'details': f"Erro: {str(e)}",
            'cluster_info': "Error"
        }
    
    finally:
        if spark:
            try:
                spark.stop()
            except:
                pass
    
    details += f"\\nTempo total: {total_time:.6f} segundos"
    details += f"\\nWorkers utilizados: {num_workers}"
    
    return {
        'success': success,
        'total_time': total_time,
        'results': results,
        'details': details,
        'cluster_info': f"Distributed across {num_workers} workers"
    }

def verify_distributed_result(final_data, tam):
    board = [[0 for _ in range(tam+2)] for _ in range(tam+2)]
    for row in final_data:
        i, j, alive = row['row'], row['col'], row['alive']
        board[i][j] = alive
    cnt = sum(sum(row) for row in board)
    return (cnt == 5 and board[tam-2][tam-1] and board[tam-1][tam] and 
            board[tam][tam-2] and board[tam][tam-1] and board[tam][tam])

@app.route('/process', methods=['GET'])
def process_game():
    try:
        powmin = request.args.get('powmin', type=int)
        powmax = request.args.get('powmax', type=int)
        
        if powmin is None or powmax is None:
            return jsonify({'error': 'Missing parameters'}), 400
        
        if powmin < 3 or powmax > 15 or powmin > powmax:
            return jsonify({'error': 'Invalid parameters'}), 400
        
        print(f"Processando no cluster distribuído: POWMIN={powmin}, POWMAX={powmax}")
        
        result = execute_distributed_game_of_life(powmin, powmax)
        
        return jsonify({
            'success': result['success'],
            'engine': 'Spark Distributed Cluster',
            'powmin': powmin,
            'powmax': powmax,
            'processing_time': result['total_time'],
            'details': result['details'],
            'cluster_info': result['cluster_info']
        })
        
    except Exception as e:
        return jsonify({'error': str(e), 'success': False}), 500

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'engine': 'Spark Distributed'})

@app.route('/', methods=['GET'])
def root():
    return jsonify({
        'message': 'Spark Distributed Engine Server',
        'endpoints': ['/process?powmin=X&powmax=Y', '/health']
    })

if __name__ == '__main__':
    print("Iniciando Spark Distributed Engine Server...")
    app.run(host='0.0.0.0', port=8082, debug=False, threaded=True)
