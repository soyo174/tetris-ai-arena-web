// TETRA ARENA C++20 Strong Baseline Bot ("C4W + Grandmaster Hybrid", v0.5.3).
// Single-file, dependency-free, ISO C++20 (g++ -std=c++20 -O2 / MSVC /std:c++20 /O2).
// Architecture:
//   1. Phase 1 (C4W_BUILD): Ultra-clean Center 4-Wide (cols 3..6 open, cols 0..2 & 7..9 stacked
//      with 3-residual seed in center) — keeps spawn zone completely safe while charging a 30-45 line cannon.
//   2. Phase 2 (C4W_FIRE): 6-7 ply 3-residual combo continuation engine (prioritizes 1-line combo
//      extension + golden 3-residual state -> unleashes 12-18 combos / 30-50 lines in ~5 seconds!).
//   3. Phase 3 (GRANDMASTER): Fast 6-7 ply 7-Bag B2B TSD/TST/Tetris + 2-layer downstack conversion
//      + exact v0.5.3 pre-lock overtime re-simulation + spawn-zone overtime survival.
//   4. Speed-engineered for v0.5.3 cumulative timing (16.67 ms = 1 frame): generation-stamped Dial
//      Dijkstra, O(1) column-height drop, zero-heap flat Zobrist dedup (~3-8 ms/move!).

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tetra {

using Clock = std::chrono::steady_clock;

constexpr uint16_t FULL_ROW = 0x3FFu;
constexpr uint16_t LEFT_MASK   = 0x007u; // cols 0,1,2
constexpr uint16_t CENTER_MASK = 0x078u; // cols 3,4,5,6
constexpr uint16_t RIGHT_MASK  = 0x380u; // cols 7,8,9
constexpr uint16_t SIDES_MASK  = 0x387u; // cols 0,1,2,7,8,9
constexpr int BOARD_W = 10;
constexpr int BOARD_H = 24;
constexpr int NUM_PIECES = 7;

enum Piece : int8_t { P_NONE = -1, P_I = 0, P_O = 1, P_T = 2, P_S = 3, P_Z = 4, P_J = 5, P_L = 6 };
enum SpinKind : uint8_t { SPIN_NONE = 0, SPIN_MINI = 1, SPIN_FULL = 2 };
enum Cmd : uint8_t { CMD_LEFT = 0, CMD_RIGHT = 1, CMD_DOWN = 2, CMD_CW = 3, CMD_CCW = 4, CMD_ROT180 = 5, CMD_HARD_DROP = 6 };
enum PlayMode : uint8_t { MODE_GRANDMASTER = 0, MODE_C4W_BUILD = 1, MODE_C4W_FIRE = 2 };

constexpr const char* CMD_NAMES[7] = {
    "LEFT", "RIGHT", "DOWN", "CW", "CCW", "ROT180", "HARD_DROP"
};

inline int8_t char_to_piece(char c) {
    switch (c) {
        case 'I': return P_I;
        case 'O': return P_O;
        case 'T': return P_T;
        case 'S': return P_S;
        case 'Z': return P_Z;
        case 'J': return P_J;
        case 'L': return P_L;
        default: return P_NONE;
    }
}

// ------------------------- Minimal Fast JSON Parser -------------------------

struct Json {
    enum Type : uint8_t { NUL, BOOL, NUM, STR, ARR, OBJ };
    Type type = NUL;
    bool bval = false;
    double nval = 0.0;
    int64_t ival = 0;
    std::string sval;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json* get(std::string_view key) const {
        if (type != OBJ) return nullptr;
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    int as_int(int def = 0) const { return type == NUM ? static_cast<int>(ival) : def; }
    bool as_bool(bool def = false) const { return type == BOOL ? bval : def; }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view src) : s_(src) {}
    bool parse(Json& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return pos_ == s_.size();
    }
private:
    std::string_view s_;
    size_t pos_ = 0;
    void skip_ws() { while (pos_ < s_.size() && static_cast<unsigned char>(s_[pos_]) <= ' ') ++pos_; }

    bool parse_value(Json& out) {
        skip_ws();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return parse_obj(out);
        if (c == '[') return parse_arr(out);
        if (c == '"') { out.type = Json::STR; return parse_str(out.sval); }
        if (c == 't' && s_.substr(pos_, 4) == "true") { pos_ += 4; out.type = Json::BOOL; out.bval = true; return true; }
        if (c == 'f' && s_.substr(pos_, 5) == "false") { pos_ += 5; out.type = Json::BOOL; out.bval = false; return true; }
        if (c == 'n' && s_.substr(pos_, 4) == "null") { pos_ += 4; out.type = Json::NUL; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_num(out);
        return false;
    }

    bool parse_str(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_; out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                    case '"': case '\\': case '/': out.push_back(e); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) return false;
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = s_[pos_++]; cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(10 + h - 'a');
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(10 + h - 'A');
                            else return false;
                        }
                        if (cp < 0x80) out.push_back(static_cast<char>(cp));
                        else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: return false;
                }
            } else out.push_back(c);
        }
        return false;
    }

    bool parse_num(Json& out) {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return false;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        bool is_float = false;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            is_float = true; ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            is_float = true; ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        std::string tmp(s_.substr(start, pos_ - start));
        out.type = Json::NUM;
        if (!is_float) {
            out.ival = std::strtoll(tmp.c_str(), nullptr, 10);
            out.nval = static_cast<double>(out.ival);
        } else {
            out.nval = std::strtod(tmp.c_str(), nullptr);
            out.ival = static_cast<int64_t>(out.nval);
        }
        return true;
    }

    bool parse_arr(Json& out) {
        ++pos_; out.type = Json::ARR; skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (pos_ < s_.size()) {
            Json elem;
            if (!parse_value(elem)) return false;
            out.arr.push_back(std::move(elem));
            skip_ws();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ']') { ++pos_; return true; }
            if (s_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }

    bool parse_obj(Json& out) {
        ++pos_; out.type = Json::OBJ; skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (pos_ < s_.size()) {
            skip_ws(); std::string key;
            if (!parse_str(key)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_; Json val;
            if (!parse_value(val)) return false;
            out.obj.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == '}') { ++pos_; return true; }
            if (s_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }
};

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

// ------------------------- Rules & Geometry -------------------------

struct Offset { int8_t dx = 0; int8_t dy = 0; };
struct KickList { std::array<Offset, 8> data{}; uint8_t count = 0; };

struct RulesConfig {
    bool is_realtime = true;
    bool has_delayed_locks = true;
    bool has_scheduled_overtime = true;
    bool has_cumulative_thinking = true;
    std::array<int, 5> normal_attack = {0, 0, 1, 2, 4};
    std::array<int, 4> tspin_attack = {0, 2, 4, 6};
    int b2b_bonus = 1;
    int perfect_clear_floor = 10;
    int max_commands = 512;
    int garbage_cap = 8;
    int fps = 60;
    int move_frames = 2;
    int rotate_frames = 1;
    int soft_drop_frames = 2;
    int hold_frames = 2;
    int lock_frames = 8;
    int clear_frames = 12;
    int special_clear_frames = 16;
    int free_think_ms = 0;
    int max_think_frames = 6;
    int garbage_delay_frames = 30;
    int combo_block_limit = 3;
    int overtime_first_frame = 3600;
    int overtime_interval_frames = 240;
    int max_frames = 10800;
    int overtime_first_turn = 150;
    int overtime_interval_turn = 8;

    std::array<int8_t, NUM_PIECES> spawn_x = {3, 3, 3, 3, 3, 3, 3};
    std::array<int8_t, NUM_PIECES> spawn_y = {18, 19, 19, 19, 19, 19, 19};
    std::array<std::array<std::array<Offset, 4>, 4>, NUM_PIECES> cells{};
    std::array<std::array<int8_t, 4>, NUM_PIECES> min_dx{}, max_dx{}, min_dy{}, max_dy{};
    std::array<std::array<std::array<KickList, 2>, 4>, NUM_PIECES> kicks90{};
    std::array<KickList, 4> kicks180{};
    struct SpawnMask {
        std::array<std::pair<uint8_t, uint16_t>, 4> rows{};
        uint8_t count = 0;
        bool valid = true;
    };
    std::array<SpawnMask, NUM_PIECES> spawn_masks{};
    std::array<bool, NUM_PIECES> unique_rot1{};

    RulesConfig() { init_defaults(); }

    void init_defaults() {
        constexpr Offset spawn_cells[NUM_PIECES][4] = {
            {{0,2},{1,2},{2,2},{3,2}}, // I
            {{1,1},{2,1},{1,2},{2,2}}, // O
            {{0,1},{1,1},{2,1},{1,2}}, // T
            {{0,1},{1,1},{1,2},{2,2}}, // S
            {{1,1},{2,1},{0,2},{1,2}}, // Z
            {{0,1},{1,1},{2,1},{0,2}}, // J
            {{0,1},{1,1},{2,1},{2,2}}, // L
        };
        for (int p = 0; p < NUM_PIECES; ++p) {
            for (int i = 0; i < 4; ++i) cells[p][0][i] = spawn_cells[p][i];
            int pivot2 = (p == P_I || p == P_O) ? 3 : 2;
            for (int r = 1; r < 4; ++r) {
                for (int i = 0; i < 4; ++i) {
                    if (p == P_O) cells[p][r][i] = spawn_cells[p][i];
                    else {
                        auto prev = cells[p][r - 1][i];
                        cells[p][r][i] = {prev.dy, static_cast<int8_t>(pivot2 - prev.dx)};
                    }
                }
            }
        }
        auto set_k = [](KickList& kl, std::initializer_list<Offset> list) {
            kl.count = 0;
            for (auto o : list) if (kl.count < 8) kl.data[kl.count++] = o;
        };
        for (int p = 0; p < NUM_PIECES; ++p) {
            if (p == P_O) {
                for (int r = 0; r < 4; ++r) { set_k(kicks90[p][r][0], {{0,0}}); set_k(kicks90[p][r][1], {{0,0}}); }
            } else if (p == P_I) {
                set_k(kicks90[p][0][0], {{0,0},{-2,0},{1,0},{-2,-1},{1,2}});
                set_k(kicks90[p][1][1], {{0,0},{2,0},{-1,0},{2,1},{-1,-2}});
                set_k(kicks90[p][1][0], {{0,0},{-1,0},{2,0},{-1,2},{2,-1}});
                set_k(kicks90[p][2][1], {{0,0},{1,0},{-2,0},{1,-2},{-2,1}});
                set_k(kicks90[p][2][0], {{0,0},{2,0},{-1,0},{2,1},{-1,-2}});
                set_k(kicks90[p][3][1], {{0,0},{-2,0},{1,0},{-2,-1},{1,2}});
                set_k(kicks90[p][3][0], {{0,0},{1,0},{-2,0},{1,-2},{-2,1}});
                set_k(kicks90[p][0][1], {{0,0},{-1,0},{2,0},{-1,2},{2,-1}});
            } else {
                set_k(kicks90[p][0][0], {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}});
                set_k(kicks90[p][1][1], {{0,0},{1,0},{1,-1},{0,2},{1,2}});
                set_k(kicks90[p][1][0], {{0,0},{1,0},{1,-1},{0,2},{1,2}});
                set_k(kicks90[p][2][1], {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}});
                set_k(kicks90[p][2][0], {{0,0},{1,0},{1,1},{0,-2},{1,-2}});
                set_k(kicks90[p][3][1], {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}});
                set_k(kicks90[p][3][0], {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}});
                set_k(kicks90[p][0][1], {{0,0},{1,0},{1,1},{0,-2},{1,-2}});
            }
        }
        set_k(kicks180[0], {{0,0},{0,1},{1,1},{-1,1},{1,0},{-1,0}});
        set_k(kicks180[2], {{0,0},{0,-1},{-1,-1},{1,-1},{-1,0},{1,0}});
        set_k(kicks180[1], {{0,0},{1,0},{1,2},{1,1},{0,2},{0,1}});
        set_k(kicks180[3], {{0,0},{-1,0},{-1,2},{-1,1},{0,2},{0,1}});
        recompute_derived();
    }

    void recompute_derived() {
        for (int p = 0; p < NUM_PIECES; ++p) {
            for (int r = 0; r < 4; ++r) {
                int8_t x0 = 127, x1 = -127, y0 = 127, y1 = -127;
                for (int i = 0; i < 4; ++i) {
                    x0 = std::min(x0, cells[p][r][i].dx); x1 = std::max(x1, cells[p][r][i].dx);
                    y0 = std::min(y0, cells[p][r][i].dy); y1 = std::max(y1, cells[p][r][i].dy);
                }
                min_dx[p][r] = x0; max_dx[p][r] = x1; min_dy[p][r] = y0; max_dy[p][r] = y1;
            }
            bool same = true;
            auto norm = [&](int r) {
                std::array<int, 4> a;
                for (int i = 0; i < 4; ++i) a[i] = cells[p][r][i].dy * 8 + cells[p][r][i].dx;
                std::sort(a.begin(), a.end());
                return a;
            };
            auto r0 = norm(0);
            for (int r = 1; r < 4; ++r) if (norm(r) != r0) same = false;
            unique_rot1[p] = same;

            SpawnMask sm;
            uint16_t row_bits[BOARD_H] = {};
            for (int i = 0; i < 4; ++i) {
                int cx = spawn_x[p] + cells[p][0][i].dx;
                int cy = spawn_y[p] + cells[p][0][i].dy;
                if (cx < 0 || cx >= BOARD_W || cy < 0 || cy >= BOARD_H) { sm.valid = false; break; }
                row_bits[cy] |= static_cast<uint16_t>(1u << cx);
            }
            if (sm.valid) {
                for (int y = 0; y < BOARD_H; ++y)
                    if (row_bits[y]) sm.rows[sm.count++] = {static_cast<uint8_t>(y), row_bits[y]};
            }
            spawn_masks[p] = sm;
        }
    }

    void load_from_json(const Json* rules, const Json* geom) {
        if (rules && rules->type == Json::OBJ) {
            if (auto* v = rules->get("overtime_first_frame")) {
                is_realtime = true;
                overtime_first_frame = v->as_int(3600);
            } else {
                is_realtime = false;
            }
            if (auto* v = rules->get("profile"); v && v->type == Json::STR) {
                has_delayed_locks = is_realtime && (v->sval != "reference-realtime-v5");
                has_scheduled_overtime = has_delayed_locks && (v->sval != "reference-realtime-v5.1");
                has_cumulative_thinking = has_scheduled_overtime && (v->sval != "reference-realtime-v5.2");
            } else {
                has_delayed_locks = is_realtime;
                has_scheduled_overtime = is_realtime;
                has_cumulative_thinking = is_realtime && (rules->get("think_ms_per_frame") == nullptr);
            }
            if (auto* v = rules->get("normal_attack"); v && v->type == Json::ARR)
                for (size_t i = 0; i < 5 && i < v->arr.size(); ++i) normal_attack[i] = v->arr[i].as_int();
            if (auto* v = rules->get("tspin_attack"); v && v->type == Json::ARR)
                for (size_t i = 0; i < 4 && i < v->arr.size(); ++i) tspin_attack[i] = v->arr[i].as_int();
            if (auto* v = rules->get("b2b_bonus")) b2b_bonus = v->as_int(1);
            if (auto* v = rules->get("perfect_clear_floor")) perfect_clear_floor = v->as_int(10);
            if (auto* v = rules->get("max_commands")) max_commands = v->as_int(512);
            if (auto* v = rules->get("garbage_cap")) garbage_cap = (v->type == Json::NUM) ? v->as_int(8) : -1;
            else garbage_cap = -1;
            if (auto* v = rules->get("fps")) fps = v->as_int(60);
            if (auto* v = rules->get("move_frames")) move_frames = v->as_int(2);
            if (auto* v = rules->get("rotate_frames")) rotate_frames = v->as_int(1);
            if (auto* v = rules->get("soft_drop_frames")) soft_drop_frames = v->as_int(2);
            if (auto* v = rules->get("hold_frames")) hold_frames = v->as_int(2);
            if (auto* v = rules->get("lock_frames")) lock_frames = v->as_int(8);
            if (auto* v = rules->get("clear_frames")) clear_frames = v->as_int(12);
            if (auto* v = rules->get("special_clear_frames")) special_clear_frames = v->as_int(16);
            if (auto* v = rules->get("free_think_ms")) free_think_ms = v->as_int(0);
            if (auto* v = rules->get("max_think_frames")) max_think_frames = v->as_int(6);
            if (auto* v = rules->get("garbage_delay_frames")) garbage_delay_frames = v->as_int(30);
            if (auto* v = rules->get("combo_block_limit")) combo_block_limit = v->as_int(3);
            if (auto* v = rules->get("overtime_interval_frames")) overtime_interval_frames = v->as_int(240);
            if (auto* v = rules->get("max_frames")) max_frames = v->as_int(10800);
            if (auto* v = rules->get("overtime_first")) overtime_first_turn = v->as_int(150);
            if (auto* v = rules->get("overtime_interval")) overtime_interval_turn = v->as_int(8);
            if (auto* so = rules->get("spawn_origins"); so && so->type == Json::OBJ) {
                for (const auto& kv : so->obj) {
                    if (kv.first.size() == 1 && kv.second.type == Json::ARR && kv.second.arr.size() == 2) {
                        int8_t p = char_to_piece(kv.first[0]);
                        if (p >= 0) {
                            spawn_x[p] = static_cast<int8_t>(kv.second.arr[0].as_int());
                            spawn_y[p] = static_cast<int8_t>(kv.second.arr[1].as_int());
                        }
                    }
                }
            }
        }
        if (geom && geom->type == Json::OBJ) {
            auto load_kicks = [](const Json& arr, KickList& kl) {
                kl.count = 0;
                if (arr.type != Json::ARR) return;
                for (const auto& pair : arr.arr) {
                    if (pair.type == Json::ARR && pair.arr.size() == 2 && kl.count < 8) {
                        kl.data[kl.count++] = {static_cast<int8_t>(pair.arr[0].as_int()),
                                               static_cast<int8_t>(pair.arr[1].as_int())};
                    }
                }
            };
            if (auto* cobj = geom->get("cells"); cobj && cobj->type == Json::OBJ) {
                for (const auto& kv : cobj->obj) {
                    if (kv.first.size() != 1 || kv.second.type != Json::ARR) continue;
                    int8_t p = char_to_piece(kv.first[0]);
                    if (p < 0) continue;
                    for (int r = 0; r < 4 && static_cast<size_t>(r) < kv.second.arr.size(); ++r) {
                        const auto& rarr = kv.second.arr[r];
                        for (int i = 0; i < 4 && static_cast<size_t>(i) < rarr.arr.size(); ++i) {
                            cells[p][r][i] = {static_cast<int8_t>(rarr.arr[i].arr[0].as_int()),
                                              static_cast<int8_t>(rarr.arr[i].arr[1].as_int())};
                        }
                    }
                }
            }
            if (auto* k90 = geom->get("kicks_90"); k90 && k90->type == Json::OBJ) {
                for (const auto& kv : k90->obj) {
                    if (kv.first.size() != 1 || kv.second.type != Json::OBJ) continue;
                    int8_t p = char_to_piece(kv.first[0]);
                    if (p < 0) continue;
                    for (int r = 0; r < 4; ++r) {
                        std::string cw = std::to_string(r) + ":" + std::to_string((r + 1) & 3);
                        std::string ccw = std::to_string(r) + ":" + std::to_string((r + 3) & 3);
                        if (auto* a = kv.second.get(cw)) load_kicks(*a, kicks90[p][r][0]);
                        if (auto* a = kv.second.get(ccw)) load_kicks(*a, kicks90[p][r][1]);
                    }
                }
            }
            if (auto* k180 = geom->get("kicks_180")) {
                if (k180->type == Json::OBJ) {
                    for (int r = 0; r < 4; ++r) {
                        std::string k = std::to_string(r) + ":" + std::to_string((r + 2) & 3);
                        if (auto* a = k180->get(k)) load_kicks(*a, kicks180[r]);
                    }
                } else if (k180->type == Json::ARR) {
                    for (int r = 0; r < 4; ++r) load_kicks(*k180, kicks180[r]);
                }
            }
        }
        recompute_derived();
    }

    inline int post_lock_frames(int lines, uint8_t spin) const {
        if (!is_realtime) return 1;
        if (lines == 0) return lock_frames;
        return lock_frames + ((lines == 4 || spin == SPIN_FULL) ? special_clear_frames : clear_frames);
    }

    inline int overtime_count(int frame) const {
        if (!is_realtime || frame < overtime_first_frame) return 0;
        return 1 + (frame - overtime_first_frame) / overtime_interval_frames;
    }

    inline int frames_until_next_overtime(int frame) const {
        if (!is_realtime) return 9999;
        if (frame < overtime_first_frame) return overtime_first_frame - frame;
        return overtime_interval_frames - ((frame - overtime_first_frame) % overtime_interval_frames);
    }
};

