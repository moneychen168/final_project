#pragma once
#include "search_types.hpp"
#include "game_history.hpp"

struct MMParams {
    bool use_kp_eval = true;
    bool use_eval_mobility = true;
    bool use_alpha_beta = true;
    bool use_pvs = true;
    bool use_quiescence = true;
    bool report_partial = true;
    int q_depth = 4;

    static MMParams from_map(const ParamMap& m){
        MMParams p;
        p.use_kp_eval       = param_bool(m, "UseKPEval", true);
        p.use_eval_mobility = param_bool(m, "UseEvalMobility", true);
        p.use_alpha_beta    = param_bool(m, "UseAlphaBeta", true);
        p.use_pvs           = param_bool(m, "UsePVS", true);
        p.use_quiescence    = param_bool(m, "UseQuiescence", true);
        p.report_partial    = param_bool(m, "ReportPartial", true);
        p.q_depth           = param_int(m, "QDepth", 4);
        if(p.q_depth < 0){ p.q_depth = 0; }
        if(p.q_depth > 8){ p.q_depth = 8; }
        return p;
    }
};

class MiniMax{
public:
    static int eval_ctx(
        State *state,
        int depth,
        GameHistory& history,
        int ply,
        SearchContext& ctx,
        const MMParams& p
    );
    static SearchResult search(
        State *state,
        int depth,
        GameHistory& history,
        SearchContext& ctx
    );

    static ParamMap default_params();
    static std::vector<ParamDef> param_defs();
};
