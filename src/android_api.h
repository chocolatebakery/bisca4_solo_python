#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BiscaEngineHandle BiscaEngineHandle;

typedef enum BiscaEngineType {
    BISCA_ENGINE_ALPHABETA = 0,
    BISCA_ENGINE_MCTS = 1
} BiscaEngineType;

typedef struct BiscaEngineConfig {
    BiscaEngineType type;
    const char* nnue_path;   // optional, can be null
    int depth;               // used for alpha-beta
    int iterations;          // used for MCTS
    double cpuct;            // used for MCTS
    int perfect_info;        // 0 or 1
    int root_mt;             // only for alpha-beta: enable root multi-thread
} BiscaEngineConfig;

BiscaEngineHandle* bisca_engine_create(const BiscaEngineConfig* cfg);
void bisca_engine_destroy(BiscaEngineHandle* handle);

const char* bisca_engine_status(BiscaEngineHandle* handle);

// Returns pointer to internal buffer (valid until next call).
const char* bisca_engine_new_game(BiscaEngineHandle* handle);
const char* bisca_engine_show(BiscaEngineHandle* handle);
const char* bisca_engine_play(BiscaEngineHandle* handle, int hand_index);
const char* bisca_engine_bestmove(BiscaEngineHandle* handle, int* out_index, double* out_eval);

#ifdef __cplusplus
}
#endif