// ------------------------- Board & Bitboard Physics -------------------------

struct BoardState {
    std::array<uint16_t, BOARD_H> rows{};
    uint32_t solid_mask = 0;
    uint32_t garbage_mask = 0;
    uint32_t opaque_mask = 0;

    inline bool fits(const RulesConfig& cfg, int8_t p, int x, int y, int r) const {
        if (x + cfg.min_dx[p][r] < 0 || x + cfg.max_dx[p][r] >= BOARD_W) return false;
        if (y + cfg.min_dy[p][r] < 0 || y + cfg.max_dy[p][r] >= BOARD_H) return false;
        const auto& c = cfg.cells[p][r];
        return !((rows[y + c[0].dy] & (1u << (x + c[0].dx))) ||
                 (rows[y + c[1].dy] & (1u << (x + c[1].dx))) ||
                 (rows[y + c[2].dy] & (1u << (x + c[2].dx))) ||
                 (rows[y + c[3].dy] & (1u << (x + c[3].dx))));
    }

    inline bool push_solid(int delta) {
        bool overflow = false;
        for (int s = 0; s < delta; ++s) {
            if (rows[BOARD_H - 1]) overflow = true;
            for (int y = BOARD_H - 1; y > 0; --y) rows[y] = rows[y - 1];
            rows[0] = FULL_ROW;
            solid_mask = ((solid_mask << 1) | 1u) & ((1u << BOARD_H) - 1u);
            garbage_mask = (garbage_mask << 1) & ((1u << BOARD_H) - 1u);
            opaque_mask = (opaque_mask << 1) & ((1u << BOARD_H) - 1u);
        }
        return overflow;
    }

    inline uint8_t spawnable_mask(const RulesConfig& cfg) const {
        uint8_t mask = 0;
        for (int p = 0; p < NUM_PIECES; ++p) {
            const auto& sm = cfg.spawn_masks[p];
            if (!sm.valid) continue;
            bool ok = true;
            for (uint8_t i = 0; i < sm.count; ++i) {
                if (rows[sm.rows[i].first] & sm.rows[i].second) { ok = false; break; }
            }
            if (ok) mask |= static_cast<uint8_t>(1u << p);
        }
        return mask;
    }

    inline int solid_floor() const {
        int f = 0;
        while (f < BOARD_H && ((solid_mask >> f) & 1u) && rows[f] == FULL_ROW) ++f;
        return f;
    }

    inline void compute_col_heights(std::array<int8_t, BOARD_W>& h) const {
        h.fill(0);
        uint16_t rem = FULL_ROW;
        for (int y = BOARD_H - 1; y >= 0 && rem; --y) {
            uint16_t hit = rows[y] & rem;
            if (!hit) continue;
            rem &= static_cast<uint16_t>(~hit);
            while (hit) {
                int x = std::countr_zero(hit);
                h[x] = static_cast<int8_t>(y + 1);
                hit &= static_cast<uint16_t>(hit - 1);
            }
        }
    }

    inline uint64_t hash64() const {
        uint64_t h = 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(solid_mask) << 32) ^ opaque_mask;
        for (int y = 0; y < BOARD_H; ++y) {
            if (rows[y]) {
                uint64_t v = (static_cast<uint64_t>(y) << 16) | rows[y];
                h ^= v * 0xBF58476D1CE4E5B9ULL;
                h = (h << 13) | (h >> 51);
            }
        }
        return h;
    }
};

struct WarningItem { int16_t lines = 0; int32_t due = 0; };
struct WarningQueue {
    std::array<WarningItem, 8> items{};
    uint8_t count = 0;
    int total_lines() const { int s = 0; for (uint8_t i = 0; i < count; ++i) s += items[i].lines; return s; }
    int due_by(int tick) const { int s = 0; for (uint8_t i = 0; i < count; ++i) if (items[i].due <= tick) s += items[i].lines; return s; }
};

struct Placement {
    BoardState after;
    uint8_t lines = 0;
    uint8_t spin = SPIN_NONE;
    bool perfect_clear = false;
    uint16_t pre_lock_frames = 0;
    uint16_t cmd_len = 0;
    uint32_t packed_cells = 0;
    std::vector<uint8_t> cmds;
};

struct SettlementResult {
    BoardState board;
    WarningQueue warnings;
    int attack = 0;
    int sent = 0;
    int cancelled = 0;
    int risen = 0;
    int combo = 0;
    bool b2b = false;
    int combo_blocks = 0;
    bool combo_blocked = false;
    int next_frame = 0;
    int total_frames = 0;
    bool alive = true;
    uint8_t spawn_mask = 0x7F;
};

inline int combo_bonus(int combo) {
    if (combo <= 2) return 0;
    if (combo <= 5) return 1;
    if (combo <= 7) return 2;
    if (combo <= 9) return 3;
    if (combo <= 12) return 4;
    return 5;
}

inline void score_clear(const RulesConfig& cfg, int lines, uint8_t spin, bool pc,
                        int in_combo, bool in_b2b, int& out_attack, int& out_combo, bool& out_b2b) {
    if (lines == 0) { out_attack = 0; out_combo = 0; out_b2b = in_b2b; return; }
    out_combo = in_combo + 1;
    bool t_attack = (spin == SPIN_FULL || (spin == SPIN_MINI && lines >= 2));
    bool difficult = (lines == 4 || t_attack);
    int atk = t_attack ? cfg.tspin_attack[lines] : cfg.normal_attack[lines];
    out_b2b = in_b2b;
    if (difficult) {
        if (in_b2b) atk += cfg.b2b_bonus;
        out_b2b = true;
    } else if (spin != SPIN_MINI) {
        out_b2b = false;
    }
    atk += combo_bonus(out_combo);
    if (pc && atk < cfg.perfect_clear_floor) atk = cfg.perfect_clear_floor;
    out_attack = atk;
}

inline uint32_t pack_sorted_cells(const RulesConfig& cfg, int8_t p, int x, int y, int r) {
    const auto& c = cfg.cells[p][r];
    uint32_t a = static_cast<uint32_t>((y + c[0].dy) * 10 + (x + c[0].dx));
    uint32_t b = static_cast<uint32_t>((y + c[1].dy) * 10 + (x + c[1].dx));
    uint32_t d = static_cast<uint32_t>((y + c[2].dy) * 10 + (x + c[2].dx));
    uint32_t e = static_cast<uint32_t>((y + c[3].dy) * 10 + (x + c[3].dx));
    if (a > b) std::swap(a, b);
    if (d > e) std::swap(d, e);
    if (a > d) std::swap(a, d);
    if (b > e) std::swap(b, e);
    if (b > d) std::swap(b, d);
    return a | (b << 8) | (d << 16) | (e << 24);
}

