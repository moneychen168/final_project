#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include "state.hpp"
#include "config.hpp"
#include "minimax.hpp"

namespace {

constexpr int INF = P_MAX;

int piece_value(int piece){
    if(piece < 0 || piece > 6){
        return 0;
    }
    return PIECE_VALUES[piece];
}

int simple_value(int piece){
    static const int values[7] = {0, 2, 6, 7, 8, 20, 0};
    if(piece < 0 || piece > 6){
        return 0;
    }
    return values[piece];
}

int material_balance_for_side(const State* state, int side){
    int opp = 1 - side;
    int score = 0;
    for(int r = 0; r < BOARD_H; r++){
        for(int c = 0; c < BOARD_W; c++){
            score += simple_value(state->board.board[side][r][c]);
            score -= simple_value(state->board.board[opp][r][c]);
        }
    }
    return score;
}

int max_step_score(const State* state, int ply){
    int diff = material_balance_for_side(state, state->player);
    if(diff > 0){
        return P_MAX - ply;
    }
    if(diff < 0){
        return M_MAX + ply;
    }
    return 0;
}

bool is_promotion_move(const State* state, const Move& move){
    int p = state->player;
    size_t fr = move.first.first, fc = move.first.second;
    size_t tr = move.second.first;
    if(fr >= static_cast<size_t>(BOARD_H) || fc >= static_cast<size_t>(BOARD_W)){
        return false;
    }
    int piece = state->board.board[p][fr][fc];
    return piece == 1 && (tr == 0 || tr == static_cast<size_t>(BOARD_H - 1));
}

int captured_piece(const State* state, const Move& move){
    size_t tr = move.second.first, tc = move.second.second;
    if(tr >= static_cast<size_t>(BOARD_H) || tc >= static_cast<size_t>(BOARD_W)){
        return 0;
    }
    return state->board.board[1 - state->player][tr][tc];
}

bool is_noisy_move(const State* state, const Move& move){
    return captured_piece(state, move) != 0 || is_promotion_move(state, move);
}

int move_order_score(const State* state, const Move& move){
    int p = state->player;
    size_t fr = move.first.first, fc = move.first.second;
    size_t tr = move.second.first, tc = move.second.second;
    int mover = 0;
    if(fr < static_cast<size_t>(BOARD_H) && fc < static_cast<size_t>(BOARD_W)){
        mover = state->board.board[p][fr][fc];
    }

    int score = 0;
    int cap = captured_piece(state, move);
    if(cap){
        score += 100000 + piece_value(cap) * 100 - piece_value(mover);
        if(cap == 6){
            score += 1000000;
        }
    }
    if(is_promotion_move(state, move)){
        score += 50000;
    }
    if(tr < static_cast<size_t>(BOARD_H) && tc < static_cast<size_t>(BOARD_W)){
        int center_r = BOARD_H / 2;
        int center_c = BOARD_W / 2;
        score += 20 - 2 * (std::abs(static_cast<int>(tr) - center_r)
                         + std::abs(static_cast<int>(tc) - center_c));
    }
    return score;
}

std::vector<Move> ordered_moves(State* state){
    std::vector<Move> moves = state->legal_actions;
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b){
        return move_order_score(state, a) > move_order_score(state, b);
    });
    return moves;
}

int quiescence(
    State* state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p,
    int qdepth
);