inline void finalize_placement(
    const RulesConfig& cfg,
    const BoardState& board,
    int8_t piece,
    int x, int y, int r, uint8_t spin_flag,
    uint16_t pre_frames, uint16_t cmd_len, uint32_t packed_cells,
    Placement& out) {

    std::array<uint16_t, BOARD_H> packed = board.rows;
    const auto& c = cfg.cells[piece][r];
    for (int i = 0; i < 4; ++i) packed[y + c[i].dy] |= static_cast<uint16_t>(1u << (x + c[i].dx));

    uint32_t clear_mask = 0;
    uint8_t cleared = 0;
    uint32_t blocked_clear = board.solid_mask | board.opaque_mask;
    for (int row = 0; row < BOARD_H; ++row) {
        if (packed[row] == FULL_ROW && !((blocked_clear >> row) & 1u)) {
            clear_mask |= (1u << row);
            ++cleared;
        }
    }

    uint8_t spin = SPIN_NONE;
    if (piece == P_T && spin_flag != 0) {
        auto corner = [&](int cx, int cy) -> bool {
            if (cx < 0 || cx >= BOARD_W || cy < 0) return true;
            if (cy >= BOARD_H) return false;
            if ((board.opaque_mask >> cy) & 1u) return false;
            return (packed[cy] & (1u << cx)) != 0;
        };
        bool cs[4] = {corner(x, y + 2), corner(x + 2, y + 2), corner(x + 2, y), corner(x, y)};
        if (static_cast<int>(cs[0]) + cs[1] + cs[2] + cs[3] >= 3) {
            spin = ((cs[r] && cs[(r + 1) & 3]) || cleared >= 2 || spin_flag == 2) ? SPIN_FULL : SPIN_MINI;
        }
    }

    BoardState next_b;
    int dst = 0;
    bool any_nonempty = false;
    for (int row = 0; row < BOARD_H; ++row) {
        if ((clear_mask >> row) & 1u) continue;
        next_b.rows[dst] = packed[row];
        if (packed[row]) any_nonempty = true;
        if ((board.solid_mask >> row) & 1u) next_b.solid_mask |= (1u << dst);
        if ((board.garbage_mask >> row) & 1u) next_b.garbage_mask |= (1u << dst);
        if ((board.opaque_mask >> row) & 1u) next_b.opaque_mask |= (1u << dst);
        ++dst;
    }

    out.after = next_b;
    out.lines = cleared;
    out.spin = spin;
    out.perfect_clear = !any_nonempty;
    out.pre_lock_frames = pre_frames;
    out.cmd_len = cmd_len;
    out.packed_cells = packed_cells;
}

inline bool execute_cmds_exact(
    const RulesConfig& cfg,
    const BoardState& board,
    int8_t piece,
    const std::vector<uint8_t>& cmds,
    uint16_t pre_frames,
    Placement& out) {

    int x = cfg.spawn_x[piece], y = cfg.spawn_y[piece], r = 0;
    uint8_t flag = 0;
    if (!board.fits(cfg, piece, x, y, r)) return false;
    for (uint8_t cmd : cmds) {
        if (cmd == CMD_LEFT) {
            if (board.fits(cfg, piece, x - 1, y, r)) { --x; flag = 0; }
        } else if (cmd == CMD_RIGHT) {
            if (board.fits(cfg, piece, x + 1, y, r)) { ++x; flag = 0; }
        } else if (cmd == CMD_DOWN) {
            if (board.fits(cfg, piece, x, y - 1, r)) { --y; flag = 0; }
        } else if (cmd == CMD_CW || cmd == CMD_CCW || cmd == CMD_ROT180) {
            int nr = (cmd == CMD_CW) ? ((r + 1) & 3) : (cmd == CMD_CCW ? ((r + 3) & 3) : ((r + 2) & 3));
            const KickList& kl = (cmd == CMD_ROT180) ? cfg.kicks180[r]
                                                     : cfg.kicks90[piece][r][cmd == CMD_CW ? 0 : 1];
            for (uint8_t k = 0; k < kl.count; ++k) {
                int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                if (board.fits(cfg, piece, nx, ny, nr)) {
                    x = nx; y = ny; r = nr;
                    flag = (piece == P_T) ? ((cmd != CMD_ROT180 && k == 4) ? 2 : 1) : 0;
                    break;
                }
            }
        } else if (cmd == CMD_HARD_DROP) {
            int fy = y;
            while (board.fits(cfg, piece, x, fy - 1, r)) --fy;
            if (fy < y) flag = 0;
            y = fy;
            break;
        }
    }
    uint32_t pcells = pack_sorted_cells(cfg, piece, x, y, r);
    finalize_placement(cfg, board, piece, x, y, r, flag, pre_frames, static_cast<uint16_t>(cmds.size()), pcells, out);
    return true;
}

inline SettlementResult apply_settlement(
    const RulesConfig& cfg,
    const Placement& pl,
    const WarningQueue& in_warnings,
    int in_combo,
    bool in_b2b,
    int in_combo_blocks,
    int cur_tick,
    bool hold_used,
    int8_t next_piece,
    bool pre_lock_ot_already_applied = false) {

    SettlementResult res;
    res.board = pl.after;
    int pre_frames = cfg.is_realtime ? (pl.pre_lock_frames + (hold_used ? cfg.hold_frames : 0)) : 0;
    int lock_tick = cur_tick + pre_frames;

    score_clear(cfg, pl.lines, pl.spin, pl.perfect_clear,
                in_combo, in_b2b, res.attack, res.combo, res.b2b);

    int rem_atk = res.attack;
    WarningQueue due_q, fut_q;
    for (uint8_t i = 0; i < in_warnings.count; ++i) {
        int lines = in_warnings.items[i].lines;
        int due = in_warnings.items[i].due;
        int used = std::min(rem_atk, lines);
        rem_atk -= used;
        res.cancelled += used;
        int left = lines - used;
        if (left > 0) {
            WarningItem wi{static_cast<int16_t>(left), due};
            if (due <= lock_tick) { if (due_q.count < 8) due_q.items[due_q.count++] = wi; }
            else { if (fut_q.count < 8) fut_q.items[fut_q.count++] = wi; }
        }
    }
    res.sent = rem_atk;

    bool overflow = false;
    res.combo_blocks = in_combo_blocks;

    if (cfg.is_realtime && due_q.count > 0 && pl.lines > 0 && in_combo_blocks < cfg.combo_block_limit) {
        res.combo_blocks = in_combo_blocks + 1;
        res.combo_blocked = true;
        for (uint8_t i = 0; i < due_q.count && res.warnings.count < 8; ++i)
            res.warnings.items[res.warnings.count++] = {due_q.items[i].lines, lock_tick};
        for (uint8_t i = 0; i < fut_q.count && res.warnings.count < 8; ++i)
            res.warnings.items[res.warnings.count++] = fut_q.items[i];
    } else {
        int budget = (cfg.garbage_cap >= 0) ? cfg.garbage_cap : 1000;
        WarningQueue def_q;
        for (uint8_t i = 0; i < due_q.count; ++i) {
            int count = due_q.items[i].lines;
            int take = std::min(count, budget);
            budget -= take;
            if (take > 0) {
                res.risen += take;
                int floor = res.board.solid_floor();
                int capacity = BOARD_H - floor;
                int retained = std::max(0, capacity - take);
                if (take > capacity) overflow = true;
                for (int y = floor + retained; y < BOARD_H; ++y) {
                    if (res.board.rows[y]) { overflow = true; break; }
                }
                int inserted = std::min(take, capacity);
                std::array<uint16_t, BOARD_H> nr{};
                uint32_t nsolid = 0, ngarb = 0, nopaque = 0;
                for (int y = 0; y < floor; ++y) {
                    nr[y] = res.board.rows[y];
                    if ((res.board.solid_mask >> y) & 1u) nsolid |= (1u << y);
                }
                for (int y = floor; y < floor + inserted; ++y) {
                    nr[y] = FULL_ROW; ngarb |= (1u << y); nopaque |= (1u << y);
                }
                for (int y = 0; y < retained; ++y) {
                    int src = floor + y;
                    int dst = floor + inserted + y;
                    nr[dst] = res.board.rows[src];
                    if ((res.board.solid_mask >> src) & 1u) nsolid |= (1u << dst);
                    if ((res.board.garbage_mask >> src) & 1u) ngarb |= (1u << dst);
                    if ((res.board.opaque_mask >> src) & 1u) nopaque |= (1u << dst);
                }
                res.board.rows = nr;
                res.board.solid_mask = nsolid;
                res.board.garbage_mask = ngarb;
                res.board.opaque_mask = nopaque;
            }
            if (count > take && def_q.count < 8)
                def_q.items[def_q.count++] = {static_cast<int16_t>(count - take), lock_tick + 1};
        }
        for (uint8_t i = 0; i < def_q.count && res.warnings.count < 8; ++i)
            res.warnings.items[res.warnings.count++] = def_q.items[i];
        for (uint8_t i = 0; i < fut_q.count && res.warnings.count < 8; ++i)
            res.warnings.items[res.warnings.count++] = fut_q.items[i];
        if (pl.lines == 0) res.combo_blocks = 0;
    }

    int post_frames = cfg.post_lock_frames(pl.lines, pl.spin);
    res.total_frames = pre_frames + post_frames;
    res.next_frame = lock_tick + post_frames;

    int solid_delta = 0;
    if (cfg.is_realtime) {
        int base_ot_tick = pre_lock_ot_already_applied ? lock_tick : cur_tick;
        solid_delta = std::max(0, cfg.overtime_count(res.next_frame) - cfg.overtime_count(base_ot_tick));
    } else if (cur_tick >= cfg.overtime_first_turn &&
               (cur_tick - cfg.overtime_first_turn) % cfg.overtime_interval_turn == 0) {
        solid_delta = 1;
    }
    if (solid_delta > 0) {
        if (res.board.push_solid(solid_delta)) overflow = true;
    }

    res.spawn_mask = res.board.spawnable_mask(cfg);
    bool next_ok = (next_piece >= 0) ? (((res.spawn_mask >> next_piece) & 1u) != 0) : (res.spawn_mask != 0);
    res.alive = (!overflow) && next_ok;
    return res;
}

// ------------------------- Generation-Stamped Zero-Memset Movegen -------------------------

struct MovegenScratch {
    static constexpr int X_OFF = 3;
    static constexpr int Y_OFF = 3;
    static constexpr int NX = 14;
    static constexpr int NY = 28;
    static constexpr int NSTATES = NX * NY * 4 * 3;
    static constexpr int BUCKETS = 64;

    uint32_t gen = 1;
    std::array<uint32_t, NSTATES> seen_gen{};
    std::array<uint16_t, NSTATES> dist{};
    std::array<uint16_t, NSTATES> steps{};
    std::array<int16_t, NSTATES> parent{};
    std::array<uint8_t, NSTATES> parent_cmd{};
    std::array<uint32_t, NX * NY * 4> fall_gen{};
    std::array<int8_t, NX * NY * 4> fall_y{};
    std::array<std::vector<uint16_t>, BUCKETS> buckets;
    std::vector<uint64_t> seen_landings;

    void next_gen() {
        ++gen;
        if (gen == 0) { seen_gen.fill(0); fall_gen.fill(0); gen = 1; }
    }
    static inline int encode(int x, int y, int r, int f) {
        return (((x + X_OFF) * NY + (y + Y_OFF)) * 4 + r) * 3 + f;
    }
    static inline int encode_xyr(int x, int y, int r) {
        return ((x + X_OFF) * NY + (y + Y_OFF)) * 4 + r;
    }
};

struct FlatDedupSet {
    static constexpr uint32_t CAP = 1024;
    static constexpr uint32_t MASK = CAP - 1;
    uint32_t gen = 1;
    std::array<uint32_t, CAP> tag{};
    std::array<uint64_t, CAP> keys{};

    void clear() { ++gen; if (gen == 0) { tag.fill(0); gen = 1; } }
    inline bool insert(uint64_t k) {
        uint32_t idx = static_cast<uint32_t>((k ^ (k >> 33)) * 0xFF51AFD7ED558CCDULL) & MASK;
        while (tag[idx] == gen) {
            if (keys[idx] == k) return false;
            idx = (idx + 1) & MASK;
        }
        tag[idx] = gen;
        keys[idx] = k;
        return true;
    }
};

void generate_moves_full(
    const RulesConfig& cfg,
    const BoardState& board,
    int8_t piece,
    bool record_cmds,
    MovegenScratch& sc,
    std::vector<Placement>& out,
    bool center_only = false) {

    out.clear();
    int sx = cfg.spawn_x[piece];
    int sy = cfg.spawn_y[piece];
    if (!board.fits(cfg, piece, sx, sy, 0)) return;

    sc.next_gen();
    const uint32_t g = sc.gen;
    for (auto& b : sc.buckets) b.clear();
    sc.seen_landings.clear();

    auto get_fall_y = [&](int x, int y, int r) -> int {
        int idx = MovegenScratch::encode_xyr(x, y, r);
        if (sc.fall_gen[idx] == g) return sc.fall_y[idx];
        int ty = y;
        while (board.fits(cfg, piece, x, ty - 1, r)) --ty;
        for (int yy = y; yy >= ty; --yy) {
            int ii = MovegenScratch::encode_xyr(x, yy, r);
            sc.fall_gen[ii] = g;
            sc.fall_y[ii] = static_cast<int8_t>(ty);
        }
        return ty;
    };

    int start_id = MovegenScratch::encode(sx, sy, 0, 0);
    sc.seen_gen[start_id] = g;
    sc.dist[start_id] = 0;
    sc.steps[start_id] = 0;
    sc.parent[start_id] = -1;
    sc.buckets[0].push_back(static_cast<uint16_t>(start_id));

    const int max_cmds_before_drop = std::max(0, cfg.max_commands - 1);
    const bool is_t = (piece == P_T);
    const bool only_one_rot = cfg.unique_rot1[piece];
    const int c_rot = std::max(1, cfg.is_realtime ? cfg.rotate_frames : 1);
    const int c_mov = std::max(1, cfg.is_realtime ? cfg.move_frames : 1);
    const int c_dn  = std::max(1, cfg.is_realtime ? cfg.soft_drop_frames : 1);

    int active_count = 1;
    uint16_t cur_cost = 0;

    auto try_relax = [&](int nx, int ny, int nr, int nf, uint16_t ncost, uint16_t nsteps, int pid, uint8_t cmd) {
        if (nsteps > max_cmds_before_drop) return;
        if (center_only) {
            if (nx + cfg.min_dx[piece][nr] < 3 || nx + cfg.max_dx[piece][nr] > 6) return;
        }
        int nid = MovegenScratch::encode(nx, ny, nr, nf);
        bool fresh = (sc.seen_gen[nid] != g);
        if (fresh || ncost < sc.dist[nid] || (ncost == sc.dist[nid] && nsteps < sc.steps[nid])) {
            sc.seen_gen[nid] = g;
            sc.dist[nid] = ncost;
            sc.steps[nid] = nsteps;
            sc.parent[nid] = static_cast<int16_t>(pid);
            sc.parent_cmd[nid] = cmd;
            sc.buckets[ncost & (MovegenScratch::BUCKETS - 1)].push_back(static_cast<uint16_t>(nid));
            ++active_count;
        }
    };

    auto has_landing = [&](uint64_t key) -> bool {
        for (uint64_t k : sc.seen_landings) if (k == key) return true;
        return false;
    };

    while (active_count > 0) {
        auto& bucket = sc.buckets[cur_cost & (MovegenScratch::BUCKETS - 1)];
        while (!bucket.empty()) {
            uint16_t id = bucket.back();
            bucket.pop_back();
            --active_count;
            if (sc.dist[id] != cur_cost) continue;

            int tmp = id;
            int flag = tmp % 3; tmp /= 3;
            int r = tmp & 3; tmp >>= 2;
            int y = (tmp % MovegenScratch::NY) - MovegenScratch::Y_OFF;
            int x = (tmp / MovegenScratch::NY) - MovegenScratch::X_OFF;
            uint16_t st = sc.steps[id];

            int fy = get_fall_y(x, y, r);
            uint8_t final_flag = (fy == y) ? static_cast<uint8_t>(flag) : 0;
            uint32_t pcells = pack_sorted_cells(cfg, piece, x, fy, r);

            uint8_t spin_preview = SPIN_NONE;
            if (is_t && final_flag != 0) {
                std::array<uint16_t, BOARD_H> packed = board.rows;
                const auto& c = cfg.cells[P_T][r];
                for (int i = 0; i < 4; ++i) packed[fy + c[i].dy] |= static_cast<uint16_t>(1u << (x + c[i].dx));
                uint8_t clr = 0;
                uint32_t blk = board.solid_mask | board.opaque_mask;
                for (int row = 0; row < BOARD_H; ++row)
                    if (packed[row] == FULL_ROW && !((blk >> row) & 1u)) ++clr;
                auto corner = [&](int cx, int cy) -> bool {
                    if (cx < 0 || cx >= BOARD_W || cy < 0) return true;
                    if (cy >= BOARD_H) return false;
                    if ((board.opaque_mask >> cy) & 1u) return false;
                    return (packed[cy] & (1u << cx)) != 0;
                };
                bool cs[4] = {corner(x, fy + 2), corner(x + 2, fy + 2), corner(x + 2, fy), corner(x, fy)};
                if (static_cast<int>(cs[0]) + cs[1] + cs[2] + cs[3] >= 3) {
                    spin_preview = ((cs[r] && cs[(r + 1) & 3]) || clr >= 2 || final_flag == 2) ? SPIN_FULL : SPIN_MINI;
                }
            }
            uint64_t lkey = (static_cast<uint64_t>(pcells) << 2) | spin_preview;
            if (!has_landing(lkey)) {
                sc.seen_landings.push_back(lkey);
                Placement pl;
                finalize_placement(cfg, board, piece, x, fy, r, final_flag, cur_cost, st + 1, pcells, pl);
                if (record_cmds) {
                    pl.cmds.reserve(st + 1);
                    int cur = id;
                    while (sc.parent[cur] != -1) {
                        pl.cmds.push_back(sc.parent_cmd[cur]);
                        cur = sc.parent[cur];
                    }
                    std::reverse(pl.cmds.begin(), pl.cmds.end());
                    pl.cmds.push_back(CMD_HARD_DROP);
                }
                out.push_back(std::move(pl));
            }

            if (st >= max_cmds_before_drop) continue;

            if (board.fits(cfg, piece, x - 1, y, r))
                try_relax(x - 1, y, r, 0, cur_cost + c_mov, st + 1, id, CMD_LEFT);
            if (board.fits(cfg, piece, x + 1, y, r))
                try_relax(x + 1, y, r, 0, cur_cost + c_mov, st + 1, id, CMD_RIGHT);
            if (board.fits(cfg, piece, x, y - 1, r))
                try_relax(x, y - 1, r, 0, cur_cost + c_dn, st + 1, id, CMD_DOWN);

            if (!only_one_rot) {
                {
                    int nr = (r + 1) & 3;
                    const auto& kl = cfg.kicks90[piece][r][0];
                    for (uint8_t k = 0; k < kl.count; ++k) {
                        int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                        if (board.fits(cfg, piece, nx, ny, nr)) {
                            try_relax(nx, ny, nr, is_t ? (k == 4 ? 2 : 1) : 0, cur_cost + c_rot, st + 1, id, CMD_CW);
                            break;
                        }
                    }
                }
                {
                    int nr = (r + 3) & 3;
                    const auto& kl = cfg.kicks90[piece][r][1];
                    for (uint8_t k = 0; k < kl.count; ++k) {
                        int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                        if (board.fits(cfg, piece, nx, ny, nr)) {
                            try_relax(nx, ny, nr, is_t ? (k == 4 ? 2 : 1) : 0, cur_cost + c_rot, st + 1, id, CMD_CCW);
                            break;
                        }
                    }
                }
                {
                    int nr = (r + 2) & 3;
                    const auto& kl = cfg.kicks180[r];
                    for (uint8_t k = 0; k < kl.count; ++k) {
                        int nx = x + kl.data[k].dx, ny = y + kl.data[k].dy;
                        if (board.fits(cfg, piece, nx, ny, nr)) {
                            try_relax(nx, ny, nr, is_t ? 1 : 0, cur_cost + c_rot, st + 1, id, CMD_ROT180);
                            break;
                        }
                    }
                }
            }
        }
        ++cur_cost;
    }
}

void generate_moves_fast_col(
    const RulesConfig& cfg,
    const BoardState& board,
    const std::array<int8_t, BOARD_W>& col_h,
    int8_t piece,
    std::vector<Placement>& out,
    bool center_only = false) {

    out.clear();
    int sx = cfg.spawn_x[piece];
    int sy = cfg.spawn_y[piece];
    if (!board.fits(cfg, piece, sx, sy, 0)) return;

    const int c_rot = cfg.is_realtime ? cfg.rotate_frames : 1;
    const int c_mov = cfg.is_realtime ? cfg.move_frames : 1;
    const int rot_count = cfg.unique_rot1[piece] ? 1 : 4;

    uint32_t seen[48];
    int seen_n = 0;

    auto drop_contour = [&](int x, int ry, int r) -> int {
        const auto& c = cfg.cells[piece][r];
        int fy = std::max({
            static_cast<int>(col_h[x + c[0].dx]) - c[0].dy,
            static_cast<int>(col_h[x + c[1].dx]) - c[1].dy,
            static_cast<int>(col_h[x + c[2].dx]) - c[2].dy,
            static_cast<int>(col_h[x + c[3].dx]) - c[3].dy
        });
        if (fy > ry || !board.fits(cfg, piece, x, fy, r)) {
            fy = ry;
            while (board.fits(cfg, piece, x, fy - 1, r)) --fy;
        }
        return fy;
    };

    constexpr int rots[4] = {0, 1, 3, 2};
    for (int ri = 0; ri < rot_count; ++ri) {
        int r = rots[ri];
        int rx = sx, ry = sy;
        uint16_t base_cost = 0, base_len = 0;
        if (r != 0) {
            base_cost = static_cast<uint16_t>(c_rot);
            base_len = 1;
            const KickList& kl = (r == 1) ? cfg.kicks90[piece][0][0]
                               : (r == 3) ? cfg.kicks90[piece][0][1]
                                          : cfg.kicks180[0];
            bool ok = false;
            for (uint8_t k = 0; k < kl.count; ++k) {
                if (board.fits(cfg, piece, sx + kl.data[k].dx, sy + kl.data[k].dy, r)) {
                    rx = sx + kl.data[k].dx; ry = sy + kl.data[k].dy; ok = true; break;
                }
            }
            if (!ok) continue;
        }

        auto emit_hd = [&](int x, uint16_t cost, uint16_t len) {
            if (center_only && (x + cfg.min_dx[piece][r] < 3 || x + cfg.max_dx[piece][r] > 6)) return;
            int fy = drop_contour(x, ry, r);
            uint32_t pcells = pack_sorted_cells(cfg, piece, x, fy, r);
            for (int i = 0; i < seen_n; ++i) if (seen[i] == pcells) return;
            if (seen_n < 48) seen[seen_n++] = pcells;
            Placement pl;
            finalize_placement(cfg, board, piece, x, fy, r, 0, cost, static_cast<uint16_t>(len + 1), pcells, pl);
            out.push_back(std::move(pl));
        };

        emit_hd(rx, base_cost, base_len);
        for (int s = 1; s <= 6; ++s) {
            if (!board.fits(cfg, piece, rx - s, ry, r)) break;
            emit_hd(rx - s, static_cast<uint16_t>(base_cost + s * c_mov), static_cast<uint16_t>(base_len + s));
        }
        for (int s = 1; s <= 6; ++s) {
            if (!board.fits(cfg, piece, rx + s, ry, r)) break;
            emit_hd(rx + s, static_cast<uint16_t>(base_cost + s * c_mov), static_cast<uint16_t>(base_len + s));
        }
    }
}

// ------------------------- Center 4-Wide Inspector & Evaluators -------------------------

struct C4WStats {
    int base = 0;
    int garb_top = 0;         // first non-garbage, non-solid row (start of our C4W zone)
    int full_side_rows = 0;   // rows >= garb_top with all 6 side cells (0..2, 7..9) filled
    int side_cells = 0;
    int side_holes = 0;
    int side_covered = 0;
    int left_h = 0;
    int right_h = 0;
    int side_max_h = 0;
    int side_bump = 0;
    int center_cells = 0;     // residual cells in cols 3..6 at rows >= garb_top
    int center_max_h = 0;     // relative to garb_top
    int center_holes = 0;
    int center_covered = 0;
    int center_bump = 0;
    int garbage_rows = 0;
};

inline C4WStats inspect_c4w(const BoardState& b) {
    C4WStats s;
    s.base = b.solid_floor();
    // Garbage + opaque rows sit directly above solid floor
    uint32_t gmask = b.garbage_mask | b.opaque_mask;
    s.garbage_rows = std::popcount(b.garbage_mask & ~b.opaque_mask);
    int gtop = s.base;
    if (gmask) gtop = std::max(s.base, static_cast<int>(std::bit_width(gmask)));
    s.garb_top = gtop;

    std::array<uint32_t, BOARD_W> cols{};
    for (int y = gtop; y < BOARD_H; ++y) {
        uint16_t r = b.rows[y];
        if (!r) continue;
        if ((r & SIDES_MASK) == SIDES_MASK) ++s.full_side_rows;
        s.side_cells += std::popcount(static_cast<uint16_t>(r & SIDES_MASK));
        s.center_cells += std::popcount(static_cast<uint16_t>(r & CENTER_MASK));
        uint32_t bit = 1u << (y - gtop);
        uint16_t m = r;
        while (m) {
            int x = std::countr_zero(m);
            cols[x] |= bit;
            m &= static_cast<uint16_t>(m - 1);
        }
    }

    int h[BOARD_W] = {};
    for (int x = 0; x < BOARD_W; ++x) {
        h[x] = static_cast<int>(std::bit_width(cols[x]));
        uint32_t below = (h[x] >= 32) ? 0xFFFFFFFFu : ((1u << h[x]) - 1u);
        uint32_t gaps = below & ~cols[x];
        int nh = std::popcount(gaps);
        int cov = 0;
        uint32_t gm = gaps;
        while (gm) {
            int gy = std::countr_zero(gm);
            cov += std::min(5, std::popcount(cols[x] >> (gy + 1)));
            gm &= (gm - 1);
        }
        if (x < 3 || x > 6) {
            s.side_holes += nh;
            s.side_covered += cov;
            if (x < 3) s.left_h = std::max(s.left_h, h[x]);
            else s.right_h = std::max(s.right_h, h[x]);
        } else {
            s.center_holes += nh;
            s.center_covered += cov;
            if (h[x] > s.center_max_h) s.center_max_h = h[x];
        }
    }
    s.side_max_h = std::max(s.left_h, s.right_h);
    s.side_bump = std::abs(h[0] - h[1]) + std::abs(h[1] - h[2]) + std::abs(h[7] - h[8]) + std::abs(h[8] - h[9]);
    s.center_bump = std::abs(h[3] - h[4]) + std::abs(h[4] - h[5]) + std::abs(h[5] - h[6]);
    return s;
}