int pvs_search(
    State* state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }
    if(state->game_state == DRAW){
        return 0;
    }
    if(state->step >= MAX_STEP){
        return max_step_score(state, ply);
    }

    int rep_score = 0;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }

    history.push(state->hash());

    if(depth <= 0){
        int score = p.use_quiescence
            ? quiescence(state, alpha, beta, history, ply, ctx, p, p.q_depth)
            : state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    if(state->legal_actions.empty()){
        int score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    int best = M_MAX;
    bool first_child = true;
    auto moves = ordered_moves(state);

    for(const auto& action : moves){
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw = 0;
        int score = 0;

        if(!p.use_alpha_beta){
            raw = MiniMax::eval_ctx(next, depth - 1, history, ply + 1, ctx, p);
            score = same ? raw : -raw;
        }else if(p.use_pvs && !first_child){
            raw = pvs_search(next, depth - 1, -alpha - 1, -alpha, history, ply + 1, ctx, p);
            score = same ? raw : -raw;
            if(score > alpha && score < beta){
                raw = pvs_search(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
                score = same ? raw : -raw;
            }
        }else{
            raw = pvs_search(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
            score = same ? raw : -raw;
        }

        delete next;
        first_child = false;

        if(score > best){
            best = score;
        }
        if(score > alpha){
            alpha = score;
        }
        if(p.use_alpha_beta && alpha >= beta){
            break;
        }
        if(ctx.stop){
            break;
        }
    }

    history.pop(state->hash());
    return best;
}

int quiescence(
    State* state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p,
    int qdepth
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }
    if(state->game_state == DRAW){
        return 0;
    }
    if(state->step >= MAX_STEP){
        return max_step_score(state, ply);
    }

    int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
    if(qdepth <= 0){
        return stand_pat;
    }
    if(stand_pat >= beta){
        return beta;
    }
    if(stand_pat > alpha){
        alpha = stand_pat;
    }

    auto moves = ordered_moves(state);
    for(const auto& action : moves){
        if(!is_noisy_move(state, action)){
            continue;
        }
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw = quiescence(next, -beta, -alpha, history, ply + 1, ctx, p, qdepth - 1);
        int score = same ? raw : -raw;
        delete next;

        if(score >= beta){
            return beta;
        }
        if(score > alpha){
            alpha = score;
        }
        if(ctx.stop){
            break;
        }
    }
    return alpha;
}

} // namespace


/*============================================================
 * MiniMax — eval_ctx
 *
 * Plain negamax without pruning. Caller manages memory.
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }
    if(state->game_state == DRAW){
        return 0;
    }
    if(state->step >= MAX_STEP){
        return max_step_score(state, ply);
    }

    int rep_score = 0;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    if(depth <= 0){
        int score = state->evaluate(
            p.use_kp_eval, p.use_eval_mobility, &history
        );
        history.pop(state->hash());
        return score;
    }

    if(state->legal_actions.empty()){
        int score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        history.pop(state->hash());
        return score;
    }

    int best_score = M_MAX;
    auto moves = ordered_moves(state);

    for(const auto& action : moves){
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();

        int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p);
        int score = same ? raw : -raw;

        delete next;

        if(score > best_score){
            best_score = score;
        }
        if(ctx.stop){
            break;
        }
    }

    history.pop(state->hash());
    return best_score;
}


/*============================================================
 * MiniMax — search
 *
 * Root search. By default it uses alpha-beta + PVS + quiescence,
 * while eval_ctx remains a plain minimax implementation for reference.
 *============================================================*/
SearchResult MiniMax::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    auto t0 = std::chrono::high_resolution_clock::now();
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->legal_actions.empty()){
        result.best_move = Move();
        result.score = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    if(state->game_state == WIN){
        result.best_move = state->legal_actions.front();
        result.score = P_MAX;
        result.nodes = 1;
        result.seldepth = 0;
        result.pv = {result.best_move};
        return result;
    }

    int best_score = M_MAX;
    int alpha = M_MAX;
    int beta = P_MAX;
    int move_index = 0;
    int total_moves = static_cast<int>(state->legal_actions.size());
    auto moves = ordered_moves(state);

    for(const auto& action : moves){
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw = 0;
        int score = 0;

        if(p.use_alpha_beta){
            raw = pvs_search(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
        }else{
            raw = eval_ctx(next, depth - 1, history, 1, ctx, p);
        }
        score = same ? raw : -raw;
        delete next;

        if(score > best_score){
            best_score = score;
            result.best_move = action;
            result.score = best_score;
            result.pv = {result.best_move};

            if(p.report_partial && ctx.on_root_update){
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }
        if(score > alpha){
            alpha = score;
        }
        move_index++;
        if(ctx.stop){
            break;
        }
    }

    if(result.pv.empty()){
        result.best_move = moves.front();
        result.pv = {result.best_move};
        result.score = best_score;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.depth = depth;
    result.seldepth = ctx.seldepth;
    result.nodes = ctx.nodes;
    result.time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}


/*============================================================
 * MiniMax — default_params / param_defs
 *============================================================*/
ParamMap MiniMax::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"UseAlphaBeta", "true"},
        {"UsePVS", "true"},
        {"UseQuiescence", "true"},
        {"QDepth", "4"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> MiniMax::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"UseAlphaBeta", ParamDef::CHECK, "true"},
        {"UsePVS", ParamDef::CHECK, "true"},
        {"UseQuiescence", ParamDef::CHECK, "true"},
        {"QDepth", ParamDef::SPIN, "4", 0, 8},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