inline int eval_c4w_build_board(const BoardState& b, int8_t hold_piece, uint8_t spawn_mask,
                                const WarningQueue& warnings, int cur_tick, int target_h) {
    C4WStats s = inspect_c4w(b);
    int spawnable = std::popcount(spawn_mask);
    int soon_warn = warnings.due_by(cur_tick + 35);
    int total_side_h = s.garb_top + s.side_max_h;

    int score = 0;
    // Primary objective: build razor-even, zero-hole 3-wide left & right walls up to target_h!
    int eff_full = std::min(s.full_side_rows, target_h);
    score += 180 * eff_full;
    score += 34 * std::min(s.side_cells, target_h * 6);
    if (s.side_max_h > target_h + 1) {
        int ex = s.side_max_h - (target_h + 1);
        score -= 160 * ex * ex;
    }

    // Strict overflow protection: side walls + incoming warnings must stay strictly below row 23!
    if (total_side_h + soon_warn > 20) {
        int ov = total_side_h + soon_warn - 20;
        score -= 520 * ov * ov;
    }
    if (total_side_h >= 21) {
        int ov = total_side_h - 20;
        score -= 700 * ov * ov;
    }

    // Zero holes in side walls, tightly balanced left/right heights, smooth 3-wide tops
    score -= 780 * s.side_holes;
    score -= 110 * s.side_covered;
    int lr_diff = std::abs(s.left_h - s.right_h);
    score -= 95 * lr_diff + 35 * lr_diff * lr_diff;
    score -= 36 * s.side_bump;

    // Golden 3-residual seed in center 4-wide well!
    if (s.center_holes > 0) score -= 900 * s.center_holes;
    score -= 120 * s.center_covered;
    if (s.center_cells == 3 && s.center_max_h <= 2 && s.center_holes == 0) {
        score += 280; // Perfect 3-residual ready to fire!
    } else if ((s.center_cells == 2 || s.center_cells == 4) && s.center_max_h <= 2 && s.center_holes == 0) {
        score += 95;
    } else if (s.center_cells == 0) {
        score -= 55;
    } else if (s.center_cells > 4) {
        score -= 190 * (s.center_cells - 3);
    }
    if (s.center_max_h > 2) score -= 240 * (s.center_max_h - 2);
    score -= 20 * s.center_bump;

    // Holding I, T, J, or L is great for starting the C4W combo
    if (hold_piece == P_I) score += 60;
    else if (hold_piece == P_T) score += 50;
    else if (hold_piece == P_J || hold_piece == P_L) score += 25;

    score -= 2400 * (7 - spawnable);
    return score;
}

inline int eval_c4w_build_reward(const Placement& pl, const SettlementResult& sr) {
    int r = 0;
    // Do NOT burn our side walls early on 0-attack singles unless canceling/blocking imminent garbage!
    if (pl.lines > 0 && sr.cancelled == 0 && !sr.combo_blocked) {
        r -= 520 * pl.lines;
    }
    if (sr.cancelled > 0) r += 75 * sr.cancelled;
    if (sr.risen > 0) r -= 35 * sr.risen;
    // Ultra-fast finesse while building the C4W walls!
    r -= 5 * static_cast<int>(pl.pre_lock_frames);
    return r;
}

inline int eval_c4w_fire_board(const BoardState& b, int combo, int8_t hold_piece, uint8_t spawn_mask) {
    C4WStats s = inspect_c4w(b);
    int spawnable = std::popcount(spawn_mask);

    int score = 0;
    // Keep golden 3-residual in the center 4-wide well so combo NEVER dies!
    if (s.full_side_rows >= 2) {
        // CRITICAL: Value every preserved row of side-wall ammo!
        // Each saved side row = +1 extra combo step at the end = +5 lines of attack!
        score += 420 * s.full_side_rows;

        if (s.center_holes > 0) score -= 1150 * s.center_holes;
        score -= 150 * s.center_covered;
        // Exact 4-wide 3-residual topology check: S-tier contiguous shapes (1110, 0111, 2100, 1200, 0021, 0012)
        // avoid split/checkered gaps (1011, 1101) and 3-tall spikes that break combos!
        int center_rtrans = 0;
        for (int y = s.garb_top; y < std::min(BOARD_H, s.garb_top + s.center_max_h); ++y) {
            uint32_t cm = ((b.rows[y] & CENTER_MASK) >> 3);
            uint32_t row6 = (cm << 1) | 1u | (1u << 5);
            center_rtrans += std::popcount((row6 ^ (row6 >> 1)) & 0x1Fu);
        }
        if (s.center_cells == 3 && s.center_max_h <= 2 && s.center_holes == 0) {
            score += 420;
            if (center_rtrans <= 2 * s.center_max_h) score += 90; // ultra-clean contiguous residual!
        } else if (s.center_cells == 3 && s.center_max_h <= 3 && s.center_holes == 0) {
            score += 240;
        } else if ((s.center_cells == 2 || s.center_cells == 4) && s.center_max_h <= 2 && s.center_holes == 0) {
            score += 140;
        } else if (s.center_cells == 0) {
            score -= 260;
        } else if (s.center_cells > 5) {
            score -= 180 * (s.center_cells - 4);
        }
        if (s.center_max_h > 2) score -= 120 * (s.center_max_h - 2) * (s.center_max_h - 2);
        score -= 28 * s.center_bump;
        score -= 22 * center_rtrans;
        // Never dump pieces into side walls unnecessarily during fire
        score -= 260 * s.side_holes;
    } else {
        // Tail end of cannon: chain the 13+ combo straight through the garbage rows underneath (+5 lines/clear!)
        score += 420 * s.full_side_rows;
        score -= 420 * (s.side_holes + s.center_holes);
        score -= 55 * (s.side_covered + s.center_covered);
        score -= 22 * (s.side_cells + s.center_cells);
        // Strongly reward exposing the top garbage hole so the +5-attack combo continues!
        uint32_t real_garb = b.garbage_mask & ~b.opaque_mask;
        if (real_garb) {
            int top_g = static_cast<int>(std::bit_width(real_garb)) - 1;
            uint16_t holes1 = static_cast<uint16_t>(FULL_ROW & ~b.rows[top_g]);
            if (holes1) {
                int min_cov = 99;
                uint16_t hm = holes1;
                while (hm) {
                    int gx = std::countr_zero(hm);
                    int cov = 0;
                    for (int y = top_g + 1; y < BOARD_H; ++y) {
                        if ((b.rows[y] >> gx) & 1u) ++cov;
                    }
                    if (cov < min_cov) min_cov = cov;
                    hm &= static_cast<uint16_t>(hm - 1);
                }
                if (min_cov == 0) score += 260;
                else if (min_cov < 99) score -= 95 * min_cov;
            }
        }
    }

    // Massive reward for maintaining high active combo state!
    score += 220 * std::min(combo, 22);
    if (hold_piece == P_I || hold_piece == P_T) score += 50;
    score -= 2600 * (7 - spawnable);
    return score;
}

inline int eval_c4w_fire_reward(const Placement& pl, const SettlementResult& sr, int pre_full_side_rows) {
    int r = 0;
    if (pl.perfect_clear) r += 3800;
    if (pl.lines > 0) {
        // HUGE reward for continuing the combo!
        r += 1800 + 260 * sr.attack;
        // Strictly prefer 1-line clears while tall side walls remain so 15 side rows -> 15-19 combo (45-60 lines)!
        if (pre_full_side_rows >= 3) {
            if (pl.lines == 1) r += 560;
            else if (pl.lines == 2 && pl.spin == SPIN_FULL && sr.combo < 8) r += 260;
            else if (pl.lines == 2) r -= 320;
            else r -= 680 * (pl.lines - 1);
        } else if (sr.combo >= 10 && pl.lines == 1) {
            // Even in the garbage tail below the 4-wide, 1-line clears at combo 10+ give +4/+5 EACH!
            r += 420;
        }
        if (sr.cancelled > 0) r += 90 * sr.cancelled;
        if (sr.combo_blocked) r += 160;
    } else {
        // Dropping the combo while ammo remains is CATASTROPHIC!
        if (pre_full_side_rows >= 2) r -= 3200;
        else r -= 750;
    }
    if (sr.risen > 0) r -= 110 * sr.risen;
    r -= 4 * static_cast<int>(pl.pre_lock_frames);
    return r;
}

// ------------------------- Grandmaster Evaluation (Phase 3 & Overtime) -------------------------

struct EvalContext {
    int opp_max_h = 0;
    int opp_center_h = 0;
    int opp_holes = 0;
    bool has_t_soon = false;
    bool has_i_soon = false;
    int incoming_total = 0;
};

struct BoardFeatures {
    int base = 0;
    std::array<int8_t, BOARD_W> h{};
    std::array<int, BOARD_W> rel{};
    int max_h = 0;
    int max_rel = 0;
    int sum_rel = 0;
    int total_cells = 0;
    int holes = 0;
    int covered = 0;
    int row_trans = 0;
    int col_trans = 0;
    int well_col = 9;
    int well_depth = 0;
    int extra_wells = 0;
    int bumpiness = 0;
    int bump_sq = 0;
    int center_peak = 0;
    int garbage_rows = 0;
    int garbage_cover1 = 0;
    int garbage_cover2 = 0;
    int spawn_zone_risk = 0;
    int tsd_slots = 0;
    int tsd_bases = 0;
};

inline BoardFeatures extract_features(const BoardState& b, bool detect_tspins) {
    BoardFeatures f;
    f.base = b.solid_floor();
    uint32_t floor_mask = (f.base >= 32) ? 0xFFFFFFFFu : ((1u << f.base) - 1u);

    std::array<uint32_t, BOARD_W> cols{};
    for (int y = f.base; y < BOARD_H; ++y) {
        uint16_t r = b.rows[y];
        if (!r) continue;
        f.total_cells += std::popcount(r);
        uint32_t bit = 1u << y;
        uint16_t m = r;
        while (m) {
            int x = std::countr_zero(m);
            cols[x] |= bit;
            m &= static_cast<uint16_t>(m - 1);
        }
    }

    int min_h = BOARD_H + 1;
    for (int x = 0; x < BOARD_W; ++x) {
        cols[x] |= floor_mask;
        int hx = static_cast<int>(std::bit_width(cols[x]));
        f.h[x] = static_cast<int8_t>(hx);
        int rx = std::max(0, hx - f.base);
        f.rel[x] = rx;
        f.sum_rel += rx;
        if (hx > f.max_h) f.max_h = hx;
        if (rx > f.max_rel) f.max_rel = rx;
        if (x >= 3 && x <= 6 && hx > f.center_peak) f.center_peak = hx;
        if (hx < min_h) { min_h = hx; f.well_col = x; }

        if (hx > f.base) {
            uint32_t col_below = (hx >= 32) ? 0xFFFFFFFFu : ((1u << hx) - 1u);
            uint32_t gaps = col_below & ~cols[x] & ~floor_mask;
            f.holes += std::popcount(gaps);
            uint32_t g = gaps;
            while (g) {
                int gy = std::countr_zero(g);
                f.covered += std::min(6, std::popcount(cols[x] >> (gy + 1)));
                g &= (g - 1);
            }
            uint32_t c_shifted = (cols[x] >> f.base);
            uint32_t c_prev = (c_shifted << 1) | 1u;
            uint32_t mask_h = ((hx - f.base) >= 31) ? 0x7FFFFFFFu : ((1u << (hx - f.base + 1)) - 1u);
            f.col_trans += std::popcount((c_shifted ^ c_prev) & mask_h);
        }
    }

    for (int y = f.base; y < f.max_h; ++y) {
        if ((b.opaque_mask >> y) & 1u) continue;
        uint32_t row = (static_cast<uint32_t>(b.rows[y]) << 1) | 1u | (1u << 11);
        f.row_trans += std::popcount((row ^ (row >> 1)) & 0x7FFu);
    }

    auto well_at = [&](int x) -> int {
        int lh = (x == 0) ? BOARD_H : f.h[x - 1];
        int rh = (x == BOARD_W - 1) ? BOARD_H : f.h[x + 1];
        return std::max(0, std::min(lh, rh) - f.h[x]);
    };
    int best_col = f.well_col;
    int best_dep = well_at(f.well_col);
    for (int pref : {9, 0, 7, 2}) {
        int d = well_at(pref);
        if (d > best_dep || (d == best_dep && d > 0 && best_col != 9 && best_col != 0 && best_col != 7 && best_col != 2)) {
            best_col = pref; best_dep = d;
        }
    }
    f.well_col = best_col;
    f.well_depth = best_dep;
    for (int x = 0; x < BOARD_W; ++x) {
        if (x == f.well_col) continue;
        int d = well_at(x);
        if (d >= 3) f.extra_wells += (d - 2);
    }

    if (f.center_peak >= 18) {
        static constexpr int row_w[6] = {1, 4, 18, 45, 70, 90};
        for (int y = 18; y < BOARD_H; ++y) {
            uint16_t sc = b.rows[y] & CENTER_MASK;
            if (sc) f.spawn_zone_risk += std::popcount(sc) * row_w[y - 18];
        }
    }

    int prev_x = -1;
    for (int x = 0; x < BOARD_W; ++x) {
        if (x == f.well_col && f.well_depth >= 1) continue;
        if (prev_x >= 0) {
            int diff = std::abs(f.rel[x] - f.rel[prev_x]);
            f.bumpiness += diff;
            f.bump_sq += diff * diff;
        }
        prev_x = x;
    }

    uint32_t real_garb = b.garbage_mask & ~b.opaque_mask;
    if (real_garb) {
        f.garbage_rows = std::popcount(real_garb);
        int top_g = static_cast<int>(std::bit_width(real_garb)) - 1;
        uint16_t holes1 = static_cast<uint16_t>(FULL_ROW & ~b.rows[top_g]);
        int top_gx = -1;
        if (holes1) {
            int min_cov = 99;
            uint16_t hm = holes1;
            while (hm) {
                int gx = std::countr_zero(hm);
                int cov = std::popcount(cols[gx] >> (top_g + 1));
                if (cov < min_cov) { min_cov = cov; top_gx = gx; }
                hm &= static_cast<uint16_t>(hm - 1);
            }
            if (min_cov < 99) f.garbage_cover1 = min_cov;
        }
        if (top_gx >= 0) {
            for (int y = top_g - 1; y >= f.base; --y) {
                if (!((real_garb >> y) & 1u)) continue;
                uint16_t holes2 = static_cast<uint16_t>(FULL_ROW & ~b.rows[y]);
                if (holes2 && !(holes2 & (1u << top_gx))) {
                    int gx2 = std::countr_zero(holes2);
                    f.garbage_cover2 = std::min(6, std::popcount(cols[gx2] >> (y + 1)));
                    break;
                }
            }
        }
    }

    if (detect_tspins && f.base < 10 && f.max_h <= 15) {
        int slots = 0, bases = 0;
        for (int x = 1; x <= BOARD_W - 2; ++x) {
            int y = f.h[x];
            if (y < f.base || y + 3 >= BOARD_H) continue;
            if ((b.rows[y] & (1u << (x - 1))) && (b.rows[y] & (1u << (x + 1)))) {
                uint16_t mask3 = static_cast<uint16_t>(7u << (x - 1));
                if (!(b.rows[y + 1] & mask3)) {
                    bool left_ov  = (b.rows[y + 2] & (1u << (x - 1))) != 0;
                    bool mid_blk  = (b.rows[y + 2] & (1u << x)) != 0;
                    bool right_ov = (b.rows[y + 2] & (1u << (x + 1))) != 0;
                    int fill0 = std::popcount(b.rows[y]);
                    int fill1 = std::popcount(b.rows[y + 1]);
                    if (!mid_blk && fill0 >= 6 && fill1 >= 4) {
                        if (left_ov != right_ov) {
                            int entry_x = left_ov ? (x + 1) : (x - 1);
                            if (f.h[entry_x] <= y + 1) {
                                ++slots;
                                if (fill0 >= 8 && fill1 >= 6) ++slots;
                            }
                        } else if (!left_ov && !right_ov) {
                            bool can_l = (x >= 2 && f.h[x - 2] >= y + 2);
                            bool can_r = (x <= BOARD_W - 3 && f.h[x + 2] >= y + 2);
                            if ((can_l || can_r) && f.h[x - 1] <= y + 1 && f.h[x + 1] <= y + 1) ++bases;
                        }
                    }
                }
            }
        }
        f.tsd_slots = std::min(slots, 3);
        f.tsd_bases = std::min(bases, 2);
    }
    return f;
}

inline int eval_gm_board(
    const RulesConfig& cfg, const BoardState& b, int combo, bool b2b,
    int8_t hold_piece, uint8_t spawn_mask, const WarningQueue& warnings,
    int cur_tick, const EvalContext& ctx) {

    BoardFeatures f = extract_features(b, true);
    int spawnable = std::popcount(spawn_mask);

    int soon_warn = warnings.due_by(cur_tick + 35);
    int eff_danger = f.max_h + std::min(soon_warn, 8);
    int frames_to_ot = cfg.frames_until_next_overtime(cur_tick);
    bool ot_tight = (f.base >= 10);
    bool ot_extreme = (f.base >= 14);
    bool crisis = (eff_danger >= 16 || f.center_peak >= 16 || f.base >= 13);
    bool downstack = (crisis || ot_tight || eff_danger >= 13 || f.holes >= 2 || (f.garbage_rows >= 3 && f.max_h >= 9));

    int score = 0;
    if (ot_extreme) {
        int ot_urgency = (frames_to_ot <= 90) ? 2 : 1;
        score -= (85 + 15 * ot_urgency) * f.sum_rel;
        score -= 115 * f.max_rel;
        int center_rel = std::max(0, f.center_peak - f.base);
        score -= (180 + 40 * ot_urgency) * center_rel * center_rel;
        int csum = f.rel[3] + f.rel[4] + f.rel[5] + f.rel[6];
        score -= (95 + 25 * ot_urgency) * csum;
        score -= (85 + 30 * ot_urgency) * f.spawn_zone_risk;
    } else if (crisis) {
        score -= 60 * f.sum_rel; score -= 50 * f.max_rel; score -= 60 * f.spawn_zone_risk;
    } else if (downstack) {
        score -= 25 * f.sum_rel; score -= 18 * f.max_rel;
    } else {
        score -= 4 * f.sum_rel;
        if (f.max_rel > 8) { int d = f.max_rel - 8; score -= 28 * d * d; }
    }

    // Hard top-out / spawn-zone fortress with +1 overtime solid-row buffer (strictly stronger than medium!)
    int ot_buf = (f.base >= 14) ? 1 : 0;
    if (f.max_h + ot_buf > 14) {
        int d = f.max_h + ot_buf - 14;
        score -= (ot_extreme ? 260 : 135) * d * d;
    }
    if (f.center_peak + ot_buf > 13) {
        int d = f.center_peak + ot_buf - 13;
        score -= (ot_extreme ? 420 : 195) * d * d;
    }
    if (ot_extreme && f.center_peak >= 19) {
        score -= 5000; // hard cliff: never let center touch row 19+ before a solid row!
    } else if (ot_extreme && f.center_peak >= 18 && frames_to_ot <= 60) {
        score -= 3200; // imminent solid row will push row 18 into spawn zone!
    }
    score -= (ot_extreme ? 4500 : 2100) * (7 - spawnable);

    int hole_w = ot_extreme ? 700 : (crisis ? 380 : (downstack ? 440 : 540));
    int cov_w  = ot_extreme ? 105 : (crisis ? 64  : (downstack ? 80  : 58));
    score -= hole_w * f.holes;
    score -= cov_w * f.covered;
    score -= (downstack ? 25 : 21) * f.row_trans;
    score -= (downstack ? 34 : 30) * f.col_trans;
    score -= (downstack ? 12 : 17) * f.bumpiness;
    score -= (downstack ? 2  : 4)  * f.bump_sq;
    score -= 68 * f.extra_wells;

    if (f.garbage_rows > 0) {
        score -= (downstack ? 165 : 95) * f.garbage_cover1;
        score -= (downstack ? 45  : 22) * f.garbage_cover2;
        if (f.garbage_cover1 == 0) {
            score += 130;
            if (f.garbage_cover2 == 0) score += 45;
        }
    }

    if (!downstack && f.holes <= 1) {
        bool edge_well = (f.well_col == 0 || f.well_col == 9);
        bool near_edge = (f.well_col == 2 || f.well_col == 7 || f.well_col == 1 || f.well_col == 8);
        int well_w = edge_well ? 60 : (near_edge ? 52 : 20);
        score += well_w * std::min(f.well_depth, 6);
        if (f.well_depth >= 4 && ctx.has_i_soon && (edge_well || near_edge)) score += 75;
    }

    if (!crisis && !ot_tight) {
        if (f.tsd_slots > 0) {
            int tsd_w = ctx.has_t_soon ? 320 : 210;
            if (b2b) tsd_w += 75;
            score += tsd_w * f.tsd_slots;
        }
        if (f.tsd_bases > 0) score += (ctx.has_t_soon ? 80 : 40) * f.tsd_bases;
    }

    if (b2b) score += (ot_extreme ? 45 : 225);
    if (combo >= 1) {
        static constexpr int combo_val[11] = {0, 45, 115, 210, 320, 440, 565, 695, 825, 955, 1085};
        score += combo_val[std::min(combo, 10)];
    }

    if (hold_piece == P_T) score += (ot_extreme ? 45 : 115);
    else if (hold_piece == P_I) score += (ot_extreme ? 75 : 75);

    int rem_warn = warnings.total_lines();
    if (rem_warn > 0) score -= (ot_extreme ? 340 : (crisis ? 130 : 48)) * rem_warn;
    return score;
}

inline int eval_gm_reward(
    int8_t piece, const Placement& pl, const SettlementResult& sr, bool was_b2b,
    int pre_holes, int pre_garb_rows, int pre_base, int pre_max_h,
    int prev_burst_sent, const EvalContext& ctx) {

    bool ot_tight = (pre_base >= 10);
    bool ot_extreme = (pre_base >= 14);
    bool ot_sudden = (pre_base >= 16);
    bool opp_vulnerable = (ctx.opp_max_h >= 13 || ctx.opp_center_h >= 12 || (ctx.opp_max_h >= 10 && ctx.opp_holes >= 3));

    int atk_w = ot_sudden ? 85 : (ot_extreme ? 155 : (opp_vulnerable ? 310 : (ctx.opp_max_h >= 10 ? 260 : 230)));
    int r = atk_w * sr.attack;

    if (!ot_extreme) {
        if (sr.sent >= 4) r += 45 * (sr.sent - 3);
        if (sr.sent >= 2 && prev_burst_sent >= 2 && (sr.sent + prev_burst_sent >= 6)) {
            r += (opp_vulnerable ? 70 : 45) * std::min(sr.sent, prev_burst_sent);
        }
    }
    if (sr.cancelled > 0) r += (ot_extreme ? 340 : (pre_max_h >= 13 ? 155 : 72)) * sr.cancelled;

    int post_garb_rows = std::popcount(pl.after.garbage_mask & ~pl.after.opaque_mask);
    bool actually_cleared_garb = (post_garb_rows < pre_garb_rows);

    if (piece == P_T && pl.spin == SPIN_NONE && !ot_tight && pre_max_h <= 11 && pre_holes == 0 && !pl.perfect_clear) {
        r -= 140;
    }

    if (pl.perfect_clear) {
        r += 2900;
    } else if (pl.spin == SPIN_FULL) {
        if (pl.lines == 3) r += ot_sudden ? 80 : (ot_extreme ? 180 : 640);
        else if (pl.lines == 2) r += ot_sudden ? 65 : (ot_extreme ? 150 : 480);
        else r += ot_sudden ? 45 : (ot_extreme ? 100 : 215);
        if (was_b2b && !ot_extreme) r += 155;
    } else if (pl.lines == 4) {
        r += ot_sudden ? 60 : (ot_extreme ? 130 : 245);
        if (was_b2b && !ot_extreme) r += 115;
    } else if (pl.lines > 0) {
        bool emergency_dig = (ot_tight || pre_max_h >= 13 || (pre_holes >= 2 && !was_b2b));
        bool valid_dig = emergency_dig || (actually_cleared_garb && pre_max_h >= 8);
        if (pl.lines == 1 && sr.attack == 0 && !sr.combo_blocked && sr.combo < 2) {
            if (!valid_dig) r -= 230;
            else if (!emergency_dig) r -= 40;
        } else if (pl.lines == 2 && sr.combo < 2 && !valid_dig && !sr.combo_blocked) {
            r -= 55;
        }
        if (was_b2b && !sr.b2b && !emergency_dig && !sr.combo_blocked) r -= 195;
        if (ot_extreme) r += 135 * pl.lines;
        else if (ot_tight) r += 65 * pl.lines;
    }

    if (sr.combo_blocked) r += (ot_extreme ? 220 : (pre_max_h >= 13 ? 290 : 125));
    if (sr.risen > 0) r -= (ot_extreme ? 500 : ((pre_max_h + sr.risen >= 16) ? 245 : 70)) * sr.risen;

    if (pl.spin == SPIN_FULL || pl.lines == 4) r -= 2 * static_cast<int>(pl.pre_lock_frames);
    else if (pl.lines > 0 && sr.attack == 0 && !ot_tight) r -= 4 * sr.total_frames;
    else r -= 4 * static_cast<int>(pl.pre_lock_frames);
    return r;
}

// ------------------------- Bot Controller -------------------------

struct GameObs {
    int turn = 1;
    int frame = 0;
    int pieces_placed = 0;
    BoardState self_board;
    int8_t current = P_NONE;
    int8_t next = P_NONE;
    std::array<int8_t, 7> queue{};
    int queue_len = 0;
    int8_t hold = P_NONE;
    bool can_hold = true;
    WarningQueue warnings;
    int combo = 0;
    bool b2b = false;
    int combo_blocks = 0;
    int opp_max_h = 0;
    int opp_center_h = 0;
    int opp_holes = 0;
};

struct Decision {
    bool is_hold = false;
    std::vector<uint8_t> commands;
};

class StrongBot {
public:
    RulesConfig cfg;
    MovegenScratch scratch;
    FlatDedupSet dedup;
    std::vector<int8_t> bag_stream;

    // Persistent C4W state machine (reset cleanly on new game!)
    PlayMode mode = MODE_C4W_BUILD;
    int c4w_target_h = 15;

    bool has_cached_hold = false;
    int cached_turn = -1;
    int8_t cached_piece = P_NONE;
    uint64_t cached_board_hash = 0;
    std::vector<uint8_t> cached_cmds;

    void parse_board(const Json* jboard, BoardState& b, int* out_max_h = nullptr,
                     int* out_center_h = nullptr, int* out_holes = nullptr) {
        b = BoardState{};
        if (!jboard || jboard->type != Json::ARR) return;
        int max_h = 0;
        std::array<uint32_t, BOARD_W> cols{};
        int nrows = std::min<int>(BOARD_H, static_cast<int>(jboard->arr.size()));
        for (int y = 0; y < nrows; ++y) {
            const auto& row = jboard->arr[y];
            if (row.type != Json::ARR) continue;
            uint16_t mask = 0;
            bool is_solid = false, is_garb = false;
            int ncols = std::min<int>(BOARD_W, static_cast<int>(row.arr.size()));
            for (int x = 0; x < ncols; ++x) {
                int v = row.arr[x].as_int(0);
                if (v != 0) {
                    mask |= static_cast<uint16_t>(1u << x);
                    cols[x] |= (1u << y);
                    if (v == 3) is_solid = true;
                    else if (v == 2) is_garb = true;
                }
            }
            b.rows[y] = mask;
            if (mask) max_h = y + 1;
            if (is_solid) b.solid_mask |= (1u << y);
            if (is_garb) b.garbage_mask |= (1u << y);
        }
        if (out_max_h) *out_max_h = max_h;
        if (out_center_h) {
            int ch = 0;
            for (int x = 3; x <= 6; ++x) ch = std::max(ch, static_cast<int>(std::bit_width(cols[x])));
            *out_center_h = ch;
        }
        if (out_holes) {
            int holes = 0;
            for (int x = 0; x < BOARD_W; ++x) {
                int hx = static_cast<int>(std::bit_width(cols[x]));
                if (hx > 0) {
                    uint32_t below = (hx >= 32) ? 0xFFFFFFFFu : ((1u << hx) - 1u);
                    holes += std::popcount(below & ~cols[x]);
                }
            }
            *out_holes = holes;
        }
    }

    GameObs parse_observation(const Json& obs) {
        if (obs.get("rules") || obs.get("geometry")) {
            cfg.load_from_json(obs.get("rules"), obs.get("geometry"));
            bag_stream.clear();
            mode = MODE_C4W_BUILD;
        }
        GameObs g;
        if (auto* v = obs.get("turn")) g.turn = v->as_int(1);
        if (auto* v = obs.get("frame")) g.frame = v->as_int(0);
        else g.frame = g.turn;

        if (auto* self = obs.get("self"); self && self->type == Json::OBJ) {
            parse_board(self->get("board"), g.self_board);
            if (auto* v = self->get("pieces_placed")) g.pieces_placed = v->as_int(0);
            if (auto* v = self->get("current"); v && v->type == Json::STR && !v->sval.empty())
                g.current = char_to_piece(v->sval[0]);
            if (auto* v = self->get("next"); v && v->type == Json::STR && !v->sval.empty())
                g.next = char_to_piece(v->sval[0]);
            if (auto* nq = self->get("next_queue"); nq && nq->type == Json::ARR) {
                for (const auto& e : nq->arr) {
                    if (e.type == Json::STR && !e.sval.empty() && g.queue_len < 6)
                        g.queue[g.queue_len++] = char_to_piece(e.sval[0]);
                }
            }
            if (g.queue_len == 0 && g.next >= 0) g.queue[g.queue_len++] = g.next;
            if (auto* v = self->get("hold"); v && v->type == Json::STR && !v->sval.empty())
                g.hold = char_to_piece(v->sval[0]);
            if (auto* v = self->get("can_hold")) g.can_hold = v->as_bool(true);
            if (auto* v = self->get("combo")) g.combo = v->as_int(0);
            if (auto* v = self->get("b2b")) g.b2b = v->as_bool(false);
            if (auto* v = self->get("combo_blocks")) g.combo_blocks = v->as_int(0);
            if (auto* ws = self->get("warnings"); ws && ws->type == Json::ARR) {
                for (const auto& w : ws->arr) {
                    if (w.type == Json::OBJ && g.warnings.count < 8) {
                        int16_t ln = static_cast<int16_t>(w.get("lines") ? w.get("lines")->as_int(0) : 0);
                        int32_t due = w.get("due") ? w.get("due")->as_int(0) : 0;
                        if (ln > 0) g.warnings.items[g.warnings.count++] = {ln, due};
                    }
                }
            }
        }
        if (auto* opp = obs.get("opponent"); opp && opp->type == Json::OBJ) {
            BoardState dummy;
            parse_board(opp->get("board"), dummy, &g.opp_max_h, &g.opp_center_h, &g.opp_holes);
        }

        // Exact 7-Bag tracking
        if (cfg.is_realtime && g.queue_len >= 5) {
            int next_index = g.pieces_placed + 1 + (g.hold >= 0 ? 1 : 0);
            if (g.pieces_placed == 0 && g.hold == P_NONE && bag_stream.empty()) bag_stream.push_back(g.current);
            size_t need = static_cast<size_t>(next_index + g.queue_len);
            if (bag_stream.size() < need) bag_stream.resize(need, P_NONE);
            for (int i = 0; i < g.queue_len; ++i) bag_stream[static_cast<size_t>(next_index + i)] = g.queue[i];
            int next_hidden_idx = next_index + g.queue_len;
            if ((next_hidden_idx % 7) == 6) {
                int bag_start = (next_hidden_idx / 7) * 7;
                uint8_t seen_mask = 0;
                bool valid = true;
                for (int k = 0; k < 6; ++k) {
                    int8_t p = bag_stream[static_cast<size_t>(bag_start + k)];
                    if (p < 0 || (seen_mask & (1u << p))) { valid = false; break; }
                    seen_mask |= static_cast<uint8_t>(1u << p);
                }
                if (valid && std::popcount(seen_mask) == 6 && g.queue_len < 7) {
                    uint8_t missing = static_cast<uint8_t>(0x7Fu & ~seen_mask);
                    g.queue[g.queue_len++] = static_cast<int8_t>(std::countr_zero(missing));
                }
            }
        }
        return g;
    }

    PlayMode select_mode(const GameObs& g, const C4WStats& cs, int now_tick) {
        // Never use C4W on pre-populated test boards with garbage or solid rows from turn 1
        if (g.pieces_placed == 0 && (cs.base > 0 || cs.garbage_rows > 0 || cs.side_cells > 0 || cs.center_cells > 0)) {
            return MODE_GRANDMASTER;
        }
        // Only run C4W before overtime gets high
        if (cs.base >= 5 || (cfg.is_realtime && now_tick >= cfg.overtime_first_frame + 720)) {
            if (mode == MODE_C4W_FIRE && (cs.full_side_rows >= 2 || (g.combo >= 3 && cs.garbage_rows >= 1)) &&
                cs.garb_top + cs.side_max_h <= 22) {
                return MODE_C4W_FIRE; // finish an already-firing massive combo!
            }
            return MODE_GRANDMASTER;
        }

        int soon_warn = g.warnings.due_by(now_tick + 35);
        int total_side_h = cs.garb_top + cs.side_max_h;

        if (mode == MODE_C4W_BUILD) {
            // Adaptive target height: stack 15-17 full side rows -> 45-60 line guaranteed lethal burst!
            int eff_target = std::clamp(21 - cs.garb_top, 12, 17);
            c4w_target_h = eff_target;

            // Ready to unleash the 45-60 line cannon!
            if (cs.full_side_rows >= eff_target && cs.center_cells >= 2) {
                return MODE_C4W_FIRE;
            }
            // Trigger fire right before incoming garbage could overflow our tall towers!
            if (cs.full_side_rows >= 11 && (total_side_h + soon_warn >= 21 || total_side_h >= 21)) {
                return MODE_C4W_FIRE;
            }
            if (cs.full_side_rows >= 8 && (g.opp_max_h >= 15 || total_side_h + soon_warn >= 22)) {
                return MODE_C4W_FIRE;
            }
            if (cs.full_side_rows >= 6 && total_side_h + soon_warn >= 22) {
                return MODE_C4W_FIRE;
            }
            // Only abort build if structure is genuinely ruined
            if (cs.side_holes >= 4 || cs.center_holes >= 3 || cs.center_max_h >= 7 ||
                (total_side_h + soon_warn >= 22 && cs.full_side_rows < 6)) {
                return MODE_GRANDMASTER;
            }
            return MODE_C4W_BUILD;
        }

        if (mode == MODE_C4W_FIRE) {
            // If combo broke AND center well got clogged, cleanly bail to Grandmaster downstack!
            if (g.combo == 0 && (cs.center_max_h >= 6 || cs.center_holes >= 2)) {
                return MODE_GRANDMASTER;
            }
            // Keep firing through ALL side-wall rows AND continue high combo straight through garbage below!
            if (cs.full_side_rows >= 2) return MODE_C4W_FIRE;
            if (g.combo >= 3 && (cs.full_side_rows >= 1 || cs.garbage_rows >= 1)) return MODE_C4W_FIRE;
            // Cannon finished! Transition to Grandmaster downstack / clean play
            return MODE_GRANDMASTER;
        }

        // In MODE_GRANDMASTER: aggressively reload a 2nd-wave C4W cannon whenever clean before overtime!
        if (cs.base <= 2 && cs.garbage_rows <= 4 && cs.side_holes <= 1 && cs.center_holes == 0 &&
            cs.center_max_h <= 3 && total_side_h + soon_warn <= 13 &&
            (!cfg.is_realtime || now_tick < cfg.overtime_first_frame - 400)) {
            c4w_target_h = 15;
            return MODE_C4W_BUILD;
        }
        return MODE_GRANDMASTER;
    }

    Decision decide(const GameObs& g, Clock::time_point deadline) {
        uint64_t bhash = g.self_board.hash64();
        if (has_cached_hold && !g.can_hold &&
            cached_turn == g.turn && cached_piece == g.current && cached_board_hash == bhash) {
            has_cached_hold = false;
            return {false, cached_cmds};
        }
        has_cached_hold = false;

        int now_tick = cfg.is_realtime ? g.frame : g.turn;
        C4WStats root_c4w = inspect_c4w(g.self_board);
        mode = select_mode(g, root_c4w, now_tick);

        struct RootAction {
            bool used_hold = false;
            std::vector<uint8_t> cmds;
            int root_eval = -1000000000;
        };

        struct Node {
            BoardState board;
            WarningQueue warnings;
            int root_idx = 0;
            int8_t hold = P_NONE;
            int8_t q_idx = 0;
            int combo = 0;
            bool b2b = false;
            int combo_blocks = 0;
            int tick = 0;
            int cum_reward = 0;
            int burst_sent = 0;
            int score = -1000000000;
            uint8_t spawn_mask = 0x7F;
            uint64_t dedup_key = 0;
        };

        EvalContext ctx;
        ctx.opp_max_h = g.opp_max_h;
        ctx.opp_center_h = g.opp_center_h;
        ctx.opp_holes = g.opp_holes;
        ctx.incoming_total = g.warnings.total_lines();

        auto set_soon_flags = [&](int8_t hold_p, int q_start) {
            ctx.has_t_soon = (hold_p == P_T);
            ctx.has_i_soon = (hold_p == P_I);
            for (int k = q_start; k < g.queue_len && k < q_start + 3; ++k) {
                if (g.queue[k] == P_T) ctx.has_t_soon = true;
                if (g.queue[k] == P_I) ctx.has_i_soon = true;
            }
        };

        auto pre_feat = (mode == MODE_GRANDMASTER) ? extract_features(g.self_board, false) : BoardFeatures{};
        std::vector<RootAction> root_actions;
        std::vector<Node> beam;
        std::vector<Placement> moves;
        moves.reserve(96);

        const int frames_to_ot = cfg.frames_until_next_overtime(now_tick);
        const bool ot_imminent = cfg.is_realtime && cfg.has_scheduled_overtime && (frames_to_ot <= 45);
        BoardState ot_shifted_board;
        bool ot_shifted_overflow = false;
        if (ot_imminent) {
            ot_shifted_board = g.self_board;
            ot_shifted_overflow = ot_shifted_board.push_solid(1);
        }

        const bool fire_center_only = (mode == MODE_C4W_FIRE && root_c4w.full_side_rows >= 2);

        auto eval_leaf = [&](const SettlementResult& sr, int8_t new_hold) -> int {
            if (!sr.alive) return -1000000000;
            if (mode == MODE_C4W_BUILD) {
                return eval_c4w_build_board(sr.board, new_hold, sr.spawn_mask, sr.warnings, sr.next_frame, c4w_target_h);
            }
            if (mode == MODE_C4W_FIRE) {
                return eval_c4w_fire_board(sr.board, sr.combo, new_hold, sr.spawn_mask);
            }
            return eval_gm_board(cfg, sr.board, sr.combo, sr.b2b, new_hold,
                                 sr.spawn_mask, sr.warnings, sr.next_frame, ctx);
        };

        auto expand_root = [&](int8_t piece, bool used_hold, int8_t new_hold, int8_t next_q_idx) {
            if (piece < 0) return;
            generate_moves_full(cfg, g.self_board, piece, true, scratch, moves, fire_center_only);
            // If center-only yielded no moves in fire mode, fall back to full board!
            if (fire_center_only && moves.empty()) {
                generate_moves_full(cfg, g.self_board, piece, true, scratch, moves, false);
            }
            int8_t next_p = (next_q_idx < g.queue_len) ? g.queue[next_q_idx] : P_NONE;
            set_soon_flags(new_hold, next_q_idx);

            std::vector<Placement> ot_moves;
            if (ot_imminent && !ot_shifted_overflow && frames_to_ot <= 20) {
                generate_moves_full(cfg, ot_shifted_board, piece, true, scratch, ot_moves, false);
                for (auto& om : ot_moves) {
                    int pre_f = om.pre_lock_frames + (used_hold ? cfg.hold_frames : 0);
                    if (pre_f >= frames_to_ot) moves.push_back(std::move(om));
                }
            }

            for (auto& pl : moves) {
                int pre_f = cfg.is_realtime ? (pl.pre_lock_frames + (used_hold ? cfg.hold_frames : 0)) : 0;
                int lock_tick = now_tick + pre_f;
                int pre_ot_delta = cfg.is_realtime ? std::max(0, cfg.overtime_count(lock_tick) - cfg.overtime_count(now_tick)) : 0;

                Placement effective_pl = pl;
                bool pre_ot_applied = false;
                bool pre_ot_dead = false;
                if (cfg.has_scheduled_overtime && pre_ot_delta > 0) {
                    BoardState shifted = g.self_board;
                    if (shifted.push_solid(pre_ot_delta)) pre_ot_dead = true;
                    else {
                        Placement re_pl;
                        if (!execute_cmds_exact(cfg, shifted, piece, pl.cmds, pl.pre_lock_frames, re_pl)) pre_ot_dead = true;
                        else { re_pl.cmds = pl.cmds; effective_pl = std::move(re_pl); pre_ot_applied = true; }
                    }
                }

                SettlementResult sr;
                if (pre_ot_dead) sr.alive = false;
                else sr = apply_settlement(cfg, effective_pl, g.warnings, g.combo, g.b2b, g.combo_blocks,
                                           now_tick, used_hold, next_p, pre_ot_applied);

                int rew = 0;
                if (mode == MODE_C4W_BUILD) rew = eval_c4w_build_reward(effective_pl, sr);
                else if (mode == MODE_C4W_FIRE) rew = eval_c4w_fire_reward(effective_pl, sr, root_c4w.full_side_rows);
                else rew = eval_gm_reward(piece, effective_pl, sr, g.b2b, pre_feat.holes, pre_feat.garbage_rows,
                                          pre_feat.base + pre_ot_delta, pre_feat.max_h + pre_ot_delta, 0, ctx);

                int bval = eval_leaf(sr, new_hold);
                int total = sr.alive ? (bval + rew) : -1000000000;
                int ridx = static_cast<int>(root_actions.size());
                root_actions.push_back({used_hold, std::move(pl.cmds), total});

                if (sr.alive) {
                    Node nd;
                    nd.board = sr.board;
                    nd.warnings = sr.warnings;
                    nd.root_idx = ridx;
                    nd.hold = new_hold;
                    nd.q_idx = next_q_idx;
                    nd.combo = sr.combo;
                    nd.b2b = sr.b2b;
                    nd.combo_blocks = sr.combo_blocks;
                    nd.tick = sr.next_frame;
                    nd.cum_reward = rew;
                    nd.burst_sent = sr.sent;
                    nd.score = total;
                    nd.spawn_mask = sr.spawn_mask;
                    nd.dedup_key = sr.board.hash64()
                                   ^ (static_cast<uint64_t>(new_hold + 2) * 0x9E3779B97F4A7C15ULL)
                                   ^ (static_cast<uint64_t>(sr.b2b) << 61)
                                   ^ (static_cast<uint64_t>(std::min(sr.combo, 15)) << 56)
                                   ^ (static_cast<uint64_t>(next_q_idx) << 52);
                    beam.push_back(std::move(nd));
                }
            }
        };

        expand_root(g.current, false, g.hold, 0);
        if (g.can_hold) {
            if (g.hold >= 0) {
                if (g.hold != g.current) expand_root(g.hold, true, g.current, 0);
            } else if (g.queue_len > 0) {
                if (g.queue[0] != g.current) expand_root(g.queue[0], true, g.current, 1);
            }
        }

        if (root_actions.empty()) return {false, {CMD_HARD_DROP}};

        int best_root_idx = 0;
        int best_root_score = root_actions[0].root_eval;
        for (int i = 1; i < static_cast<int>(root_actions.size()); ++i) {
            if (root_actions[i].root_eval > best_root_score ||
                (root_actions[i].root_eval == best_root_score &&
                 !root_actions[i].used_hold && root_actions[best_root_idx].used_hold)) {
                best_root_score = root_actions[i].root_eval;
                best_root_idx = i;
            }
        }

        auto prune_beam = [&](std::vector<Node>& nodes, int width, int max_per_root) {
            std::stable_sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
                return a.score > b.score;
            });
            std::vector<Node> kept;
            kept.reserve(static_cast<size_t>(width));
            dedup.clear();
            std::vector<uint16_t> root_cnt(root_actions.size(), 0);
            for (auto& nd : nodes) {
                if (max_per_root > 0 && root_cnt[nd.root_idx] >= max_per_root) continue;
                if (!dedup.insert(nd.dedup_key)) continue;
                ++root_cnt[nd.root_idx];
                kept.push_back(std::move(nd));
                if (static_cast<int>(kept.size()) >= width) break;
            }
            if (static_cast<int>(kept.size()) < width && max_per_root > 0) {
                for (auto& nd : nodes) {
                    if (!dedup.insert(nd.dedup_key)) continue;
                    kept.push_back(std::move(nd));
                    if (static_cast<int>(kept.size()) >= width) break;
                }
            }
            nodes = std::move(kept);
        };

        // Blazing-fast mode-adaptive beams tailored for v0.5.3 cumulative timing (~2-8 ms/move!!):
        // - C4W_FIRE: tiny 4-wide search space -> 6-7 plies of exact combo continuation in ~1.5 ms!
        // - C4W_BUILD: fast, razor-clean 5-6 ply wall construction in ~3-4 ms!
        // - GRANDMASTER: crisp 6-7 ply midgame (~7-9 ms), huge overtime survival beam (~5-7 ms)!
        const bool ot_mode = (mode == MODE_GRANDMASTER && pre_feat.base >= 13);
        const int bw_build[6] = {34, 28, 22, 18, 14, 12};
        const int bw_fire[6]  = {52, 46, 40, 34, 28, 22};
        const int bw_gm[6]    = {44, 36, 28, 22, 18, 14};
        const int bw_ot[6]    = {76, 62, 50, 40, 32, 24};
        const int* BEAM_WIDTHS = (mode == MODE_C4W_BUILD) ? bw_build
                               : (mode == MODE_C4W_FIRE)  ? bw_fire
                               : (ot_mode ? bw_ot : bw_gm);
        const int ROOT_CAPS[6] = {0,
                                  (ot_mode || mode == MODE_C4W_FIRE) ? 0 : 10,
                                  (ot_mode || mode == MODE_C4W_FIRE) ? 0 : 14,
                                  0, 0, 0};
        const int max_extra_plies = (g.hold >= 0) ? std::min(6, g.queue_len)
                                                  : std::min(5, std::max(0, g.queue_len - 1));

        prune_beam(beam, BEAM_WIDTHS[0], 0);

        std::vector<Node> next_beam;
        next_beam.reserve(2048);
        std::array<int8_t, BOARD_W> col_h{};

        for (int depth = 0; depth < max_extra_plies && !beam.empty(); ++depth) {
            if (Clock::now() >= deadline) break;

            next_beam.clear();
            bool any_expanded = false;
            bool timed_out = false;

            for (size_t ni = 0; ni < beam.size(); ++ni) {
                if ((ni & 3u) == 0u && Clock::now() >= deadline) { timed_out = true; break; }
                const auto& nd = beam[ni];
                if (nd.q_idx >= g.queue_len) continue;
                any_expanded = true;

                int8_t cur_p = g.queue[nd.q_idx];
                int8_t nxt_idx = static_cast<int8_t>(nd.q_idx + 1);

                nd.board.compute_col_heights(col_h);
                int nf_base = nd.board.solid_floor();
                int nf_max_h = 0, col_sum = 0, cell_cnt = 0;
                for (int x = 0; x < BOARD_W; ++x) {
                    if (col_h[x] > nf_max_h) nf_max_h = col_h[x];
                    col_sum += std::max(0, static_cast<int>(col_h[x]) - nf_base);
                }
                for (int y = nf_base; y < nf_max_h; ++y) cell_cnt += std::popcount(nd.board.rows[y]);
                int nf_holes = std::max(0, col_sum - cell_cnt);
                int nf_garb_rows = std::popcount(nd.board.garbage_mask & ~nd.board.opaque_mask);

                int nd_full_sides = 0;
                if (mode == MODE_C4W_FIRE) {
                    int gtop = nf_base;
                    uint32_t gm = nd.board.garbage_mask | nd.board.opaque_mask;
                    if (gm) gtop = std::max(nf_base, static_cast<int>(std::bit_width(gm)));
                    for (int y = gtop; y < BOARD_H; ++y) {
                        if ((nd.board.rows[y] & SIDES_MASK) == SIDES_MASK) ++nd_full_sides;
                    }
                }
                bool child_center_only = (mode == MODE_C4W_FIRE && nd_full_sides >= 2);
                bool need_tucks = (nf_holes >= 2 || nf_max_h >= 14);
                bool deep_ot = (mode == MODE_GRANDMASTER && nf_base >= 13 && depth < 5);

                auto expand_child = [&](int8_t piece, bool use_hold, int8_t new_hold, int8_t child_q_idx) {
                    if (piece < 0) return;
                    if (mode == MODE_C4W_FIRE) {
                        // Full-kick movegen at ALL depths inside the 4-wide well (only 4 cols wide => ~1.5 us!)
                        // Finds EVERY single SRS tuck/spin combo-saver across all 6 plies!
                        if (child_center_only) {
                            generate_moves_full(cfg, nd.board, piece, false, scratch, moves, true);
                        } else if (depth < 3 && (piece == P_T || nf_holes >= 1)) {
                            generate_moves_full(cfg, nd.board, piece, false, scratch, moves, false);
                        } else {
                            generate_moves_fast_col(cfg, nd.board, col_h, piece, moves, false);
                        }
                    } else if (mode == MODE_C4W_BUILD) {
                        generate_moves_fast_col(cfg, nd.board, col_h, piece, moves, false);
                    } else {
                        bool full_gen = (piece == P_T && depth < 3) || deep_ot ||
                                        (depth == 0 && need_tucks && piece != P_O && piece != P_I);
                        if (full_gen) generate_moves_full(cfg, nd.board, piece, false, scratch, moves, false);
                        else generate_moves_fast_col(cfg, nd.board, col_h, piece, moves, false);
                    }

                    int8_t look_next = (child_q_idx < g.queue_len) ? g.queue[child_q_idx] : P_NONE;
                    set_soon_flags(new_hold, child_q_idx);

                    for (const auto& pl : moves) {
                        auto sr = apply_settlement(cfg, pl, nd.warnings, nd.combo, nd.b2b,
                                                   nd.combo_blocks, nd.tick, use_hold, look_next);
                        if (!sr.alive) continue;

                        int rew = 0;
                        if (mode == MODE_C4W_BUILD) rew = eval_c4w_build_reward(pl, sr);
                        else if (mode == MODE_C4W_FIRE) rew = eval_c4w_fire_reward(pl, sr, nd_full_sides);
                        else rew = eval_gm_reward(piece, pl, sr, nd.b2b, nf_holes, nf_garb_rows,
                                                  nf_base, nf_max_h, nd.burst_sent, ctx);

                        int disc_rew = (mode == MODE_C4W_FIRE) ? ((rew * (11 - depth)) / 12) : ((rew * (9 - depth)) / 10);
                        int cum = nd.cum_reward + disc_rew;
                        int bval = eval_leaf(sr, new_hold);

                        Node c;
                        c.board = sr.board;
                        c.warnings = sr.warnings;
                        c.root_idx = nd.root_idx;
                        c.hold = new_hold;
                        c.q_idx = child_q_idx;
                        c.combo = sr.combo;
                        c.b2b = sr.b2b;
                        c.combo_blocks = sr.combo_blocks;
                        c.tick = sr.next_frame;
                        c.cum_reward = cum;
                        c.burst_sent = (sr.sent > 0) ? std::min(14, sr.sent + nd.burst_sent / 2) : (nd.burst_sent / 3);
                        c.score = bval + cum;
                        c.spawn_mask = sr.spawn_mask;
                        c.dedup_key = sr.board.hash64()
                                      ^ (static_cast<uint64_t>(new_hold + 2) * 0x9E3779B97F4A7C15ULL)
                                      ^ (static_cast<uint64_t>(sr.b2b) << 61)
                                      ^ (static_cast<uint64_t>(std::min(sr.combo, 15)) << 56)
                                      ^ (static_cast<uint64_t>(child_q_idx) << 52);
                        next_beam.push_back(std::move(c));
                    }
                };

                expand_child(cur_p, false, nd.hold, nxt_idx);
                if (nd.hold >= 0) {
                    if (nd.hold != cur_p) expand_child(nd.hold, true, cur_p, nxt_idx);
                } else if (nxt_idx < g.queue_len && g.queue[nxt_idx] != cur_p) {
                    expand_child(g.queue[nxt_idx], true, cur_p, static_cast<int8_t>(nxt_idx + 1));
                }
            }

            if (!any_expanded || timed_out || next_beam.empty()) break;

            int next_w = (depth + 1 < 6) ? BEAM_WIDTHS[depth + 1] : 16;
            int cap = (depth + 1 < 6) ? ROOT_CAPS[depth + 1] : 0;
            prune_beam(next_beam, next_w, cap);
            beam = std::move(next_beam);

            if (!beam.empty()) {
                best_root_idx = beam[0].root_idx;
                best_root_score = beam[0].score;
            }
        }

        const auto& chosen = root_actions[best_root_idx];
        if (chosen.used_hold) {
            has_cached_hold = true;
            cached_turn = g.turn;
            cached_piece = (g.hold >= 0) ? g.hold : (g.queue_len > 0 ? g.queue[0] : P_NONE);
            cached_board_hash = bhash;
            cached_cmds = chosen.cmds;
            return {true, {}};
        }
        return {false, chosen.cmds};
    }
};

} // namespace tetra

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    tetra::StrongBot bot;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto started = tetra::Clock::now();

        tetra::Json req;
        tetra::JsonParser parser(line);
        if (!parser.parse(req)) continue;

        std::string req_id;
        if (auto* rid = req.get("request_id"); rid && rid->type == tetra::Json::STR) {
            req_id = rid->sval;
        }
        auto* obs = req.get("observation");
        if (!obs || obs->type != tetra::Json::OBJ) {
            std::cout << "{\"type\":\"PLAY\",\"request_id\":\"" << tetra::json_escape(req_id)
                      << "\",\"commands\":[\"HARD_DROP\"]}\n" << std::flush;
            continue;
        }

        tetra::GameObs g = bot.parse_observation(*obs);

        const auto* rem_j = req.get("remaining_ms");
        tetra::Clock::time_point deadline = tetra::Clock::time_point::max();
        if (rem_j && rem_j->type == tetra::Json::NUM) {
            int remaining_ms = rem_j->as_int(100);
            int bank_ms = (req.get("bank_ms") && req.get("bank_ms")->type == tetra::Json::NUM)
                              ? req.get("bank_ms")->as_int(-1) : -1;
            int turn_budget = remaining_ms;
            if (bank_ms >= 0) {
                turn_budget = std::min(remaining_ms, std::max(0, remaining_ms - bank_ms) + std::min(bank_ms, 80));
            }
            // Fast target under v0.5.3 cumulative thinking (~3-8 ms in C4W/midgame, allow 24 ms in deep overtime!)
            int solid_h = g.self_board.solid_floor();
            int cap_ms = (solid_h >= 14) ? 24 : (bot.cfg.has_cumulative_thinking ? 14 : std::max(14, bot.cfg.free_think_ms - 6));
            int target_ms = std::min(cap_ms, std::max(0, turn_budget - 10));
            deadline = started + std::chrono::milliseconds(target_ms);
        }

        tetra::Decision dec = bot.decide(g, deadline);

        std::cout << "{\"type\":\"" << (dec.is_hold ? "HOLD" : "PLAY")
                  << "\",\"request_id\":\"" << tetra::json_escape(req_id) << "\"";
        if (!dec.is_hold) {
            std::cout << ",\"commands\":[";
            if (dec.commands.empty()) {
                std::cout << "\"HARD_DROP\"";
            } else {
                for (size_t i = 0; i < dec.commands.size(); ++i) {
                    if (i > 0) std::cout << ',';
                    std::cout << '"' << tetra::CMD_NAMES[dec.commands[i]] << '"';
                }
            }
            std::cout << "]";
        }
        std::cout << "}\n" << std::flush;
    }
    return 0;
}
